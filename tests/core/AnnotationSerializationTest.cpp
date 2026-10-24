#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <variant>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

std::chrono::system_clock::time_point at(long long epochSeconds)
{
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(epochSeconds));
}

Annotation makeAnnotation()
{
    AnnotationProperties properties;
    properties.label = "Tumour focus";
    properties.classification = "Carcinoma";
    properties.color = Color{0x1F, 0x77, 0xB4, 0x80};
    properties.lineWidth = 3.5f;
    properties.fillOpacity = 0.25f;
    properties.notes = "Checked under 40x";

    AnnotationMetadata metadata;
    metadata.author = "jdoe";
    metadata.createdAt = at(1772683629LL);
    metadata.modifiedAt = at(1772683750LL);

    return Annotation("4f1c8e2a-91b7-4d3e-8c15-2a9f7e6b0d44",
                      RectangleGeometry{PointF{1200.0, 800.0}, PointF{4400.0, 2600.0}},
                      properties, metadata);
}

AnnotationDocument makeDocument()
{
    AnnotationDocument document;
    document.slideId = "a3f8c2e1b4d50697";
    document.sceneIndex = 0;
    document.slide.fileName = "case001_HE.svs";
    document.slide.path = "D:/data/pathology/case001_HE.svs";
    document.slide.width = 102400;
    document.slide.height = 76800;
    document.createdAt = at(1772683600LL);
    document.modifiedAt = at(1772683700LL);
    document.annotations.push_back(makeAnnotation());
    return document;
}

/// A valid one-annotation document whose geometry is `geometryJson` verbatim.
/// Mirrors the inline raw strings the rest of this file uses, which is how a
/// hand-edited file reaches the parser.
std::string documentWithGeometry(const std::string& geometryJson)
{
    return std::string(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
          {"id": "a1", "geometry": )") + geometryJson + R"(}
        ]})";
}

} // namespace

TEST_CASE("an empty document serializes with the current schema version", "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "a3f8c2e1b4d50697";
    const std::string json = serializeAnnotationDocument(document);

    REQUIRE_FALSE(json.empty());
    REQUIRE(json.find("\"schemaVersion\": 1") != std::string::npos);
    REQUIRE(json.find("\"annotations\": []") != std::string::npos);
}

TEST_CASE("the document header is written verbatim", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());

    REQUIRE(json.find("\"slideId\": \"a3f8c2e1b4d50697\"") != std::string::npos);
    REQUIRE(json.find("\"sceneIndex\": 0") != std::string::npos);
    REQUIRE(json.find("\"fileName\": \"case001_HE.svs\"") != std::string::npos);
    REQUIRE(json.find("\"width\": 102400") != std::string::npos);
    REQUIRE(json.find("\"createdAt\": \"2026-03-05T04:06:40Z\"") != std::string::npos);
}

TEST_CASE("a rectangle writes its own corners, not a normalised box", "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    // Deliberately inverted: the serializer records what was drawn. Normalising
    // here would make a round trip lossy for a drag that went up and left.
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{400.0, 300.0}, PointF{100.0, 50.0}});

    const std::string json = serializeAnnotationDocument(document);
    const std::size_t topLeft = json.find("\"topLeft\"");
    const std::size_t bottomRight = json.find("\"bottomRight\"");
    REQUIRE(topLeft != std::string::npos);
    REQUIRE(bottomRight != std::string::npos);

    // nlohmann orders object keys alphabetically, so each corner's section runs
    // from its key to the other key (or the end). Compare within the section:
    // 400.0 belongs to topLeft here. Had the serializer normalised, topLeft
    // would hold 100.0 and this would fail.
    const std::size_t topLeftEnd = topLeft < bottomRight ? bottomRight : json.size();
    const std::size_t bottomRightEnd = bottomRight < topLeft ? topLeft : json.size();

    const std::size_t topLeftValue = json.find("400.0", topLeft);
    REQUIRE(topLeftValue != std::string::npos);
    REQUIRE(topLeftValue < topLeftEnd);

    const std::size_t bottomRightValue = json.find("100.0", bottomRight);
    REQUIRE(bottomRightValue != std::string::npos);
    REQUIRE(bottomRightValue < bottomRightEnd);
}

