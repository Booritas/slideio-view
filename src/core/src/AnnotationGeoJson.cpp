#include "slideio/viewer/core/AnnotationGeoJson.h"

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
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

namespace
{

// Reading needs no key order, and the file is somebody else's.
using InJson = nlohmann::json;

/// Why a feature's geometry produced no annotation. Each is one counter of
/// GeoJsonImportResult.
enum class GeometrySkip
{
    None,
    Holes,
    Unsupported,
    Invalid,
};

struct GeometryRead
{
    std::vector<AnnotationGeometry> parts;
    GeometrySkip skip = GeometrySkip::None;
};

GeometryRead skipped(GeometrySkip reason)
{
    GeometryRead read;
    read.skip = reason;
    return read;
}

const InJson* member(const InJson& object, const char* key)
{
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

std::string stringMember(const InJson& object, const char* key)
{
    const InJson* value = member(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string();
}

/// One GeoJSON position. A third number, z, is ignored: annotations here are
/// not per plane. Anything but two finite numbers is refused.
bool readPosition(const InJson& value, PointF& out)
{
    if (!value.is_array() || value.size() < 2 || !value[0].is_number() || !value[1].is_number()) {
        return false;
    }
    const double x = value[0].get<double>();
    const double y = value[1].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    out = PointF{x, y};
    return true;
}

bool readPositions(const InJson& value, std::vector<PointF>& out)
{
    if (!value.is_array()) {
        return false;
    }
    out.clear();
    out.reserve(value.size());
    for (const InJson& item : value) {
        PointF point;
        if (!readPosition(item, point)) {
            return false;
        }
        out.push_back(point);
    }
    return true;
}

/// A polygon's exterior ring, its closing repeat dropped, or false when it
/// has fewer vertices than a saved polygon may. Counted, not de-duplicated:
/// the serializer counts stored vertices, and a degenerate polygon this
/// application saved and exported must come back.
bool readRing(const InJson& value, std::vector<PointF>& out)
{
    if (!readPositions(value, out)) {
        return false;
    }
    if (out.size() > 1 && out.front().x == out.back().x && out.front().y == out.back().y) {
        out.pop_back();
    }
    return out.size() >= kMinPolygonVertices;
}

/// The four corners of an axis-aligned rectangle, as QuPath writes its
/// Rectangle ROI and as the export writes a rectangle: four distinct points,
/// two distinct x and two distinct y, every edge horizontal or vertical.
bool isAxisAlignedBox(const std::vector<PointF>& ring)
{
    if (ring.size() != 4) {
        return false;
    }
    std::set<double> xs;
    std::set<double> ys;
    for (std::size_t i = 0; i < 4; ++i) {
        const PointF& a = ring[i];
        const PointF& b = ring[(i + 1) % 4];
        if (a.x != b.x && a.y != b.y) {
            return false;
        }
        for (std::size_t j = i + 1; j < 4; ++j) {
            if (ring[i].x == ring[j].x && ring[i].y == ring[j].y) {
                return false;
            }
        }
        xs.insert(a.x);
        ys.insert(a.y);
    }
    return xs.size() == 2 && ys.size() == 2;
}

RectF boundsOf(const std::vector<PointF>& points)
{
    double minX = points.front().x;
    double maxX = minX;
    double minY = points.front().y;
    double maxY = minY;
    for (const PointF& p : points) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    return RectF{minX, minY, maxX - minX, maxY - minY};
}

/// A closed ring as the viewer's shape for it: a rectangle when it is one.
AnnotationGeometry areaFrom(std::vector<PointF> ring)
{
    if (isAxisAlignedBox(ring)) {
        const RectF box = boundsOf(ring);
        return RectangleGeometry{PointF{box.x, box.y}, PointF{box.x + box.width, box.y + box.height}};
    }
    return PolygonGeometry{std::move(ring), true};
}

/// A Polygon's rings. Holes are refused before validity, so a holed polygon
/// is reported as holed even when its holes are also malformed.
GeometrySkip readPolygonRings(const InJson& coordinates, std::vector<PointF>& exterior)
{
    if (!coordinates.is_array() || coordinates.empty()) {
        return GeometrySkip::Invalid;
    }
    if (coordinates.size() > 1) {
        return GeometrySkip::Holes;
    }
    return readRing(coordinates[0], exterior) ? GeometrySkip::None : GeometrySkip::Invalid;
}

/// The geometry by its GeoJSON type alone -- a QuPath file, or anything else.
GeometryRead readGeometry(const InJson& geometry)
{
    const std::string type = stringMember(geometry, "type");
    const InJson* coordinatesMember = member(geometry, "coordinates");
    const InJson coordinates = coordinatesMember != nullptr ? *coordinatesMember : InJson();

    GeometryRead read;
    if (type == "Point") {
        PointF point;
        if (!readPosition(coordinates, point)) {
            return skipped(GeometrySkip::Invalid);
        }
        read.parts.emplace_back(PointGeometry{point});
    } else if (type == "MultiPoint") {
        // QuPath's Points tool writes a whole counting class as one MultiPoint;
        // each position is a counted object here.
        std::vector<PointF> points;
        if (!readPositions(coordinates, points) || points.empty()) {
            return skipped(GeometrySkip::Invalid);
        }
        for (const PointF& point : points) {
            read.parts.emplace_back(PointGeometry{point});
        }
    } else if (type == "LineString" || type == "MultiLineString") {
        const bool multi = type == "MultiLineString";
        if (multi && (!coordinates.is_array() || coordinates.empty())) {
            return skipped(GeometrySkip::Invalid);
        }
        const InJson lines = multi ? coordinates : InJson::array({coordinates});
        for (const InJson& line : lines) {
            std::vector<PointF> points;
            if (!readPositions(line, points) || points.size() < kMinPolylineVertices) {
                return skipped(GeometrySkip::Invalid);
            }
            read.parts.emplace_back(PolygonGeometry{std::move(points), false});
        }
    } else if (type == "Polygon" || type == "MultiPolygon") {
        const bool multi = type == "MultiPolygon";
        if (multi && (!coordinates.is_array() || coordinates.empty())) {
            return skipped(GeometrySkip::Invalid);
        }
        const InJson polygons = multi ? coordinates : InJson::array({coordinates});
        GeometrySkip worst = GeometrySkip::None;
        for (const InJson& polygon : polygons) {
            std::vector<PointF> ring;
            const GeometrySkip skip = readPolygonRings(polygon, ring);
            if (skip == GeometrySkip::Holes) {
                worst = GeometrySkip::Holes;
            } else if (skip == GeometrySkip::Invalid) {
                if (worst == GeometrySkip::None) {
                    worst = GeometrySkip::Invalid;
                }
            } else {
                read.parts.push_back(areaFrom(std::move(ring)));
            }
        }
        if (worst != GeometrySkip::None) {
            return skipped(worst);
        }
    } else {
        return skipped(GeometrySkip::Unsupported);
    }
    return read;
}

/// The shape this application exported, when the feature's `slideio.type`
/// fits its geometry; nullopt otherwise, and the GeoJSON type decides. The
/// geometry wins any disagreement, so a hand-edited or foreign block cannot
/// produce a shape the file does not describe.
std::optional<AnnotationGeometry> readExportedShape(const std::string& slideioType, const InJson& geometry)
{
    const std::string type = stringMember(geometry, "type");
    const InJson* coordinatesMember = member(geometry, "coordinates");
    const InJson coordinates = coordinatesMember != nullptr ? *coordinatesMember : InJson();

    if (type == "Polygon" && (slideioType == "rectangle" || slideioType == "ellipse" || slideioType == "polygon")) {
        std::vector<PointF> ring;
        if (readPolygonRings(coordinates, ring) != GeometrySkip::None) {
            return std::nullopt;
        }
        if (slideioType == "polygon") {
            // Kept a polygon even when it happens to be an axis-aligned box.
            return AnnotationGeometry{PolygonGeometry{std::move(ring), true}};
        }
        if (slideioType == "rectangle" && !isAxisAlignedBox(ring)) {
            return std::nullopt;
        }
        // The 512-gon's vertices include the box's four extremes, so its
        // bounds are the ellipse's box; a rectangle's are its corners.
        const RectF box = boundsOf(ring);
        const PointF topLeft{box.x, box.y};
        const PointF bottomRight{box.x + box.width, box.y + box.height};
        if (slideioType == "rectangle") {
            return AnnotationGeometry{RectangleGeometry{topLeft, bottomRight}};
        }
        return AnnotationGeometry{EllipseGeometry{topLeft, bottomRight}};
    }
    if (type == "LineString" && slideioType == "arrow") {
        std::vector<PointF> points;
        if (!readPositions(coordinates, points) || points.size() != 2) {
            return std::nullopt;
        }
        // Tail first: the export's order is the arrow's direction.
        return AnnotationGeometry{ArrowGeometry{points[0], points[1]}};
    }
    return std::nullopt;
}

/// `[r, g, b]`, every channel an integer 0-255; alpha opaque.
bool readRgbArray(const InJson* value, Color& out)
{
    if (value == nullptr || !value->is_array() || value->size() < 3) {
        return false;
    }
    std::uint8_t channels[3] = {};
    for (std::size_t i = 0; i < 3; ++i) {
        const InJson& channel = (*value)[i];
        if (!channel.is_number_integer()) {
            return false;
        }
        const long long v = channel.get<long long>();
        if (v < 0 || v > 255) {
            return false;
        }
        channels[i] = static_cast<std::uint8_t>(v);
    }
    out = Color{channels[0], channels[1], channels[2], 255};
    return true;
}

/// QuPath's legacy packed colour: an int whose low 24 bits are RGB, often
/// negative because its top byte is an opaque alpha.
bool readPackedRgb(const InJson* value, Color& out)
{
    if (value == nullptr || !value->is_number_integer()) {
        return false;
    }
    const auto packed = static_cast<std::uint32_t>(value->get<long long>());
    out = Color{static_cast<std::uint8_t>((packed >> 16) & 0xFF), static_cast<std::uint8_t>((packed >> 8) & 0xFF),
                static_cast<std::uint8_t>(packed & 0xFF), 255};
    return true;
}

std::string readClassification(const InJson* value)
{
    if (value == nullptr) {
        return {};
    }
    if (value->is_string()) {
        return value->get<std::string>();
    }
    // QuPath's derived classes are a list of names, which it shows joined by ": ".
    if (const InJson* names = member(*value, "names"); names != nullptr && names->is_array() && !names->empty()) {
        std::string joined;
        for (const InJson& name : *names) {
            if (!name.is_string()) {
                return {};
            }
            joined += (joined.empty() ? "" : ": ") + name.get<std::string>();
        }
        return joined;
    }
    return stringMember(*value, "name");
}

/// The file's colour for a feature, most specific source first; false when
/// none of them holds a usable one.
bool readColor(const InJson& properties, const InJson& slideio, Color& out)
{
    const std::string exported = stringMember(slideio, "color");
    if (!exported.empty() && parseAnnotationColor(exported, out)) {
        return true;
    }
    if (readRgbArray(member(properties, "color"), out) || readPackedRgb(member(properties, "colorRGB"), out)) {
        return true;
    }
    const InJson* classification = member(properties, "classification");
    return classification != nullptr
        && (readRgbArray(member(*classification, "color"), out)
            || readPackedRgb(member(*classification, "colorRGB"), out));
}

std::chrono::system_clock::time_point readTimestamp(const InJson& slideio, const char* key,
                                                    std::chrono::system_clock::time_point fallback)
{
    std::chrono::system_clock::time_point parsed;
    const std::string text = stringMember(slideio, key);
    return !text.empty() && parseIso8601Utc(text, parsed) ? parsed : fallback;
}

/// Everything but the geometry, from the most specific source the feature
/// offers. A malformed value falls through to the next source or the
/// default; it never rejects the annotation -- the shape is the finding, the
/// styling is not.
void readProperties(const InJson& properties, std::chrono::system_clock::time_point importedAt,
                    AnnotationProperties& outProperties, AnnotationMetadata& outMetadata)
{
    const InJson* slideioMember = member(properties, "slideio");
    const InJson slideio = slideioMember != nullptr && slideioMember->is_object() ? *slideioMember : InJson::object();

    outProperties = AnnotationProperties{};
    outProperties.label = stringMember(properties, "name");
    outProperties.classification = readClassification(member(properties, "classification"));
    if (isUnclassified(outProperties.classification)) {
        outProperties.classification.clear();
    }
    Color color;
    if (readColor(properties, slideio, color)) {
        outProperties.color = color;
    }

    // Each style value is kept only if a saved file could hold it.
    if (const InJson* width = member(slideio, "lineWidth"); width != nullptr && width->is_number()) {
        AnnotationProperties candidate;
        candidate.lineWidth = width->get<float>();
        if (isRepresentableStyle(candidate)) {
            outProperties.lineWidth = candidate.lineWidth;
        }
    }
    if (const InJson* opacity = member(slideio, "fillOpacity"); opacity != nullptr && opacity->is_number()) {
        AnnotationProperties candidate;
        candidate.fillOpacity = opacity->get<float>();
        if (isRepresentableStyle(candidate)) {
            outProperties.fillOpacity = candidate.fillOpacity;
        }
    }
    outProperties.notes = stringMember(slideio, "notes");

    // The author is whoever drew it, never whoever imported it, and QuPath
    // records none. Timestamps carry the file format's whole seconds.
    outMetadata = AnnotationMetadata{};
    outMetadata.author = stringMember(slideio, "author");
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(importedAt);
    outMetadata.createdAt = readTimestamp(slideio, "createdAt", now);
    outMetadata.modifiedAt = readTimestamp(slideio, "modifiedAt", now);
}

std::string readId(const InJson& feature)
{
    const InJson* id = member(feature, "id");
    if (id == nullptr) {
        return {};
    }
    if (id->is_string()) {
        return id->get<std::string>();
    }
    return id->is_number() ? id->dump() : std::string();
}

/// QuPath's nested objects, wherever its version put them, counted to the bottom.
int countNested(const InJson& feature)
{
    int count = 0;
    for (const InJson* holder : {&feature, member(feature, "properties")}) {
        const InJson* children = holder != nullptr ? member(*holder, "childObjects") : nullptr;
        if (children == nullptr || !children->is_array()) {
            continue;
        }
        for (const InJson& child : *children) {
            count += 1 + countNested(child);
        }
    }
    return count;
}

void readFeature(const InJson& feature, std::chrono::system_clock::time_point importedAt,
                 GeoJsonImportResult& result)
{
    ++result.featuresRead;
    result.nestedObjects += countNested(feature);

    const InJson* propertiesMember = member(feature, "properties");
    const InJson properties =
        propertiesMember != nullptr && propertiesMember->is_object() ? *propertiesMember : InJson::object();

    // No object type at all is a plain GeoJSON file from another tool: a set
    // of annotations to the person importing it.
    std::string objectType = stringMember(properties, "objectType");
    if (objectType.empty()) {
        objectType = stringMember(properties, "object_type");
    }
    if (!objectType.empty() && objectType != "annotation") {
        ++result.skippedObjectTypes[objectType];
        return;
    }

    const InJson* geometryMember = member(feature, "geometry");
    const InJson geometry = geometryMember != nullptr ? *geometryMember : InJson();
    const InJson* slideio = member(properties, "slideio");
    const std::string slideioType = slideio != nullptr ? stringMember(*slideio, "type") : std::string();

    GeometryRead read;
    if (std::optional<AnnotationGeometry> exported = readExportedShape(slideioType, geometry)) {
        read.parts.push_back(std::move(*exported));
    } else {
        read = readGeometry(geometry);
    }
    switch (read.skip) {
    case GeometrySkip::Holes:
        ++result.withHoles;
        return;
    case GeometrySkip::Unsupported:
        ++result.unsupportedGeometry;
        return;
    case GeometrySkip::Invalid:
        ++result.invalidGeometry;
        return;
    case GeometrySkip::None:
        break;
    }

    AnnotationProperties annotationProperties;
    AnnotationMetadata metadata;
    readProperties(properties, importedAt, annotationProperties, metadata);

    // Every part carries the feature's properties. The first keeps the
    // feature's id, so a re-import still finds it already present; the rest
    // are new.
    const std::string id = readId(feature);
    for (std::size_t i = 0; i < read.parts.size(); ++i) {
        result.annotations.emplace_back(i == 0 ? id : std::string(), std::move(read.parts[i]),
                                        annotationProperties, metadata);
    }
    ++result.featuresImported;
}

GeoJsonImportResult importFailure(std::string message)
{
    GeoJsonImportResult result;
    result.error = std::move(message);
    return result;
}

} // namespace

GeoJsonImportResult geoJsonToAnnotations(std::string_view text, std::chrono::system_clock::time_point importedAt)
{
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (text.substr(0, kBom.size()) == kBom) {
        text.remove_prefix(kBom.size());
    }
    // No exceptions on the way in: a file that is not JSON, or not UTF-8, is a
    // file we report, not one we crash on.
    const InJson document = InJson::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded()) {
        return importFailure("the file is not valid JSON");
    }

    try {
        GeoJsonImportResult result;
        const std::string type = stringMember(document, "type");
        InJson features;
        if (document.is_array()) {
            features = document;
        } else if (type == "Feature") {
            features = InJson::array({document});
        } else if (type == "FeatureCollection") {
            const InJson* listed = member(document, "features");
            if (listed == nullptr || !listed->is_array()) {
                return importFailure("the FeatureCollection has no list of features");
            }
            features = *listed;
            if (const InJson* slideio = member(document, "slideio"); slideio != nullptr && slideio->is_object()) {
                result.provenance.present = true;
                result.provenance.slide = stringMember(*slideio, "slide");
                result.provenance.slideId = stringMember(*slideio, "slideId");
                const InJson* scene = member(*slideio, "scene");
                if (scene != nullptr && scene->is_number_integer()) {
                    result.provenance.scene = scene->get<int>();
                }
            }
        } else {
            return importFailure("the file is not a GeoJSON Feature, FeatureCollection or list of Features");
        }

        for (const InJson& feature : features) {
            readFeature(feature, importedAt, result);
        }
        return result;
    } catch (const nlohmann::json::exception& e) {
        // A backstop: every field is type-checked before it is read.
        return importFailure(std::string("the file could not be read: ") + e.what());
    }
}

} // namespace slideio::viewer::core
