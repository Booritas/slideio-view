#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationImport.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

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