TEST_CASE("a non-finite line width or fill opacity refuses to serialize",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    SECTION("line width")
    {
        AnnotationProperties properties = document.annotations.front().properties();
        properties.lineWidth = std::numeric_limits<float>::quiet_NaN();
        document.annotations.front().setProperties(properties);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("fill opacity")
    {
        AnnotationProperties properties = document.annotations.front().properties();
        properties.fillOpacity = std::numeric_limits<float>::infinity();
        document.annotations.front().setProperties(properties);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }
}

TEST_CASE("invalid UTF-8 in a string refuses to serialize rather than throwing",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    AnnotationProperties properties = document.annotations.front().properties();
    properties.label = std::string("bad\xC3");   // truncated two-byte sequence
    document.annotations.front().setProperties(properties);

    REQUIRE_NOTHROW(serializeAnnotationDocument(document));
    REQUIRE(serializeAnnotationDocument(document).empty());
}

TEST_CASE("the document is written in the documented field order",
          "[core][AnnotationSerialization]")
{
    // The format is meant to be opened in a text editor, so the slide's
    // identity has to come before the annotation array rather than after it.
    const std::string json = serializeAnnotationDocument(makeDocument());

    const std::size_t schemaVersion = json.find("\"schemaVersion\"");
    const std::size_t slideId = json.find("\"slideId\"");
    const std::size_t sceneIndex = json.find("\"sceneIndex\"");
    const std::size_t slide = json.find("\"slide\":");      // with the colon:
    const std::size_t annotations = json.find("\"annotations\"");

    REQUIRE(schemaVersion != std::string::npos);
    REQUIRE(annotations != std::string::npos);
    REQUIRE(schemaVersion < slideId);
    REQUIRE(slideId < sceneIndex);
    REQUIRE(sceneIndex < slide);
    REQUIRE(slide < annotations);
}

TEST_CASE("a colour is written as #RRGGBBAA in upper case", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());
    REQUIRE(json.find("\"color\": \"#1F77B480\"") != std::string::npos);
}

TEST_CASE("the default annotation colour survives verbatim", "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"color\": \"#E67E22FF\"") != std::string::npos);
}

