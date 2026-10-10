#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"

#include <chrono>
#include <limits>
#include <string>

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
    metadata.author = "s.melnikov";
    metadata.createdAt = at(1772683629LL);
    metadata.modifiedAt = at(1772683750LL);

    return Annotation("4f1c8e2a-91b7-4d3e-8c15-2a9f7e6b0d44", AnnotationType::Rectangle,
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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{400.0, 300.0}, PointF{100.0, 50.0}});

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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"color\": \"#E67E22FF\"") != std::string::npos);
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
        document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                          RectangleGeometry{PointF{0.0, 0.0}, PointF{nan, 1.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("infinity")
    {
        const double inf = std::numeric_limits<double>::infinity();
        document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                          RectangleGeometry{PointF{0.0, inf}, PointF{1.0, 1.0}});
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
        document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                          RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
                                          AnnotationProperties{}, metadata);
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("annotation modifiedAt")
    {
        AnnotationDocument document;
        document.slideId = "id";
        AnnotationMetadata metadata;
        metadata.modifiedAt = beforeEpoch;
        document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                          RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{awkward, 0.0}, PointF{1.0, 1.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("0.30000000000000004") != std::string::npos);
}

TEST_CASE("annotation metadata is written, not regenerated", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());
    REQUIRE(json.find("\"author\": \"s.melnikov\"") != std::string::npos);
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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{400.0, 300.0}, PointF{100.0, 50.0}});

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
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{awkward, 0.0}, PointF{1.0, 1.0}});

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
