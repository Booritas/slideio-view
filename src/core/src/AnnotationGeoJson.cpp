#include "slideio/viewer/core/AnnotationGeoJson.h"

#include "slideio/viewer/core/AnnotationGeometry.h"

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
    (void)context;
    (void)exportedAt;

    json features = json::array();
    for (const Annotation& annotation : annotations) {
        // outline() covers every coordinate a geometry stores except the
        // vertices it drops as duplicates, and those equal a kept one.
        if (!allFinite(outline(annotation.geometry()))) {
            return {{}, "annotation " + annotation.id() + " has a coordinate that is not a finite number"};
        }
        json properties{{"objectType", "annotation"}};
        features.push_back(json{{"type", "Feature"},
                                {"id", annotation.id()},
                                {"geometry", std::visit(GeometryVisitor{}, annotation.geometry())},
                                {"properties", std::move(properties)}});
    }

    json collection{{"type", "FeatureCollection"},
                    {"slideio", json::object()},
                    {"features", std::move(features)}};
    return {collection.dump() + "\n", {}};
}

} // namespace slideio::viewer::core