TEST_CASE("the serializer refuses ids the parser would reject",
          "[core][AnnotationSerialization]")
{
    // Same invariant as the property bounds: never write what we cannot read.
    AnnotationDocument document;
    document.slideId = "a3f8c2e1b4d50697";

    SECTION("an empty id")
    {
        document.annotations.emplace_back("", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("two annotations sharing an id")
    {
        document.annotations.emplace_back("same", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});
        document.annotations.emplace_back("same", RectangleGeometry{PointF{5.0, 5.0}, PointF{6.0, 6.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }
}

TEST_CASE("a non-finite coordinate refuses to serialize", "[core][AnnotationSerialization]")
{
    // JSON cannot carry NaN or infinity. nlohmann emits null, which produces a
    // file that will not parse back -- so the write is refused instead.
    AnnotationDocument document;
    document.slideId = "id";

    SECTION("NaN")
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{nan, 1.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("infinity")
    {
        const double inf = std::numeric_limits<double>::infinity();
        document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, inf}, PointF{1.0, 1.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }
}

TEST_CASE("a timestamp that cannot be formatted refuses to serialize", "[core][AnnotationSerialization]")
{
    // formatIso8601Utc returns "" for instants before 1970 or past year 9999.
    // Writing that as "createdAt": "" would produce a file that will not parse
    // back, so the write is refused instead.
    const auto beforeEpoch = at(-1LL);

    SECTION("document createdAt")
    {
        AnnotationDocument document = makeDocument();
        document.createdAt = beforeEpoch;
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("document modifiedAt")
    {
        AnnotationDocument document = makeDocument();
        document.modifiedAt = beforeEpoch;
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("annotation createdAt")
    {
        AnnotationDocument document;
        document.slideId = "id";
        AnnotationMetadata metadata;
        metadata.createdAt = beforeEpoch;
        document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
                                          AnnotationProperties{}, metadata);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("annotation modifiedAt")
    {
        AnnotationDocument document;
        document.slideId = "id";
        AnnotationMetadata metadata;
        metadata.modifiedAt = beforeEpoch;
        document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
                                          AnnotationProperties{}, metadata);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }
}

TEST_CASE("a double needing full precision is written at full precision",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    const double awkward = 0.1 + 0.2;   // 0.30000000000000004
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{awkward, 0.0}, PointF{1.0, 1.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("0.30000000000000004") != std::string::npos);
}

TEST_CASE("annotation metadata is written, not regenerated", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());
    REQUIRE(json.find("\"author\": \"jdoe\"") != std::string::npos);
    REQUIRE(json.find("\"createdAt\": \"2026-03-05T04:07:09Z\"") != std::string::npos);
    REQUIRE(json.find("\"modifiedAt\": \"2026-03-05T04:09:10Z\"") != std::string::npos);
}

TEST_CASE("a document round-trips through JSON unchanged", "[core][AnnotationSerialization]")
{
    const AnnotationDocument original = makeDocument();
    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(original));

    REQUIRE(result.error == ParseError::None);
    const AnnotationDocument& parsed = result.document;

    REQUIRE(parsed.schemaVersion == kCurrentSchemaVersion);
    REQUIRE(parsed.slideId == original.slideId);
    REQUIRE(parsed.sceneIndex == original.sceneIndex);
    REQUIRE(parsed.slide.fileName == original.slide.fileName);
    REQUIRE(parsed.slide.path == original.slide.path);
    REQUIRE(parsed.slide.width == original.slide.width);
    REQUIRE(parsed.slide.height == original.slide.height);
    REQUIRE(parsed.createdAt == original.createdAt);
    REQUIRE(parsed.modifiedAt == original.modifiedAt);
    REQUIRE(parsed.annotations.size() == 1);

    const Annotation& a = parsed.annotations.front();
    const Annotation& b = original.annotations.front();
    REQUIRE(a.id() == b.id());
    REQUIRE(a.type() == b.type());
    REQUIRE(a.properties().label == b.properties().label);
    REQUIRE(a.properties().classification == b.properties().classification);
    REQUIRE(a.properties().notes == b.properties().notes);
    REQUIRE(a.properties().lineWidth == b.properties().lineWidth);
    REQUIRE(a.properties().fillOpacity == b.properties().fillOpacity);
    REQUIRE(a.metadata().author == b.metadata().author);
    REQUIRE(a.metadata().createdAt == b.metadata().createdAt);
    REQUIRE(a.metadata().modifiedAt == b.metadata().modifiedAt);
}

TEST_CASE("a colour round-trips including a non-opaque alpha", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(makeDocument()));
    REQUIRE(result.error == ParseError::None);

    const Color color = result.document.annotations.front().properties().color;
    REQUIRE(color.r == 0x1F);
    REQUIRE(color.g == 0x77);
    REQUIRE(color.b == 0xB4);
    REQUIRE(color.a == 0x80);
}

TEST_CASE("an inverted rectangle round-trips with its corners as drawn",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{400.0, 300.0}, PointF{100.0, 50.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);

    const auto& rectangle = std::get<RectangleGeometry>(result.document.annotations.front().geometry());
    REQUIRE(rectangle.topLeft.x == 400.0);
    REQUIRE(rectangle.topLeft.y == 300.0);
    REQUIRE(rectangle.bottomRight.x == 100.0);
    REQUIRE(rectangle.bottomRight.y == 50.0);
}

TEST_CASE("a double needing full precision survives the round trip",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    const double awkward = 0.1 + 0.2;
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{awkward, 0.0}, PointF{1.0, 1.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    const auto& rectangle = std::get<RectangleGeometry>(result.document.annotations.front().geometry());
    REQUIRE(rectangle.topLeft.x == awkward);
}

TEST_CASE("malformed JSON is reported, not thrown", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument("{\"schemaVersion\": 1,");
    REQUIRE(result.error == ParseError::MalformedJson);
    REQUIRE_FALSE(result.message.empty());
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("an empty input is malformed, not an empty document", "[core][AnnotationSerialization]")
{
    REQUIRE(parseAnnotationDocument("").error == ParseError::MalformedJson);
}

TEST_CASE("JSON that is not an annotation document is rejected", "[core][AnnotationSerialization]")
{
    SECTION("an array at the root")
    {
        REQUIRE(parseAnnotationDocument("[]").error == ParseError::NotAnAnnotationDocument);
    }
    SECTION("an object with no schemaVersion")
    {
        REQUIRE(parseAnnotationDocument("{\"slideId\": \"x\"}").error
                == ParseError::NotAnAnnotationDocument);
    }
    SECTION("a schemaVersion that is not a number")
    {
        REQUIRE(parseAnnotationDocument("{\"schemaVersion\": \"1\"}").error
                == ParseError::NotAnAnnotationDocument);
    }
}

TEST_CASE("a newer schema version is refused outright", "[core][AnnotationSerialization]")
{
    // Loading what we understand and saving it back would silently drop
    // whatever the newer version added.
    const ParseResult result = parseAnnotationDocument(
        "{\"schemaVersion\": 2, \"slideId\": \"x\", \"sceneIndex\": 0, \"annotations\": []}");
    REQUIRE(result.error == ParseError::UnsupportedFutureVersion);
    REQUIRE(result.message.find('2') != std::string::npos);
}

TEST_CASE("a field of the wrong type is an InvalidField naming the field",
          "[core][AnnotationSerialization]")
{
    SECTION("annotations is not an array")
    {
        const ParseResult result = parseAnnotationDocument(
            "{\"schemaVersion\": 1, \"slideId\": \"x\", \"annotations\": {}}");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("annotations") != std::string::npos);
    }

    SECTION("a coordinate is a string")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"id": "a1", "type": "rectangle",
               "geometry": {"type": "rectangle",
                            "topLeft": {"x": "nope", "y": 0.0},
                            "bottomRight": {"x": 1.0, "y": 1.0}}}
            ]})");
        REQUIRE(result.error == ParseError::InvalidField);
    }

    SECTION("an unknown geometry type")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"id": "a1", "type": "hexagon",
               "geometry": {"type": "hexagon"}}
            ]})");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("hexagon") != std::string::npos);
    }

    SECTION("an annotation with no id")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"type": "rectangle",
               "geometry": {"type": "rectangle",
                            "topLeft": {"x": 0.0, "y": 0.0},
                            "bottomRight": {"x": 1.0, "y": 1.0}}}
            ]})");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("id") != std::string::npos);
    }

    SECTION("an unparseable timestamp")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0,
            "createdAt": "yesterday", "annotations": []})");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("createdAt") != std::string::npos);
    }
}

