#include "slideio/viewer/core/AnnotationGeoJson.h"

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
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
    json operator()(const TextGeometry& shape) const
    {
        // GeoJSON and QuPath have no text: a Point named with the words is the
        // nearest, and it is where the note was. slideio.type keeps it a text.
        return json{{"type", "Point"}, {"coordinates", position(shape.anchor)}};
    }
    // Lines through their points in the order drawn; slideio.type is what keeps
    // them a ruler and an angle rather than an ordinary line on the way back.
    json operator()(const RulerGeometry& shape) const
    {
        return lineGeometry(outline(shape).points);
    }
    json operator()(const AngleGeometry& shape) const
    {
        return lineGeometry(outline(shape).points);
    }
    json operator()(const ArrowGeometry& shape) const
    {
        // Tail first, head second: the order is the arrow's direction, and it
        // is all GeoJSON can keep of it.
        return lineGeometry(outline(shape).points);
    }
};

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
    // Trimmed, as categoryTally counts it: " Tumor " is a Tumor row on the
    // strip, and would be a second class of its own in QuPath.
    if (!isBlankOrUnclassified(props.classification)) {
        out["classification"] = json{{"name", trimAsciiWhitespace(props.classification)}, {"color", rgb(props.color)}};
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
    // Unknown is said by absence, never by a zero that reads as a measurement
    // -- nor by a null, which is what nlohmann writes for an infinity or NaN.
    const auto known = [](double size) { return std::isfinite(size) && size > 0.0; };
    if (known(context.pixelSizeMicronsX) && known(context.pixelSizeMicronsY)) {
        block["pixelSizeMicrons"] = json::array({context.pixelSizeMicronsX, context.pixelSizeMicronsY});
    }
    // Always written, "none" included: a reader must be able to tell a manual
    // calibration from the scanner's, and an absent key would say neither.
    block["calibrationSource"] = calibrationSourceName(context.calibrationSource);
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
        // JSON has no NaN or infinity: nlohmann would write null, and the
        // saved-file reader refuses it -- so a value the serializer would not
        // write is not written here either.
        if (!isRepresentableStyle(annotation.properties())) {
            return {{}, "annotation " + annotation.id()
                            + " has a line width or fill opacity that cannot be written"};
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
/// AnnotationImportResult.
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
    /// A Multi* geometry, whose parts are identified by seed rather than by
    /// the feature's id -- even when it has only one part, which a later
    /// version of the same QuPath class may not.
    bool multi = false;
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
/// has fewer vertices than a saved polygon may or no finite extent. Counted,
/// not de-duplicated: the serializer counts stored vertices, and a degenerate
/// polygon this application saved and exported must come back.
bool readRing(const InJson& value, std::vector<PointF>& out)
{
    if (!readPositions(value, out)) {
        return false;
    }
    if (out.size() > 1 && out.front().x == out.back().x && out.front().y == out.back().y) {
        out.pop_back();
    }
    return out.size() >= kMinPolygonVertices && hasFiniteExtent(out);
}

/// A line's positions, or false when it has fewer than a saved line may or no
/// finite extent.
bool readLine(const InJson& value, std::vector<PointF>& out)
{
    return readPositions(value, out) && out.size() >= kMinPolylineVertices && hasFiniteExtent(out);
}

/// The four corners of a box as the export writes a rectangle, degenerate ones
/// included: a rectangle's outline keeps all four corners even when they
/// coincide, so a zero-width or zero-size rectangle is written as a ring whose
/// corners repeat. Every corner on the bounding box's corners, every edge
/// horizontal or vertical. Used only where the file says "rectangle" -- a
/// foreign ring of coincident points is a degenerate polygon, not a box.
bool isBoxOutline(const std::vector<PointF>& ring)
{
    if (ring.size() != 4) {
        return false;
    }
    const auto [low, high] = cornersOf(ring);
    for (std::size_t i = 0; i < 4; ++i) {
        const PointF& a = ring[i];
        const PointF& b = ring[(i + 1) % 4];
        if ((a.x != low.x && a.x != high.x) || (a.y != low.y && a.y != high.y)) {
            return false;
        }
        if (a.x != b.x && a.y != b.y) {
            return false;
        }
    }
    return true;
}

/// Stands in for a member the file left out, by reference. Nothing parsed is
/// copied: nlohmann copies recursively. The parse itself caps the depth, so
/// this is no longer what keeps a deep file from crashing -- only what keeps a
/// large one from being duplicated in memory.
const InJson& absent()
{
    static const InJson kNull;
    return kNull;
}

const InJson& emptyObject()
{
    static const InJson kEmpty = InJson::object();
    return kEmpty;
}

const InJson& coordinatesOf(const InJson& geometry)
{
    const InJson* coordinates = member(geometry, "coordinates");
    return coordinates != nullptr ? *coordinates : absent();
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

/// The coordinates of each part: `coordinates` itself for a single geometry,
/// each of its elements for a Multi* one. Empty when a Multi* geometry has no
/// list of parts. Pointers, so a single geometry needs no one-element array.
std::vector<const InJson*> partsOf(const InJson& coordinates, bool multi)
{
    std::vector<const InJson*> parts;
    if (!multi) {
        parts.push_back(&coordinates);
    } else if (coordinates.is_array()) {
        for (const InJson& part : coordinates) {
            parts.push_back(&part);
        }
    }
    return parts;
}

/// The geometry by its GeoJSON type alone -- a QuPath file, or anything else.
GeometryRead readGeometry(const InJson& geometry)
{
    const std::string type = stringMember(geometry, "type");
    const InJson& coordinates = coordinatesOf(geometry);

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
        read.multi = true;
    } else if (type == "LineString" || type == "MultiLineString") {
        read.multi = type == "MultiLineString";
        const std::vector<const InJson*> lines = partsOf(coordinates, read.multi);
        if (lines.empty()) {
            return skipped(GeometrySkip::Invalid);
        }
        for (const InJson* line : lines) {
            std::vector<PointF> points;
            if (!readLine(*line, points)) {
                return skipped(GeometrySkip::Invalid);
            }
            read.parts.emplace_back(PolygonGeometry{std::move(points), false});
        }
    } else if (type == "Polygon" || type == "MultiPolygon") {
        read.multi = type == "MultiPolygon";
        const std::vector<const InJson*> polygons = partsOf(coordinates, read.multi);
        if (polygons.empty()) {
            return skipped(GeometrySkip::Invalid);
        }
        // Every part is looked at, so a holed part is reported as holed even
        // after an invalid one.
        GeometrySkip worst = GeometrySkip::None;
        for (const InJson* polygon : polygons) {
            std::vector<PointF> ring;
            const GeometrySkip skip = readPolygonRings(*polygon, ring);
            if (skip == GeometrySkip::Holes) {
                worst = GeometrySkip::Holes;
            } else if (skip == GeometrySkip::Invalid && worst == GeometrySkip::None) {
                worst = GeometrySkip::Invalid;
            } else if (skip == GeometrySkip::None) {
                read.parts.push_back(closedAreaFrom(std::move(ring)));
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
    const InJson& coordinates = coordinatesOf(geometry);

    if (type == "Polygon" && (slideioType == "rectangle" || slideioType == "ellipse" || slideioType == "polygon")) {
        std::vector<PointF> ring;
        if (readPolygonRings(coordinates, ring) != GeometrySkip::None) {
            return std::nullopt;
        }
        if (slideioType == "polygon") {
            // Kept a polygon even when it happens to be an axis-aligned box.
            return AnnotationGeometry{PolygonGeometry{std::move(ring), true}};
        }
        if (slideioType == "rectangle" && !isBoxOutline(ring)) {
            return std::nullopt;
        }
        // The 512-gon's vertices include the box's four extremes, so its
        // bounds are the ellipse's box; a rectangle's are its corners.
        const auto [topLeft, bottomRight] = cornersOf(ring);
        if (slideioType == "rectangle") {
            return AnnotationGeometry{RectangleGeometry{topLeft, bottomRight}};
        }
        return AnnotationGeometry{EllipseGeometry{topLeft, bottomRight}};
    }
    if (type == "Point" && slideioType == "text") {
        PointF anchor;
        if (!readPosition(coordinates, anchor)) {
            return std::nullopt;
        }
        return AnnotationGeometry{TextGeometry{anchor}};
    }
    if (type == "LineString" && slideioType == "arrow") {
        std::vector<PointF> points;
        if (!readLine(coordinates, points) || points.size() != 2) {
            return std::nullopt;
        }
        // Tail first: the export's order is the arrow's direction.
        return AnnotationGeometry{ArrowGeometry{points[0], points[1]}};
    }
    // The tag is honoured only when the geometry fits it: a ruler is exactly
    // two positions and an angle exactly three; anything else stays the line
    // it is (the geometry wins).
    if (type == "LineString" && (slideioType == "ruler" || slideioType == "angle")) {
        std::vector<PointF> points;
        if (!readLine(coordinates, points)) {
            return std::nullopt;
        }
        if (slideioType == "ruler" && points.size() == 2) {
            return AnnotationGeometry{RulerGeometry{points[0], points[1]}};
        }
        if (slideioType == "angle" && points.size() == 3) {
            return AnnotationGeometry{AngleGeometry{points[0], points[1], points[2]}};
        }
        return std::nullopt;
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
    const InJson& slideio = slideioMember != nullptr && slideioMember->is_object() ? *slideioMember : emptyObject();

    outProperties = AnnotationProperties{};
    outProperties.label = stringMember(properties, "name");
    outProperties.classification = readClassification(member(properties, "classification"));
    if (isBlankOrUnclassified(outProperties.classification)) {
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

/// Separates a seed's fields; a unit separator cannot occur in a number and
/// is vanishingly unlikely in an id.
constexpr char kSeedSeparator = '\x1F';

/// A part's identity within its feature: the feature's id and the part's own
/// outline, each coordinate as the hexadecimal of its exact bit pattern. A seed
/// only has to be the same for the same coordinates, never read back, so this
/// needs no number formatting at all -- the floating-point std::to_chars is
/// unavailable below macOS 13.3, which the macOS builds target, and a stream
/// or printf reads the C locale.
std::string partSeed(const std::string& featureId, const AnnotationGeometry& part)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    constexpr char kHex[] = "0123456789abcdef";
    std::string seed = featureId;
    for (const PointF& point : outline(part).points) {
        for (const double value : {point.x, point.y}) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            seed += kSeedSeparator;
            for (int shift = 60; shift >= 0; shift -= 4) {
                seed += kHex[(bits >> shift) & 0xF];
            }
        }
    }
    return seed;
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

/// QuPath's nested objects, wherever its version put them, counted to the
/// bottom. With a work list rather than recursion, so no depth a file can
/// reach overflows the stack.
int countNested(const InJson& feature)
{
    int count = 0;
    std::vector<const InJson*> pending{&feature};
    while (!pending.empty()) {
        const InJson* object = pending.back();
        pending.pop_back();
        for (const InJson* holder : {object, member(*object, "properties")}) {
            const InJson* children = holder != nullptr ? member(*holder, "childObjects") : nullptr;
            if (children == nullptr || !children->is_array()) {
                continue;
            }
            for (const InJson& child : *children) {
                ++count;
                pending.push_back(&child);
            }
        }
    }
    return count;
}

const InJson& propertiesOf(const InJson& feature)
{
    const InJson* properties = member(feature, "properties");
    return properties != nullptr && properties->is_object() ? *properties : emptyObject();
}

/// The object type a feature is skipped for, or empty when it is imported.
/// No object type at all is a plain GeoJSON file from another tool: a set of
/// annotations to the person importing it.
std::string skippedObjectType(const InJson& feature)
{
    const InJson& properties = propertiesOf(feature);
    std::string objectType = stringMember(properties, "objectType");
    if (objectType.empty()) {
        objectType = stringMember(properties, "object_type");
    }
    return objectType == "annotation" ? std::string() : objectType;
}

/// Counts a top-level feature that is not an annotation -- read, skipped by
/// type, and its nested objects -- and says whether it was one. Shared by the
/// parse, which drops such features as they complete, and by readFeature, which
/// meets the rest; the counts cannot depend on which of the two saw it.
bool countIfSkipped(const InJson& feature, AnnotationImportResult& result)
{
    const std::string type = skippedObjectType(feature);
    if (type.empty()) {
        return false;
    }
    ++result.featuresRead;
    result.nestedObjects += countNested(feature);
    ++result.skippedObjectTypes[type];
    return true;
}

void readFeature(const InJson& feature, std::chrono::system_clock::time_point importedAt,
                 AnnotationImportResult& result)
{
    if (countIfSkipped(feature, result)) {
        return;
    }
    ++result.featuresRead;
    result.nestedObjects += countNested(feature);
    const InJson& properties = propertiesOf(feature);

    const InJson* geometryMember = member(feature, "geometry");
    const InJson& geometry = geometryMember != nullptr ? *geometryMember : absent();
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

    // Every part carries the feature's properties. A single geometry keeps the
    // feature's id. The parts of a Multi* geometry get none here and a seed
    // instead -- see AnnotationImportResult::idSeeds for why a part's identity is
    // where it is, not where it sits in the list.
    const std::string id = readId(feature);
    std::map<std::string, int> repeats;
    for (AnnotationGeometry& part : read.parts) {
        std::string seed;
        if (read.multi && !id.empty()) {
            seed = partSeed(id, part);
            // Two parts at the same place are still two findings.
            const int repeat = repeats[seed]++;
            seed += kSeedSeparator + std::to_string(repeat);
        }
        result.annotations.emplace_back(read.multi ? std::string() : id, std::move(part), annotationProperties,
                                        metadata);
        result.idSeeds.push_back(std::move(seed));
    }
    ++result.featuresImported;
}


/// Far deeper than GeoJSON needs -- a MultiPolygon's coordinates are four
/// arrays deep, and each level of a QuPath hierarchy adds three -- and far
/// shallower than the stack a recursive walk over the parsed value can take.
constexpr int kMaxJsonDepth = 256;

struct NestedTooDeeply
{
};

struct ReadCancelled
{
};


/// The parse callback. It does three things as the file streams past: stops
/// the read when asked to, refuses nesting no GeoJSON file needs, and drops
/// every top-level feature that is not an annotation the moment it is complete
/// -- after counting it into `dropped`. A QuPath detection export is otherwise
/// held whole in memory only to be thrown away: 60 000 cells peaked near a
/// gigabyte.
///
/// "Top-level" is an element of the root array, or of the root object's
/// `features`. A feature nested in another's `childObjects` is part of its
/// parent, counted through countNested, and must never be dropped on its own --
/// so the callback tracks the containers it is inside and the key each one
/// hangs from.
class ParseWatcher
{
public:
    ParseWatcher(const std::atomic<bool>* cancel, AnnotationImportResult& dropped)
        : m_cancel(cancel), m_dropped(dropped)
    {
    }

    bool operator()(int depth, InJson::parse_event_t event, InJson& parsed)
    {
        if (importCancelRequested(m_cancel)) {
            throw ReadCancelled{};
        }
        if (depth > kMaxJsonDepth) {
            throw NestedTooDeeply{};
        }
        switch (event) {
        case InJson::parse_event_t::key:
            if (!m_frames.empty() && parsed.is_string()) {
                m_frames.back().lastKey = parsed.get<std::string>();
            }
            return true;
        case InJson::parse_event_t::object_start:
        case InJson::parse_event_t::array_start: {
            const bool inObject = !m_frames.empty() && !m_frames.back().array;
            m_frames.push_back(Frame{event == InJson::parse_event_t::array_start,
                                     inObject ? m_frames.back().lastKey : std::string(), {}});
            return true;
        }
        case InJson::parse_event_t::array_end:
            if (!m_frames.empty()) {
                m_frames.pop_back();
            }
            return true;
        case InJson::parse_event_t::object_end:
            if (!m_frames.empty()) {
                m_frames.pop_back();
            }
            // False discards the finished object from its parent.
            return !(atFeatureList() && countIfSkipped(parsed, m_dropped));
        case InJson::parse_event_t::value:
            return true;
        }
        return true;
    }

private:
    struct Frame
    {
        bool array = false;
        /// The key this container hangs from in its parent object.
        std::string key;
        /// The most recent key read inside this container, if it is an object.
        std::string lastKey;
    };

    /// True when the innermost open container is the list of top-level
    /// features: the root array, or the root object's `features` array.
    bool atFeatureList() const
    {
        if (m_frames.empty() || !m_frames.back().array) {
            return false;
        }
        return m_frames.size() == 1
            || (m_frames.size() == 2 && !m_frames.front().array && m_frames.back().key == "features");
    }

    const std::atomic<bool>* m_cancel;
    AnnotationImportResult& m_dropped;
    std::vector<Frame> m_frames;
};

void addCounts(AnnotationImportResult& into, const AnnotationImportResult& from)
{
    into.featuresRead += from.featuresRead;
    into.nestedObjects += from.nestedObjects;
    for (const auto& [kind, count] : from.skippedObjectTypes) {
        into.skippedObjectTypes[kind] += count;
    }
}


} // namespace

AnnotationImportResult geoJsonToAnnotations(std::string_view text, std::chrono::system_clock::time_point importedAt,
                                         const std::atomic<bool>* cancel)
{
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (text.substr(0, kBom.size()) == kBom) {
        text.remove_prefix(kBom.size());
    }
    // No exceptions on the way in: a file that is not JSON, or not UTF-8, is a
    // file we report, not one we crash on. The depth is capped as it is
    // parsed, which bounds every walk over the result at once -- nlohmann's
    // copy and comparison recurse, and a file nested twenty thousand deep
    // overflowed the stack in one of them.
    AnnotationImportResult dropped;
    InJson document;
    try {
        ParseWatcher watcher(cancel, dropped);
        document = InJson::parse(
            text.begin(), text.end(),
            [&watcher](int depth, InJson::parse_event_t event, InJson& parsed) {
                return watcher(depth, event, parsed);
            },
            false);
    } catch (const NestedTooDeeply&) {
        return importFailure("the file is nested more deeply than any GeoJSON file needs");
    } catch (const ReadCancelled&) {
        return cancelledImport();
    }
    if (document.is_discarded()) {
        return importFailure("the file is not valid JSON");
    }

    try {
        AnnotationImportResult result;
        addCounts(result, dropped);
        const std::string type = stringMember(document, "type");
        const InJson* features = nullptr;
        if (document.is_array()) {
            features = &document;
        } else if (type == "Feature") {
            readFeature(document, importedAt, result);
            return result;
        } else if (type == "FeatureCollection") {
            const InJson* listed = member(document, "features");
            if (listed == nullptr || !listed->is_array()) {
                return importFailure("the FeatureCollection has no list of features");
            }
            features = listed;
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

        for (const InJson& feature : *features) {
            if (importCancelRequested(cancel)) {
                return cancelledImport();
            }
            readFeature(feature, importedAt, result);
        }
        return result;
    } catch (const nlohmann::json::exception& e) {
        // A backstop: every field is type-checked before it is read.
        return importFailure(std::string("the file could not be read: ") + e.what());
    }
}

} // namespace slideio::viewer::core
