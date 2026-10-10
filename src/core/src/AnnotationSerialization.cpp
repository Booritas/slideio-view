#include "slideio/viewer/core/AnnotationSerialization.h"

#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

using nlohmann::json;

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

bool isRepresentable(const AnnotationProperties& properties)
{
    // Same reason as the geometry check: JSON has no NaN or infinity, nlohmann
    // writes null, and the file will not parse back.
    return std::isfinite(properties.lineWidth) && std::isfinite(properties.fillOpacity);
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

} // namespace slideio::viewer::core