TEST_CASE("missing optional blocks fall back to defaults", "[core][AnnotationSerialization]")
{
    // A minimal hand-written file must load: properties, metadata, slide and
    // the timestamps are all omittable.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 2, "annotations": [
          {"id": "a1", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}}
        ]})");

    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.sceneIndex == 2);
    REQUIRE(result.document.annotations.size() == 1);

    const AnnotationProperties& properties = result.document.annotations.front().properties();
    REQUIRE(properties.color.r == 0xE6);
    REQUIRE(properties.lineWidth == 2.0f);
    REQUIRE(properties.label.empty());
}

TEST_CASE("unknown fields are dropped rather than preserved", "[core][AnnotationSerialization]")
{
    // Round-tripping unrecognised JSON would promise a forward compatibility
    // the version check already handles honestly.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0,
        "layers": ["not-implemented"], "annotations": []})");

    REQUIRE(result.error == ParseError::None);
    REQUIRE(serializeAnnotationDocument(result.document).find("layers") == std::string::npos);
}

TEST_CASE("a malformed colour is an InvalidField", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
          {"id": "a1", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}},
           "properties": {"color": "orange"}}
        ]})");
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("color") != std::string::npos);
}

TEST_CASE("a non-finite coordinate in a file is rejected on load",
          "[core][AnnotationSerialization]")
{
    // JSON has no NaN literal, but it has 1e400. nlohmann's lexer rejects an
    // out-of-range literal itself (it does not yield infinity), so this is
    // reported as MalformedJson before readDouble's isfinite check is reached.
    // Either way no non-finite value can enter an AnnotationDocument.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
          {"id": "a1", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 1e400, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}}
        ]})");
    REQUIRE(result.error == ParseError::MalformedJson);
}

TEST_CASE("a colour with signs, spaces or a 0x prefix inside a field is rejected",
          "[core][AnnotationSerialization]")
{
    for (const char* color : {"#+F+F+F+F", "# 1 2 3 4", "#0x0x0x0x", "#GGGGGGGG", "#1F77B4"}) {
        const std::string text = std::string(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"id": "a1", "type": "rectangle",
               "geometry": {"type": "rectangle",
                            "topLeft": {"x": 0.0, "y": 0.0},
                            "bottomRight": {"x": 1.0, "y": 1.0}},
               "properties": {"color": ")") + color + R"("}}
            ]})";
        REQUIRE(parseAnnotationDocument(text).error == ParseError::InvalidField);
    }
}

TEST_CASE("a failure partway through leaves nothing of the document behind",
          "[core][AnnotationSerialization]")
{
    // The first annotation is valid and the second is not. If the parser
    // returned what it had managed so far, a caller could save it back over a
    // file that still holds both.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "a3f8c2e1b4d50697", "sceneIndex": 2,
        "slide": {"fileName": "case001_HE.svs"},
        "annotations": [
          {"id": "good", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}},
          {"id": "bad", "type": "rectangle",
           "geometry": {"type": "hexagon"}}
        ]})");

    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.document.annotations.empty());
    REQUIRE(result.document.slideId.empty());
    REQUIRE(result.document.sceneIndex == 0);
    REQUIRE(result.document.schemaVersion == kCurrentSchemaVersion);
    REQUIRE(result.document.slide.fileName.empty());
}

TEST_CASE("the schema version must be a sane positive integer",
          "[core][AnnotationSerialization]")
{
    const char* bad[] = {
        R"({"schemaVersion": 0, "slideId": "x", "annotations": []})",
        R"({"schemaVersion": -1, "slideId": "x", "annotations": []})",
        R"({"schemaVersion": 4294967297, "slideId": "x", "annotations": []})",
    };
    for (const char* text : bad) {
        const ParseResult result = parseAnnotationDocument(text);
        REQUIRE(result.error != ParseError::None);
        REQUIRE(result.document.annotations.empty());
    }
}

TEST_CASE("the scene index must be a non-negative integer that fits",
          "[core][AnnotationSerialization]")
{
    // It becomes part of the file name, so a negative or truncated value names
    // a file that belongs to a different scene.
    REQUIRE(parseAnnotationDocument(
                R"({"schemaVersion": 1, "slideId": "x", "sceneIndex": -1, "annotations": []})")
                .error == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(
                R"({"schemaVersion": 1, "slideId": "x", "sceneIndex": 1.5, "annotations": []})")
                .error == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(
                R"({"schemaVersion": 1, "slideId": "x", "sceneIndex": 4294967296, "annotations": []})")
                .error == ParseError::InvalidField);
}

