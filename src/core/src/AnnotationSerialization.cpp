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
#include <utility>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

// ordered_json, not json: the default sorts keys alphabetically, which buries
// slideId and schemaVersion below the whole annotations array. This format
// exists to be read by a human with a text editor.
using json = nlohmann::ordered_json;

constexpr const char* kRectangleTypeName = "rectangle";

bool isFinitePoint(const PointF& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool isRepresentable(const AnnotationGeometry& geometry)
{
    static_assert(std::variant_size_v<AnnotationGeometry> == 1,
                  "A new geometry alternative needs its own finiteness check and its own "
                  "JSON shape below; silently writing nulls corrupts the file.");
    const auto& rectangle = std::get<RectangleGeometry>(geometry);
    return isFinitePoint(rectangle.topLeft) && isFinitePoint(rectangle.bottomRight);
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

json geometryToJson(const AnnotationGeometry& geometry)
{
    static_assert(std::variant_size_v<AnnotationGeometry> == 1,
                  "A new geometry alternative needs its own JSON shape here.");
    const auto& rectangle = std::get<RectangleGeometry>(geometry);
    return json{
        {"type", kRectangleTypeName},
        {"topLeft", pointToJson(rectangle.topLeft)},
        {"bottomRight", pointToJson(rectangle.bottomRight)},
    };
}

const char* typeName(AnnotationType type)
{
    static_assert(static_cast<int>(AnnotationType::Rectangle) == 0,
                  "A new AnnotationType needs its own name here.");
    (void)type;
    return kRectangleTypeName;
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
        {"type", typeName(annotation.type())},
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

bool readGeometry(const json& value, AnnotationGeometry& out, std::string& problem)
{
    static_assert(std::variant_size_v<AnnotationGeometry> == 1,
                  "A new geometry alternative needs its own branch here.");

    if (!value.is_object() || !value.contains("type") || !value.at("type").is_string()) {
        problem = "geometry has no type";
        return false;
    }

    const std::string type = value.at("type").get<std::string>();
    if (type != kRectangleTypeName) {
        problem = "unknown geometry type '" + type + "'";
        return false;
    }

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

    // The type is derived from the geometry rather than read: the two are
    // independent in the constructor and a file must not be able to make them
    // disagree.
    out = Annotation(value.at("id").get<std::string>(), AnnotationType::Rectangle,
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
        Annotation annotation("", AnnotationType::Rectangle,
                              RectangleGeometry{PointF{}, PointF{}});
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

std::string serializeAnnotationDocument(const AnnotationDocument& document)
{
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
