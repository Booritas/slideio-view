#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationImport.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationSerialization.h"

#include <atomic>
#include <string>
#include <variant>
#include <vector>

using namespace slideio::viewer::core;

TEST_CASE("the format is chosen by content", "[core][AnnotationImport]")
{
    CHECK(detectAnnotationFileFormat("<?xml version=\"1.0\"?><ASAP_Annotations/>") == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat("  \r\n\t<ASAP_Annotations/>") == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat("\xEF\xBB\xBF<ASAP_Annotations/>") == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat("{\"type\":\"Feature\"}") == AnnotationFileFormat::GeoJson);
    CHECK(detectAnnotationFileFormat("\xEF\xBB\xBF  [") == AnnotationFileFormat::GeoJson);
    CHECK(detectAnnotationFileFormat("") == AnnotationFileFormat::GeoJson);
    CHECK(detectAnnotationFileFormat("garbage") == AnnotationFileFormat::GeoJson);
}

TEST_CASE("isAxisAlignedBox and closedAreaFrom", "[core][AnnotationImport]")
{
    const std::vector<PointF> box = {{10, 20}, {40, 20}, {40, 50}, {10, 50}};
    CHECK(isAxisAlignedBox(box));
    const AnnotationGeometry rectangle = closedAreaFrom(box);
    const auto* asRectangle = std::get_if<RectangleGeometry>(&rectangle);
    REQUIRE(asRectangle != nullptr);
    CHECK(asRectangle->topLeft.x == 10);
    CHECK(asRectangle->topLeft.y == 20);
    CHECK(asRectangle->bottomRight.x == 40);
    CHECK(asRectangle->bottomRight.y == 50);

    const std::vector<PointF> diamond = {{0, 5}, {5, 0}, {10, 5}, {5, 10}};
    CHECK_FALSE(isAxisAlignedBox(diamond));
    const AnnotationGeometry polygon = closedAreaFrom(diamond);
    const auto* asPolygon = std::get_if<PolygonGeometry>(&polygon);
    REQUIRE(asPolygon != nullptr);
    CHECK(asPolygon->closed);
    CHECK(asPolygon->vertices.size() == 4);
}

TEST_CASE("hasFiniteExtent", "[core][AnnotationImport]")
{
    CHECK_FALSE(hasFiniteExtent({{-1.7e308, 0}, {1.7e308, 0}}));
    CHECK(hasFiniteExtent({{0, 0}, {5, 5}}));
    CHECK(hasFiniteExtent({}));
}

TEST_CASE("cornersOf takes the extremes as they are", "[core][AnnotationImport]")
{
    const auto [low, high] = cornersOf({{3, 9}, {-2, 4}, {7, -1}});
    CHECK(low.x == -2);
    CHECK(low.y == -1);
    CHECK(high.x == 7);
    CHECK(high.y == 9);
}

TEST_CASE("a UTF-16 file is XML when it starts like XML", "[core][AnnotationImport]")
{
    // ASAP writes UTF-8, but an ASAP file saved again as UTF-16 by another
    // tool is still XML; Qt's reader decodes it, and the GeoJSON reader's
    // "not valid JSON" would point at the wrong format.
    CHECK(detectAnnotationFileFormat(std::string("\xFF\xFE<\0A\0", 6)) == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat(std::string("\xFE\xFF\0<\0A", 6)) == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat(std::string("<\0?\0x\0", 6)) == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat(std::string("\0<\0?", 4)) == AnnotationFileFormat::AsapXml);
    CHECK(detectAnnotationFileFormat(std::string("\xFF\xFE{\0", 4)) == AnnotationFileFormat::GeoJson);
}

TEST_CASE("parseRgbColor reads #RRGGBB and nothing else", "[core][AnnotationImport]")
{
    Color color;
    REQUIRE(parseRgbColor("#F4FA58", color));
    CHECK(color.r == 244);
    CHECK(color.g == 250);
    CHECK(color.b == 88);
    CHECK(color.a == 255);
    const Color before = color;
    for (const std::string bad : {"#zz0000", "#F4FA58FF", "F4FA58", "#F4FA5", ""}) {
        INFO(bad);
        CHECK_FALSE(parseRgbColor(bad, color));
        CHECK(color.r == before.r);
    }
}

TEST_CASE("the shared failure and cancel results", "[core][AnnotationImport]")
{
    const AnnotationImportResult failed = importFailure("boom");
    CHECK(failed.error == "boom");
    CHECK_FALSE(failed.cancelled);
    const AnnotationImportResult stopped = cancelledImport();
    CHECK(stopped.cancelled);
    CHECK(stopped.error.empty());
    std::atomic<bool> flag{false};
    CHECK_FALSE(importCancelRequested(nullptr));
    CHECK_FALSE(importCancelRequested(&flag));
    flag = true;
    CHECK(importCancelRequested(&flag));
}