TEST_CASE("a document with no slide id is rejected", "[core][AnnotationSerialization]")
{
    REQUIRE(parseAnnotationDocument(R"({"schemaVersion": 1, "annotations": []})").error
            == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(
                R"({"schemaVersion": 1, "slideId": "", "annotations": []})")
                .error == ParseError::InvalidField);
}

TEST_CASE("out-of-domain float properties are rejected", "[core][AnnotationSerialization]")
{
    auto withProperty = [](const char* json) {
        return std::string(R"({"schemaVersion": 1, "slideId": "x", "sceneIndex": 0,
            "annotations": [{"id": "a1", "type": "rectangle",
              "geometry": {"type": "rectangle", "topLeft": {"x": 0.0, "y": 0.0},
                           "bottomRight": {"x": 1.0, "y": 1.0}},
              "properties": {)") + json + "}}]}";
    };

    // 1e300 is a finite double and an infinite float.
    REQUIRE(parseAnnotationDocument(withProperty(R"("lineWidth": 1e300)")).error
            == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(withProperty(R"("lineWidth": -2.0)")).error
            == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(withProperty(R"("fillOpacity": 5.0)")).error
            == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(withProperty(R"("fillOpacity": -0.5)")).error
            == ParseError::InvalidField);
    REQUIRE(parseAnnotationDocument(withProperty(R"("lineWidth": 2.5)")).error
            == ParseError::None);
}

TEST_CASE("colours decode across the whole range", "[core][AnnotationSerialization]")
{
    auto colorOf = [](const char* text) {
        const std::string json = std::string(R"({"schemaVersion": 1, "slideId": "x",
            "sceneIndex": 0, "annotations": [{"id": "a1", "type": "rectangle",
              "geometry": {"type": "rectangle", "topLeft": {"x": 0.0, "y": 0.0},
                           "bottomRight": {"x": 1.0, "y": 1.0}},
              "properties": {"color": ")") + text + R"("}}]})";
        return parseAnnotationDocument(json);
    };

    const ParseResult black = colorOf("#00000000");
    REQUIRE(black.error == ParseError::None);
    REQUIRE(black.document.annotations.front().properties().color.a == 0x00);

    const ParseResult white = colorOf("#FFFFFFFF");
    REQUIRE(white.error == ParseError::None);
    REQUIRE(white.document.annotations.front().properties().color.r == 0xFF);

    const ParseResult lower = colorOf("#abcdef01");
    REQUIRE(lower.error == ParseError::None);
    REQUIRE(lower.document.annotations.front().properties().color.g == 0xCD);
}

TEST_CASE("the serializer never writes a document the parser would refuse",
          "[core][AnnotationSerialization]")
{
    // The two sides share one set of bounds. If they ever drift, this fails.
    AnnotationDocument document;
    document.slideId = "a3f8c2e1b4d50697";
    document.annotations.emplace_back("a1", RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    const double outOfRange[] = {0.0, -1.0, 2000.0};
    for (double value : outOfRange) {
        AnnotationProperties properties = document.annotations.front().properties();
        properties.lineWidth = static_cast<float>(value);
        document.annotations.front().setProperties(properties);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    for (double value : {-0.5, 1.5}) {
        AnnotationProperties properties = document.annotations.front().properties();
        properties.lineWidth = 2.0f;
        properties.fillOpacity = static_cast<float>(value);
        document.annotations.front().setProperties(properties);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    // And the defaults, which must always survive the round trip.
    AnnotationProperties defaults;
    document.annotations.front().setProperties(defaults);
    const std::string json = serializeAnnotationDocument(document);
    REQUIRE_FALSE(json.empty());
    const ParseResult parsed = parseAnnotationDocument(json);
    REQUIRE(parsed.error == ParseError::None);
    REQUIRE(parsed.document.annotations.front().properties().lineWidth == defaults.lineWidth);
    REQUIRE(parsed.document.annotations.front().properties().fillOpacity == defaults.fillOpacity);
}

TEST_CASE("a document with two annotations sharing an id is rejected",
          "[core][AnnotationSerialization]")
{
    // hitTest scans from the end while find and setGeometry take the first
    // match, so duplicates make the application move a shape the user did not
    // click. The id is the identity; two of them is a malformed document.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "a3f8c2e1b4d50697", "sceneIndex": 0,
        "annotations": [
          {"id": "same", "type": "rectangle",
           "geometry": {"type": "rectangle", "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}},
          {"id": "same", "type": "rectangle",
           "geometry": {"type": "rectangle", "topLeft": {"x": 5.0, "y": 5.0},
                        "bottomRight": {"x": 6.0, "y": 6.0}}}
        ]})");

    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("same") != std::string::npos);
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("distinct ids in one document are fine", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "a3f8c2e1b4d50697", "sceneIndex": 0,
        "annotations": [
          {"id": "first", "type": "rectangle",
           "geometry": {"type": "rectangle", "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}},
          {"id": "second", "type": "rectangle",
           "geometry": {"type": "rectangle", "topLeft": {"x": 5.0, "y": 5.0},
                        "bottomRight": {"x": 6.0, "y": 6.0}}}
        ]})");

    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 2);
}

TEST_CASE("a polygon round-trips through the serializer",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("poly-1", PolygonGeometry{{PointF{1.5, 2.0}, PointF{30.0, 4.0},
                                              PointF{10.0, 40.0}}, true}));

    const std::string text = serializeAnnotationDocument(document);
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("\"polygon\"") != std::string::npos);

    const ParseResult result = parseAnnotationDocument(text);
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);

    const Annotation& back = result.document.annotations.front();
    REQUIRE(back.type() == AnnotationType::Polygon);
    const PolygonGeometry& shape = std::get<PolygonGeometry>(back.geometry());
    REQUIRE(shape.closed);
    REQUIRE(shape.vertices.size() == 3);
    REQUIRE(shape.vertices[0].x == 1.5);
    REQUIRE(shape.vertices[2].y == 40.0);
}

