#include "slideio/viewer/core/AnnotationGeoJson.h"

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <type_traits>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

// Ordered, as the serializer's is: "type" first in every object, so the file
// reads naturally when someone does open it.
using json = nlohmann::ordered_json;

json position(const PointF& point)
{
    return json::array({point.x, point.y});
}

json positions(const std::vector<PointF>& points)
{
    json result = json::array();
    for (const PointF& point : points) {
        result.push_back(position(point));
    }
    return result;
}

/// The points a shape is written from: its outline, unless the outline has
/// dropped so many duplicates that GeoJSON could not hold it. Then the stored
/// vertices, which the serializer guarantees number at least the minimum for
/// their kind -- so nothing is ever dropped, only written as it was stored.
std::vector<PointF> exportPoints(const PolygonGeometry& poly)
{
    const Outline shape = outline(poly);
    const std::size_t minimum = poly.closed ? kMinPolygonVertices : kMinPolylineVertices;
    return shape.points.size() < minimum ? poly.vertices : shape.points;
}

json ring(std::vector<PointF> points)
{
    // RFC 7946: a linear ring ends where it starts.
    if (!points.empty()) {
        points.push_back(points.front());
    }
    return json::array({positions(points)});
}

json polygonGeometry(std::vector<PointF> points)
{
    return json{{"type", "Polygon"}, {"coordinates", ring(std::move(points))}};
}

json lineGeometry(const std::vector<PointF>& points)
{
    return json{{"type", "LineString"}, {"coordinates", positions(points)}};
}

/// One overload per alternative and no generic fallback: a new shape is a
/// compile error here, as it is at every other exhaustive dispatch site,
/// rather than a feature written with the wrong geometry.
struct GeometryVisitor
{
    json operator()(const RectangleGeometry& shape) const
    {
        return polygonGeometry(outline(shape).points);
    }
    json operator()(const EllipseGeometry& shape) const
    {
        // The 512-gon outline() already is. GeoJSON has no ellipse.
        return polygonGeometry(outline(shape).points);
    }
    json operator()(const PolygonGeometry& shape) const
    {
        return shape.closed ? polygonGeometry(exportPoints(shape))
                            : lineGeometry(exportPoints(shape));
    }
    json operator()(const PointGeometry& shape) const
    {
        return json{{"type", "Point"}, {"coordinates", position(shape.position)}};
    }
    json operator()(const ArrowGeometry& shape) const
    {
        // Tail first, head second: the order is the arrow's direction, and it
        // is all GeoJSON can keep of it.
        return lineGeometry(outline(shape).points);
    }
};

/// The tally's rule, so the file and the counting strip agree on who has no
/// class: blank, or "Unclassified" in any case. isUnclassifiedCategory alone
/// is the second half. ASCII whitespace only, compared by value: std::isspace
/// reads the C locale and under a single-byte one would call a UTF-8 lead
/// byte whitespace.
bool isUnclassified(const std::string& classification)
{
    const bool blank = classification.find_first_not_of(" \t\r\n\f\v") == std::string::npos;
    return blank || isUnclassifiedCategory(classification);
}

json rgb(const Color& color)
{
    return json::array({color.r, color.g, color.b});
}

/// QuPath's own fields first, then everything QuPath has no field for under
/// one `slideio` key, so it cannot collide with a property QuPath or a later
/// reader defines. `false` when a timestamp cannot be written.
bool buildProperties(const Annotation& annotation, json& out)
{
    const AnnotationProperties& props = annotation.properties();
    const AnnotationMetadata& meta = annotation.metadata();
    const std::string createdAt = formatIso8601Utc(meta.createdAt);
    const std::string modifiedAt = formatIso8601Utc(meta.modifiedAt);
    if (createdAt.empty() || modifiedAt.empty()) {
        return false;
    }

    out = json{{"objectType", "annotation"}};
    if (!props.label.empty()) {
        out["name"] = props.label;
    }
    // The object's own colour, separate from the class colour in QuPath, so
    // two annotations of one class drawn in different colours keep them.
    out["color"] = rgb(props.color);
    // QuPath represents "no class" by the absence of the field; a class
    // literally named Unclassified would appear there as a real one.
    if (!isUnclassified(props.classification)) {
        out["classification"] = json{{"name", props.classification}, {"color", rgb(props.color)}};
    }
    out["slideio"] = json{{"type", annotationTypeName(annotation.geometry())},
                          {"notes", props.notes},
                          {"author", meta.author},
                          {"createdAt", createdAt},
                          {"modifiedAt", modifiedAt},
                          {"color", formatAnnotationColor(props.color)},
                          {"lineWidth", props.lineWidth},
                          {"fillOpacity", props.fillOpacity}};
    return true;
}

json provenance(const GeoJsonExportContext& context, const std::string& exportedAt)
{
    json block{{"format", "slideio-viewer annotation export"},
               {"formatVersion", 1},
               {"slide", context.slidePath},
               {"slideId", context.slideId},
               {"scene", context.sceneIndex},
               {"exportedAt", exportedAt},
               {"coordinates", "level-0 pixels of the scene; origin top-left, x right, y down"}};
    // Unknown is said by absence, never by a zero that reads as a measurement.
    if (context.pixelSizeMicronsX > 0.0 && context.pixelSizeMicronsY > 0.0) {
        block["pixelSizeMicrons"] = json::array({context.pixelSizeMicronsX, context.pixelSizeMicronsY});
    }
    return block;
}

bool allFinite(const Outline& shape)
{
    for (const PointF& point : shape.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return false;
        }
    }
    return true;
}

} // namespace

GeoJsonExportResult annotationsToGeoJson(const std::vector<Annotation>& annotations,
                                         const GeoJsonExportContext& context,
                                         std::chrono::system_clock::time_point exportedAt)
{
    const std::string exportTime = formatIso8601Utc(exportedAt);
    if (exportTime.empty()) {
        return {{}, "the export time cannot be written as a date"};
    }

    json features = json::array();
    for (const Annotation& annotation : annotations) {
        // outline() covers every coordinate a geometry stores except the
        // vertices it drops as duplicates, and those equal a kept one.
        if (!allFinite(outline(annotation.geometry()))) {
            return {{}, "annotation " + annotation.id() + " has a coordinate that is not a finite number"};
        }
        json properties;
        if (!buildProperties(annotation, properties)) {
            return {{}, "annotation " + annotation.id() + " has a date that cannot be written"};
        }
        features.push_back(json{{"type", "Feature"},
                                {"id", annotation.id()},
                                {"geometry", std::visit(GeometryVisitor{}, annotation.geometry())},
                                {"properties", std::move(properties)}});
    }

    json collection{{"type", "FeatureCollection"},
                    {"slideio", provenance(context, exportTime)},
                    {"features", std::move(features)}};
    // Compact: an ellipse alone is 513 positions, and pretty printing puts
    // each number on its own line. `replace` turns invalid UTF-8 -- annotation
    // files are hand-editable -- into U+FFFD instead of throwing, and leaves
    // valid non-ASCII unescaped.
    return {collection.dump(-1, ' ', false, json::error_handler_t::replace) + "\n", {}};
}

} // namespace slideio::viewer::core
