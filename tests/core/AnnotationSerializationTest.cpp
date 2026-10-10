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
    metadata.modifiedAt = at(1772683700LL);

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
    REQUIRE(json.find("\"topLeft\"") != std::string::npos);
    REQUIRE(json.find("400.0") != std::string::npos);
    REQUIRE(json.find("50.0") != std::string::npos);
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
    REQUIRE(json.find("\"modifiedAt\": \"2026-03-05T04:08:20Z\"") != std::string::npos);
}