TEST_CASE("a polyline round-trips and stays open",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("line-1", PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 10.0}}, false}));

    const ParseResult result =
        parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    const Annotation& back = result.document.annotations.front();
    REQUIRE(back.type() == AnnotationType::Polyline);
    REQUIRE_FALSE(std::get<PolygonGeometry>(back.geometry()).closed);
}

// schemaVersion does not move when a geometry type is added. Bumping it would
// make every shipped build refuse rectangle-only files during a staggered
// rollout; not bumping makes an old build fail on the polygon alone and leave
// the file intact.
TEST_CASE("adding a geometry type does not bump schemaVersion",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("poly-1", PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                              PointF{0.0, 10.0}}, true}));
    REQUIRE(serializeAnnotationDocument(document).find("\"schemaVersion\": 1")
            != std::string::npos);
}

// Review Focus 4. A hand-edited file must be rejected with a named problem,
// not indexed into.
TEST_CASE("an empty vertices array is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "polygon", "vertices": []})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("vertices") != std::string::npos);
}

TEST_CASE("a missing vertices key is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result =
        parseAnnotationDocument(documentWithGeometry(R"({"type": "polygon"})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("vertices") != std::string::npos);
}

TEST_CASE("a vertex that is not an object is rejected",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon", "vertices": [{"x": 0, "y": 0}, 7, {"x": 1, "y": 1}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("vertices") != std::string::npos);
}

TEST_CASE("a vertex missing a coordinate is rejected",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon", "vertices": [{"x": 0, "y": 0}, {"x": 1}, {"x": 2, "y": 2}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
}

// Note: a literal 1e400 in the file is rejected by the JSON lexer as
// MalformedJson and never reaches readPolygon -- the header documents this.
// It is pinned here so the distinction is deliberate rather than a surprise.
TEST_CASE("an out-of-range vertex literal is malformed JSON",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon",
            "vertices": [{"x": 0, "y": 0}, {"x": 1e400, "y": 1}, {"x": 2, "y": 2}]})"));
    REQUIRE(result.error == ParseError::MalformedJson);
}

TEST_CASE("a two-vertex polygon is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon", "vertices": [{"x": 0, "y": 0}, {"x": 1, "y": 1}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
}

TEST_CASE("a one-vertex polyline is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polyline", "vertices": [{"x": 0, "y": 0}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
}

// Three collinear vertices are a valid polygon. Pinning this stops a later
// tightening of "validity" from silently making existing files unloadable.
TEST_CASE("a collinear polygon is accepted", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon",
            "vertices": [{"x": 0, "y": 0}, {"x": 1, "y": 0}, {"x": 2, "y": 0}]})"));
    REQUIRE(result.error == ParseError::None);
}

// Review Focus 5. The governing rule seen from the writer's side: refusing to
// write what we would refuse to read is what guarantees the application cannot
// produce a file it cannot open.
TEST_CASE("serializing an under-minimum polygon is refused",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("bad-1", PolygonGeometry{{PointF{0.0, 0.0}, PointF{1.0, 1.0}}, true}));
    REQUIRE(serializeAnnotationDocument(document).empty());
}

TEST_CASE("serializing a polygon with a non-finite vertex is refused",
          "[core][AnnotationSerialization]")
{
    const double infinity = std::numeric_limits<double>::infinity();
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("bad-2", PolygonGeometry{{PointF{0.0, 0.0}, PointF{infinity, 1.0},
                                             PointF{2.0, 2.0}}, true}));
    REQUIRE(serializeAnnotationDocument(document).empty());
}

