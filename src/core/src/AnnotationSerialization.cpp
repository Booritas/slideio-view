#include "slideio/viewer/core/AnnotationSerialization.h"

#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace slideio::viewer::core
{

namespace
{

// ordered_json, not json: the default sorts keys alphabetically, which buries
// slideId and schemaVersion below the whole annotations array. This format
// exists to be read by a human with a text editor.
using json = nlohmann::ordered_json;

constexpr const char* kRectangleTypeName = "rectangle";
constexpr const char* kEllipseTypeName = "ellipse";
constexpr const char* kPolygonTypeName = "polygon";
constexpr const char* kPolylineTypeName = "polyline";
constexpr const char* kPointTypeName = "point";
constexpr const char* kArrowTypeName = "arrow";

bool isFinitePoint(const PointF& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool isRepresentableShape(const RectangleGeometry& rectangle)
{
    return isFinitePoint(rectangle.topLeft) && isFinitePoint(rectangle.bottomRight);
}

// Finite corners and nothing more, exactly as for a rectangle. No minimum
// extent, because readEllipse accepts a degenerate one (design spec 2.2) and
// the two sides must agree or this application writes a file it cannot open.
bool isRepresentableShape(const EllipseGeometry& ellipse)
{
    return isFinitePoint(ellipse.topLeft) && isFinitePoint(ellipse.bottomRight);
}

// Finite coordinates and nothing more. There is no minimum extent to enforce:
// any finite position is a valid marker, and any finite pair is a valid arrow
// -- including a zero-length one, which readArrow accepts, and the two sides
// must agree or this application writes a file it cannot open.
bool isRepresentableShape(const PointGeometry& marker)
{
    return isFinitePoint(marker.position);
}

bool isRepresentableShape(const ArrowGeometry& arrow)
{
    return isFinitePoint(arrow.tail) && isFinitePoint(arrow.head);
}

bool isRepresentableShape(const PolygonGeometry& poly)
{
    // The same minimums the parser enforces. Refusing to write a shape we
    // would refuse to read is what keeps every file this application produces
    // loadable by it.
    const std::size_t minimum = poly.closed ? kMinPolygonVertices : kMinPolylineVertices;
    if (poly.vertices.size() < minimum) {
        return false;
    }
    for (const PointF& vertex : poly.vertices) {
        if (!isFinitePoint(vertex)) {
            return false;
        }
    }
    return true;
}

bool isRepresentable(const AnnotationGeometry& geometry)
{
    return std::visit([](const auto& shape) { return isRepresentableShape(shape); }, geometry);
}

// The domains of the two float properties, shared by the writer and the reader.
// They live together because a value one side accepts and the other rejects
// means this application wrote a file it cannot open.
constexpr double kMinLineWidth = 0.01;
constexpr double kMaxLineWidth = 1000.0;
constexpr double kMinFillOpacity = 0.0;
constexpr double kMaxFillOpacity = 1.0;

bool inClosedRange(double value, double min, double max)
{
    return std::isfinite(value) && value >= min && value <= max;
}

bool isRepresentable(const AnnotationProperties& properties)
{
    // The same bounds the parser enforces: refusing to write a value we would
    // refuse to read keeps every file this application produces loadable by it.
    return inClosedRange(properties.lineWidth, kMinLineWidth, kMaxLineWidth)
        && inClosedRange(properties.fillOpacity, kMinFillOpacity, kMaxFillOpacity);
}

std::string formatColor(const Color& color)
{
    std::array<char, 16> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "#%02X%02X%02X%02X",
                  color.r, color.g, color.b, color.a);
    return std::string(buffer.data());
}

json pointToJson(const PointF& point)
{
    return json{{"x", point.x}, {"y", point.y}};
}

json shapeToJson(const RectangleGeometry& rectangle)
{
    return json{
        {"type", kRectangleTypeName},
        {"topLeft", pointToJson(rectangle.topLeft)},
        {"bottomRight", pointToJson(rectangle.bottomRight)},
    };
}

json shapeToJson(const EllipseGeometry& ellipse)
{
    return json{
        {"type", kEllipseTypeName},
        {"topLeft", pointToJson(ellipse.topLeft)},
        {"bottomRight", pointToJson(ellipse.bottomRight)},
    };
}

json shapeToJson(const PointGeometry& marker)
{
    return json{
        {"type", kPointTypeName},
        {"position", pointToJson(marker.position)},
    };
}

// tail and head, not topLeft and bottomRight: the names say the pair is
// ordered, so a human reading the file can see which end carries the head.
json shapeToJson(const ArrowGeometry& arrow)
{
    return json{
        {"type", kArrowTypeName},
        {"tail", pointToJson(arrow.tail)},
        {"head", pointToJson(arrow.head)},
    };
}

/// The geometry "type" string for a polygon-shaped path. Shared by the geometry
/// object and the annotation-level field, so the closed/open choice is made once.
const char* polygonTypeName(const PolygonGeometry& poly)
{
    return poly.closed ? kPolygonTypeName : kPolylineTypeName;
}

json shapeToJson(const PolygonGeometry& poly)
{
    json vertices = json::array();
    for (const PointF& vertex : poly.vertices) {
        vertices.push_back(pointToJson(vertex));
    }
    return json{
        {"type", polygonTypeName(poly)},
        {"vertices", std::move(vertices)},
    };
}

json geometryToJson(const AnnotationGeometry& geometry)
{
    return std::visit([](const auto& shape) { return shapeToJson(shape); }, geometry);
}

/// The annotation-level "type" string. Derived from the geometry, like
/// Annotation::type() itself, so the two fields of a written file cannot
/// disagree. On read the field is ignored and the geometry wins.
const char* geometryTypeName(const AnnotationGeometry& geometry)
{
    return std::visit([](const auto& shape) -> const char* {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, RectangleGeometry>) {
            return kRectangleTypeName;
        } else if constexpr (std::is_same_v<Shape, EllipseGeometry>) {
            return kEllipseTypeName;
        } else if constexpr (std::is_same_v<Shape, PolygonGeometry>) {
            return polygonTypeName(shape);
        } else if constexpr (std::is_same_v<Shape, PointGeometry>) {
            return kPointTypeName;
        } else if constexpr (std::is_same_v<Shape, ArrowGeometry>) {
            return kArrowTypeName;
        } else {
            static_assert(sizeof(Shape) == 0, "A new geometry alternative needs its own type name here.");
        }
    }, geometry);
}

/// Formats `time` into `out`; false when formatIso8601Utc cannot represent it.
bool formatTimestamp(std::chrono::system_clock::time_point time, std::string& out)
{
    out = formatIso8601Utc(time);
    return !out.empty();
}

/// Builds the JSON for one annotation; false when a timestamp cannot be formatted.
bool annotationToJson(const Annotation& annotation, json& out)
{
    const AnnotationProperties& properties = annotation.properties();
    const AnnotationMetadata& metadata = annotation.metadata();

    std::string createdAt;
    std::string modifiedAt;
    if (!formatTimestamp(metadata.createdAt, createdAt) || !formatTimestamp(metadata.modifiedAt, modifiedAt)) {
        return false;
    }

    out = json{
        {"id", annotation.id()},
        {"type", geometryTypeName(annotation.geometry())},
        {"geometry", geometryToJson(annotation.geometry())},
        {"properties", json{
            {"label", properties.label},
            {"classification", properties.classification},
            {"color", formatColor(properties.color)},
            {"lineWidth", properties.lineWidth},
            {"fillOpacity", properties.fillOpacity},
            {"notes", properties.notes},
        }},
        {"metadata", json{
            {"author", metadata.author},
            {"createdAt", createdAt},
            {"modifiedAt", modifiedAt},
        }},
    };
    return true;
}

ParseResult failure(ParseError error, std::string message)
{
    ParseResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

/// One hex digit; -1 for anything else. sscanf's %x would also accept signs,
/// spaces and a 0x prefix inside a field, so it is not used here.
int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseColor(const std::string& text, Color& out)
{
    if (text.size() != 9 || text[0] != '#') {
        return false;
    }
    std::array<uint8_t, 4> channels{};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const int high = hexDigit(text[1 + 2 * i]);
        const int low = hexDigit(text[2 + 2 * i]);
        if (high < 0 || low < 0) {
            return false;
        }
        channels[i] = static_cast<uint8_t>(high * 16 + low);
    }
    out = Color{channels[0], channels[1], channels[2], channels[3]};
    return true;
}

/// nlohmann's is_number_float() is false for an integer literal, and a
/// coordinate written as `0` is perfectly legal JSON, so both are accepted.
bool readDouble(const json& value, double& out)
{
    if (!value.is_number()) {
        return false;
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool readPoint(const json& value, PointF& out)
{
    if (!value.is_object()) {
        return false;
    }
    return value.contains("x") && value.contains("y")
        && readDouble(value.at("x"), out.x) && readDouble(value.at("y"), out.y);
}

std::string readString(const json& parent, const char* key, const std::string& fallback)
{
    if (!parent.contains(key) || !parent.at(key).is_string()) {
        return fallback;
    }
    return parent.at(key).get<std::string>();
}

/// Reads an integer that must fit in an int. nlohmann's get<int>() casts
/// without checking, so 4294967297 silently becomes 1 -- which would walk
/// straight through the schema-version gate.
bool readBoundedInt(const json& value, int& out)
{
    if (!value.is_number_integer()) {
        return false;
    }
    const int64_t wide = value.get<int64_t>();
    if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max()) {
        return false;
    }
    out = static_cast<int>(wide);
    return true;
}

/// `min` and `max` bound the value in the double domain, before the narrowing
/// to float -- 1e300 is a finite double and an infinite float.
bool readBoundedFloat(const json& parent, const char* key, double min, double max, float& out)
{
    if (!parent.contains(key)) {
        return true;        // absent keeps the caller's default
    }
    const json& value = parent.at(key);
    if (!value.is_number()) {
        return false;
    }
    const double wide = value.get<double>();
    if (!std::isfinite(wide) || wide < min || wide > max) {
        return false;
    }
    out = static_cast<float>(wide);
    return true;
}

/// Absent is fine (the field is optional); present but unparseable is not,
/// because it means the file says something we cannot honour.
bool readTimestamp(const json& parent, const char* key,
                   std::chrono::system_clock::time_point& out, std::string& problem)
{
    if (!parent.contains(key)) {
        return true;
    }
    const json& value = parent.at(key);
    if (!value.is_string() || !parseIso8601Utc(value.get<std::string>(), out)) {
        problem = std::string(key) + " is not an ISO 8601 UTC timestamp";
        return false;
    }
    return true;
}

bool readRectangle(const json& value, AnnotationGeometry& out, std::string& problem)
{
    RectangleGeometry rectangle;
    if (!value.contains("topLeft") || !readPoint(value.at("topLeft"), rectangle.topLeft)) {
        problem = "geometry.topLeft is not a finite point";
        return false;
    }
    if (!value.contains("bottomRight") || !readPoint(value.at("bottomRight"), rectangle.bottomRight)) {
        problem = "geometry.bottomRight is not a finite point";
        return false;
    }
    out = rectangle;
    return true;
}

bool readEllipse(const json& value, AnnotationGeometry& out, std::string& problem)
{
    EllipseGeometry ellipse;
    if (!value.contains("topLeft") || !readPoint(value.at("topLeft"), ellipse.topLeft)) {
        problem = "geometry.topLeft is not a finite point";
        return false;
    }
    if (!value.contains("bottomRight") || !readPoint(value.at("bottomRight"), ellipse.bottomRight)) {
        problem = "geometry.bottomRight is not a finite point";
        return false;
    }
    // A zero-extent ellipse is accepted, exactly as a degenerate rectangle is
    // (design spec 2.2): refusing it would make the whole document unloadable,
    // and by the governing rule never again writable, over one shape the user
    // can see and fix.
    out = ellipse;
    return true;
}

bool readPoint(const json& value, AnnotationGeometry& out, std::string& problem)
{
    PointGeometry marker;
    if (!value.contains("position") || !readPoint(value.at("position"), marker.position)) {
        problem = "geometry.position is not a finite point";
        return false;
    }
    out = marker;
    return true;
}

bool readArrow(const json& value, AnnotationGeometry& out, std::string& problem)
{
    ArrowGeometry arrow;
    if (!value.contains("tail") || !readPoint(value.at("tail"), arrow.tail)) {
        problem = "geometry.tail is not a finite point";
        return false;
    }
    if (!value.contains("head") || !readPoint(value.at("head"), arrow.head)) {
        problem = "geometry.head is not a finite point";
        return false;
    }
    // A zero-length arrow is accepted, exactly as a degenerate ellipse is:
    // refusing it would make the whole document unloadable, and by the
    // governing rule never again writable, over one shape the user can see and
    // delete. The endpoints are NOT normalised -- the order is the direction.
    out = arrow;
    return true;
}

bool readPolygon(const json& value, bool closed, AnnotationGeometry& out, std::string& problem)
{
    if (!value.contains("vertices") || !value.at("vertices").is_array()) {
        problem = "geometry.vertices is not an array";
        return false;
    }

    PolygonGeometry poly;
    poly.closed = closed;
    poly.vertices.reserve(value.at("vertices").size());
    for (const json& entry : value.at("vertices")) {
        PointF vertex;
        if (!readPoint(entry, vertex)) {
            problem = "geometry.vertices contains something that is not a finite point";
            return false;
        }
        poly.vertices.push_back(vertex);
    }

    // No maximum. A dense freehand trace is legitimate data, and a cap here
    // would reject a clinician's outline for being careful; density is the
    // drawing tool's business. By the time this runs the array has already
    // been parsed and allocated, so a cap would not be a defence anyway.
    const std::size_t minimum = closed ? kMinPolygonVertices : kMinPolylineVertices;
    if (poly.vertices.size() < minimum) {
        problem = "geometry.vertices has fewer than " + std::to_string(minimum) + " points";
        return false;
    }

    out = std::move(poly);
    return true;
}

bool readGeometry(const json& value, AnnotationGeometry& out, std::string& problem)
{
    if (!value.is_object() || !value.contains("type") || !value.at("type").is_string()) {
        problem = "geometry has no type";
        return false;
    }

    const std::string type = value.at("type").get<std::string>();
    if (type == kRectangleTypeName) {
        return readRectangle(value, out, problem);
    }
    if (type == kEllipseTypeName) {
        return readEllipse(value, out, problem);
    }
    if (type == kPolygonTypeName) {
        return readPolygon(value, true, out, problem);
    }
    if (type == kPolylineTypeName) {
        return readPolygon(value, false, out, problem);
    }
    if (type == kPointTypeName) {
        return readPoint(value, out, problem);
    }
    if (type == kArrowTypeName) {
        return readArrow(value, out, problem);
    }

    // Naming the version is the whole value of this message: schemaVersion
    // stays at 1 across geometry additions on purpose (design spec 3.2), so an
    // older build meeting a newer shape fails here rather than at the version
    // check, and a user needs to be told which it was.
    problem = "unknown geometry type '" + type
            + "' -- this file may have been written by a newer version of the viewer";
    return false;
}

bool readAnnotation(const json& value, Annotation& out, std::string& problem)
{
    if (!value.is_object()) {
        problem = "an entry of annotations is not an object";
        return false;
    }
    if (!value.contains("id") || !value.at("id").is_string()
        || value.at("id").get<std::string>().empty()) {
        problem = "an annotation has no id";
        return false;
    }

    AnnotationGeometry geometry;
    if (!value.contains("geometry") || !readGeometry(value.at("geometry"), geometry, problem)) {
        if (problem.empty()) {
            problem = "an annotation has no geometry";
        }
        return false;
    }

    AnnotationProperties properties;
    if (value.contains("properties") && value.at("properties").is_object()) {
        const json& source = value.at("properties");
        properties.label = readString(source, "label", properties.label);
        properties.classification = readString(source, "classification", properties.classification);
        properties.notes = readString(source, "notes", properties.notes);
        if (!readBoundedFloat(source, "lineWidth", kMinLineWidth, kMaxLineWidth, properties.lineWidth)) {
            problem = "properties.lineWidth is not a number in 0.01..1000";
            return false;
        }
        if (!readBoundedFloat(source, "fillOpacity", kMinFillOpacity, kMaxFillOpacity, properties.fillOpacity)) {
            problem = "properties.fillOpacity is not a number in 0..1";
            return false;
        }
        if (source.contains("color")) {
            if (!source.at("color").is_string()
                || !parseColor(source.at("color").get<std::string>(), properties.color)) {
                problem = "properties.color is not #RRGGBBAA";
                return false;
            }
        }
    }

    AnnotationMetadata metadata;
    if (value.contains("metadata") && value.at("metadata").is_object()) {
        const json& source = value.at("metadata");
        metadata.author = readString(source, "author", metadata.author);
        if (!readTimestamp(source, "createdAt", metadata.createdAt, problem)
            || !readTimestamp(source, "modifiedAt", metadata.modifiedAt, problem)) {
            problem = "metadata." + problem;
            return false;
        }
    }

    // The type is derived from the geometry rather than read: a file must not
    // be able to make them disagree.
    out = Annotation(value.at("id").get<std::string>(),
                     std::move(geometry), std::move(properties), std::move(metadata));
    return true;
}

ParseResult parseDocument(std::string_view text)
{
    json root = json::parse(text, nullptr, false /*allow_exceptions*/);
    if (root.is_discarded()) {
        return failure(ParseError::MalformedJson, "the file is not valid JSON");
    }
    if (!root.is_object()) {
        return failure(ParseError::NotAnAnnotationDocument, "the root of the file is not an object");
    }
    if (!root.contains("schemaVersion") || !root.at("schemaVersion").is_number_integer()) {
        return failure(ParseError::NotAnAnnotationDocument,
                       "the file has no numeric schemaVersion, so it is not an annotation file");
    }

    int version = 0;
    if (!readBoundedInt(root.at("schemaVersion"), version) || version < 1) {
        return failure(ParseError::InvalidField, "schemaVersion is not a positive integer");
    }
    if (version > kCurrentSchemaVersion) {
        return failure(ParseError::UnsupportedFutureVersion,
                       "the file uses schema version " + std::to_string(version)
                           + ", which is newer than this version of SlideIO Viewer understands ("
                           + std::to_string(kCurrentSchemaVersion) + ")");
    }

    AnnotationDocument document;
    document.schemaVersion = version;
    if (!root.contains("slideId") || !root.at("slideId").is_string()
        || root.at("slideId").get<std::string>().empty()) {
        return failure(ParseError::InvalidField, "slideId is missing or not a non-empty string");
    }
    document.slideId = root.at("slideId").get<std::string>();
    if (root.contains("sceneIndex")) {
        if (!readBoundedInt(root.at("sceneIndex"), document.sceneIndex) || document.sceneIndex < 0) {
            return failure(ParseError::InvalidField, "sceneIndex is not a non-negative integer");
        }
    }

    if (root.contains("slide") && root.at("slide").is_object()) {
        const json& slide = root.at("slide");
        document.slide.fileName = readString(slide, "fileName", {});
        document.slide.path = readString(slide, "path", {});
        if (slide.contains("width") && slide.at("width").is_number_integer()) {
            document.slide.width = slide.at("width").get<int64_t>();
        }
        if (slide.contains("height") && slide.at("height").is_number_integer()) {
            document.slide.height = slide.at("height").get<int64_t>();
        }
    }

    std::string problem;
    if (!readTimestamp(root, "createdAt", document.createdAt, problem)
        || !readTimestamp(root, "modifiedAt", document.modifiedAt, problem)) {
        return failure(ParseError::InvalidField, problem);
    }

    if (!root.contains("annotations")) {
        return failure(ParseError::InvalidField, "the file has no annotations array");
    }
    if (!root.at("annotations").is_array()) {
        return failure(ParseError::InvalidField, "annotations is not an array");
    }

    // Ids are identity. Two annotations sharing one makes hitTest and
    // setGeometry disagree about which shape the user is pointing at, so the
    // honest answer is that the document is malformed.
    std::set<std::string> seenIds;
    for (const json& entry : root.at("annotations")) {
        Annotation annotation("", RectangleGeometry{PointF{}, PointF{}});
        if (!readAnnotation(entry, annotation, problem)) {
            return failure(ParseError::InvalidField, problem);
        }
        if (!seenIds.insert(annotation.id()).second) {
            return failure(ParseError::InvalidField,
                           "two annotations share the id '" + annotation.id() + "'");
        }
        document.annotations.push_back(std::move(annotation));
    }

    ParseResult result;
    result.document = std::move(document);
    return result;
}

} // namespace

const char* annotationTypeName(const AnnotationGeometry& geometry)
{
    return geometryTypeName(geometry);
}

std::string formatAnnotationColor(const Color& color)
{
    return formatColor(color);
}

std::string serializeAnnotationDocument(const AnnotationDocument& document)
{
    // The parser rejects an empty id and a duplicate id. Same rule as the
    // property bounds: never write a file we cannot read back.
    std::set<std::string> ids;
    for (const Annotation& annotation : document.annotations) {
        if (annotation.id().empty() || !ids.insert(annotation.id()).second) {
            return {};
        }
    }

    for (const Annotation& annotation : document.annotations) {
        if (!isRepresentable(annotation.geometry()) || !isRepresentable(annotation.properties())) {
            return {};
        }
    }

    // Same rule as the finiteness check above: a timestamp formatIso8601Utc
    // cannot represent comes back empty, and writing `"createdAt": ""` would
    // produce a file that will not parse back -- silently destroying the
    // user's only copy. Refuse the write instead.
    std::string createdAt;
    std::string modifiedAt;
    if (!formatTimestamp(document.createdAt, createdAt) || !formatTimestamp(document.modifiedAt, modifiedAt)) {
        return {};
    }

    json annotations = json::array();
    for (const Annotation& annotation : document.annotations) {
        json annotationJson;
        if (!annotationToJson(annotation, annotationJson)) {
            return {};
        }
        annotations.push_back(std::move(annotationJson));
    }

    json root{
        {"schemaVersion", document.schemaVersion},
        {"slideId", document.slideId},
        {"sceneIndex", document.sceneIndex},
        {"slide", json{
            {"fileName", document.slide.fileName},
            {"path", document.slide.path},
            {"width", document.slide.width},
            {"height", document.slide.height},
        }},
        {"createdAt", createdAt},
        {"modifiedAt", modifiedAt},
        {"annotations", std::move(annotations)},
    };

    // nlohmann throws on invalid UTF-8 in any string. Turning that into the
    // same empty-string refusal as the other failures keeps the promise that no
    // exception leaves this function -- Task 5's save path has no catch.
    try {
        return root.dump(2) + "\n";
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

ParseResult parseAnnotationDocument(std::string_view text)
{
    // json::parse is told not to throw, but get<>() and allocation still can.
    // A throw here would reach Task 5's load path, which has no catch.
    try {
        return parseDocument(text);
    } catch (const std::exception& e) {
        return failure(ParseError::InvalidField, std::string("the file could not be read: ") + e.what());
    }
}

} // namespace slideio::viewer::core
