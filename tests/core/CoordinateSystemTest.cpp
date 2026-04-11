#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slideio/viewer/core/CoordinateSystem.h"
#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/Viewport.h"
#include "slideio/viewer/core/TileKey.h"

using namespace slideio::viewer::core;

static TilePyramid makeTestPyramid()
{
    std::vector<LevelInfo> levels = {
        {0, 4000, 2000, 1.0, 40.0, 256, 256, 16, 8},
        {1, 2000, 1000, 0.5, 20.0, 256, 256, 8, 4},
        {2, 1000, 500, 0.25, 10.0, 256, 256, 4, 2},
    };
    return TilePyramid(4000, 2000, levels);
}

TEST_CASE("CoordinateSystem visibleTiles returns non-empty", "[core][CoordinateSystem]")
{
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    auto tiles = cs.visibleTiles(vp);
    REQUIRE_FALSE(tiles.empty());
}

TEST_CASE("CoordinateSystem tileSlideRect", "[core][CoordinateSystem]")
{
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    // Level 0, tile at column=1, row=0 → slide rect starts at (256, 0), size 256x256
    TileKey key{0, 1, 0};
    auto rect = cs.tileSlideRect(key);

    REQUIRE(rect.x == 256.0);
    REQUIRE(rect.y == 0.0);
    REQUIRE(rect.width == 256.0);
    REQUIRE(rect.height == 256.0);
}

TEST_CASE("CoordinateSystem tileSlideRect at coarser level", "[core][CoordinateSystem]")
{
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    // Level 1 (scale 0.5): tile at col=0, row=0 covers 256 pixels at level 1
    // which is 512 slide pixels (256 / 0.5)
    TileKey key{1, 0, 0};
    auto rect = cs.tileSlideRect(key);

    REQUIRE(rect.x == 0.0);
    REQUIRE(rect.y == 0.0);
    // Tile covers 256/0.5 = 512 slide pixels
    REQUIRE(rect.width == 512.0);
    REQUIRE(rect.height == 512.0);
}

TEST_CASE("CoordinateSystem tileScreenRect", "[core][CoordinateSystem]")
{
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    TileKey key{0, 0, 0};
    auto screenRect = cs.tileScreenRect(key, vp);

    // Should have positive width/height
    REQUIRE(screenRect.width > 0);
    REQUIRE(screenRect.height > 0);
}

TEST_CASE("CoordinateSystem bestLevel", "[core][CoordinateSystem]")
{
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    REQUIRE(cs.bestLevel(1.0) == 0);
    REQUIRE(cs.bestLevel(0.5) == 1);
    REQUIRE(cs.bestLevel(0.25) == 2);
    REQUIRE(cs.bestLevel(0.1) == 2);
}