// The whole point of holding schemaVersion at 1: an older build must fail on
// the shape, with a message that explains itself, and must not touch the file.
// The placeholder here must be a type this build will never know. It used to
// be "ellipse", which stopped being unknown the moment the ellipse shipped --
// the test then exercised a malformed ellipse rather than an unknown shape.
TEST_CASE("an unknown geometry type names the likely cause",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "trapezoid", "vertices": []})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("newer version") != std::string::npos);
    // Naming the offending type is the whole value of the message: a user has
    // to be told which shape their build is too old for.
    REQUIRE(result.message.find("trapezoid") != std::string::npos);
}

// Spec 5.3: readGeometry is a string chain with no compile-time link to the
// variant. Nothing in the compiler forces its ellipse branch to exist -- this
// test is what forces it. Without the branch the application writes files it
// then refuses to open.
TEST_CASE("an ellipse round-trips through JSON", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "e1", EllipseGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));

    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);

    const AnnotationGeometry& geometry = result.document.annotations.front().geometry();
    REQUIRE(std::holds_alternative<EllipseGeometry>(geometry));
    const EllipseGeometry& ellipse = std::get<EllipseGeometry>(geometry);
    REQUIRE(ellipse.topLeft.x == 10.0);
    REQUIRE(ellipse.topLeft.y == 20.0);
    REQUIRE(ellipse.bottomRight.x == 110.0);
    REQUIRE(ellipse.bottomRight.y == 70.0);
    REQUIRE(result.document.annotations.front().type() == AnnotationType::Ellipse);
}

TEST_CASE("an ellipse is written with the ellipse type name",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "e1", EllipseGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"ellipse\"") != std::string::npos);
}

// Spec 2.2: accepted, not rejected. Refusing would make the whole document
// permanently unloadable -- and by the governing rule, never again writable --
// over one shape the user can see and fix.
TEST_CASE("a degenerate ellipse loads rather than failing the document",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "e1", EllipseGeometry{PointF{10.0, 25.0}, PointF{110.0, 25.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);
}

// Spec 2.6: readGeometry is a string chain with no compile-time link to the
// variant. Nothing in the compiler forces its point branch to exist -- this
// test is what forces it. Without the branch the application writes files it
// then refuses to open.
TEST_CASE("a point round-trips through JSON", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("p1", PointGeometry{PointF{1200.5, 800.25}});

    const std::string text = serializeAnnotationDocument(document);
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("\"point\"") != std::string::npos);

    const ParseResult result = parseAnnotationDocument(text);
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);

    const AnnotationGeometry& geometry = result.document.annotations.front().geometry();
    REQUIRE(std::holds_alternative<PointGeometry>(geometry));
    REQUIRE(std::get<PointGeometry>(geometry).position.x == 1200.5);
    REQUIRE(std::get<PointGeometry>(geometry).position.y == 800.25);
    REQUIRE(result.document.annotations.front().type() == AnnotationType::Point);
}

// Same reasoning as the point above, and with one thing more to prove: the
// pair is ordered, so a round-trip that came back reversed would silently turn
// every arrow in a file around.
TEST_CASE("an arrow round-trips with its direction intact",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{90.0, 60.0}, PointF{10.0, 20.0}});

    const std::string text = serializeAnnotationDocument(document);
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("\"arrow\"") != std::string::npos);

    const ParseResult result = parseAnnotationDocument(text);
    REQUIRE(result.error == ParseError::None);

    const AnnotationGeometry& geometry = result.document.annotations.front().geometry();
    REQUIRE(std::holds_alternative<ArrowGeometry>(geometry));
    const ArrowGeometry& arrow = std::get<ArrowGeometry>(geometry);
    // Tail right-of and below head: an arrow drawn right-to-left and
    // bottom-to-top. Normalising anywhere in the round trip would swap these.
    REQUIRE(arrow.tail.x == 90.0);
    REQUIRE(arrow.tail.y == 60.0);
    REQUIRE(arrow.head.x == 10.0);
    REQUIRE(arrow.head.y == 20.0);
    REQUIRE(result.document.annotations.front().type() == AnnotationType::Arrow);
}

// Accepted from disk even though the tool refuses to create one: refusing it
// would make the whole document unloadable, and by the governing rule never
// again writable, over one shape the user can see and delete.
TEST_CASE("a zero-length arrow round-trips", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{40.0, 40.0}, PointF{40.0, 40.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    REQUIRE(std::holds_alternative<ArrowGeometry>(
        result.document.annotations.front().geometry()));
}

// Review Focus 2: never write a file we cannot read back. A non-finite
// coordinate has no JSON form the parser would accept, so the write is refused
// outright rather than producing `null` or `NaN` in the file.
TEST_CASE("a non-finite point or arrow refuses to serialize",
          "[core][AnnotationSerialization]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();

    AnnotationDocument withPoint = makeDocument();
    withPoint.annotations.clear();
    withPoint.annotations.emplace_back("p1", PointGeometry{PointF{nan, 0.0}});
    REQUIRE(serializeAnnotationDocument(withPoint).empty());

    AnnotationDocument withArrow = makeDocument();
    withArrow.annotations.clear();
    withArrow.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{0.0, 0.0}, PointF{infinity, 0.0}});
    REQUIRE(serializeAnnotationDocument(withArrow).empty());
}

// Review Focus 3: a missing endpoint must be named, not defaulted. Silently
// reading an absent head as the origin would move someone's arrow to the
// corner of the slide and then autosave it there.
TEST_CASE("an arrow missing an endpoint is refused by name",
          "[core][AnnotationSerialization]")
{
    const ParseResult missingHead = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "arrow", "tail": {"x": 1, "y": 2}})"));
    REQUIRE(missingHead.error == ParseError::InvalidField);
    REQUIRE(missingHead.message.find("head") != std::string::npos);

    const ParseResult missingTail = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "arrow", "head": {"x": 1, "y": 2}})"));
    REQUIRE(missingTail.error == ParseError::InvalidField);
    REQUIRE(missingTail.message.find("tail") != std::string::npos);
}

TEST_CASE("a point missing its position is refused by name",
          "[core][AnnotationSerialization]")
{
    const ParseResult result =
        parseAnnotationDocument(documentWithGeometry(R"({"type": "point"})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("position") != std::string::npos);
}

// schemaVersion does not move when a geometry type is added. Bumping it would
// make every shipped build refuse rectangle-only files during a staggered
// rollout; not bumping makes an old build fail on the new shape alone and
// leave the file intact.
TEST_CASE("adding the point and arrow types does not bump schemaVersion",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("p1", PointGeometry{PointF{5.0, 6.0}});
    document.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.schemaVersion == 1);
}

// readGeometry is a string chain with no compile-time link to the variant.
// Nothing in the compiler forces its text branch to exist -- this test is
// what forces it. Without the branch the application writes files it then
// refuses to open.
TEST_CASE("a text round-trips through JSON with its words", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    AnnotationProperties properties;
    properties.label = "\xCE\x9D\xCE\xAD\xCE\xBA\xCF\x81\xCF\x89\xCF\x83\xCE\xB7, \"re-cut\" \\ ok";
    document.annotations.emplace_back("t1", TextGeometry{PointF{1200.5, 800.25}}, properties,
                                      AnnotationMetadata{});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"text\"") != std::string::npos);
    const ParseResult result = parseAnnotationDocument(json);

    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);
    const Annotation& text = result.document.annotations.front();
    REQUIRE(text.type() == AnnotationType::Text);
    const auto& geometry = std::get<TextGeometry>(text.geometry());
    CHECK(geometry.anchor.x == 1200.5);
    CHECK(geometry.anchor.y == 800.25);
    CHECK(text.properties().label == properties.label);
}

TEST_CASE("a text with a non-finite anchor is refused", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("t1", TextGeometry{PointF{std::nan(""), 1.0}});
    REQUIRE(serializeAnnotationDocument(document).empty());
}

TEST_CASE("a ruler and an angle round-trip with their points in order", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("r1", RulerGeometry{PointF{90.0, 60.5}, PointF{10.0, 20.0}});
    document.annotations.emplace_back(
        "g1", AngleGeometry{PointF{30.0, 5.0}, PointF{10.0, 10.0}, PointF{12.0, 40.25}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE_FALSE(json.empty());
    CHECK(json.find("\"ruler\"") != std::string::npos);
    CHECK(json.find("\"angle\"") != std::string::npos);
    const ParseResult result = parseAnnotationDocument(json);
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 2);

    const auto& ruler = std::get<RulerGeometry>(result.document.annotations[0].geometry());
    CHECK(ruler.start.x == 90.0);
    CHECK(ruler.start.y == 60.5);
    CHECK(ruler.end.x == 10.0);
    CHECK(ruler.end.y == 20.0);
    const auto& angle = std::get<AngleGeometry>(result.document.annotations[1].geometry());
    CHECK(angle.first.x == 30.0);
    CHECK(angle.vertex.y == 10.0);
    CHECK(angle.second.y == 40.25);
}

TEST_CASE("a ruler or angle with a non-finite point is refused", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("r1", RulerGeometry{PointF{std::nan(""), 1.0}, PointF{2.0, 2.0}});
    CHECK(serializeAnnotationDocument(document).empty());
    document.annotations.clear();
    document.annotations.emplace_back(
        "g1", AngleGeometry{PointF{1.0, 1.0}, PointF{0.0, 0.0}, PointF{1.0, std::nan("")}});
    CHECK(serializeAnnotationDocument(document).empty());
}
