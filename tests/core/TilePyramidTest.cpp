#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/TileKey.h"

using namespace slideio::viewer::core;

static std::vector<LevelInfo> makeTestLevels()
{
    // Simulates a 3-level pyramid: 40000x20000 base, 256x256 tiles
    return {
        {0, 40000, 20000, 1.0, 40.0, 256, 256, 157, 79},
        {1, 20000, 10000, 0.5, 20.0, 256, 256, 79, 40},
        {2, 10000, 5000, 0.25, 10.0, 256, 256, 40, 20},
    };
}

TEST_CASE("TilePyramid basic properties", "[core][TilePyramid]")
{
    auto levels = makeTestLevels();
    TilePyramid pyramid(40000, 20000, levels);

    REQUIRE(pyramid.numLevels() == 3);
    REQUIRE(pyramid.levelInfo(0).width == 40000);
    REQUIRE(pyramid.levelInfo(2).scale == 0.25);
}

TEST_CASE("TilePyramid bestLevelForScale", "[core][TilePyramid]")
{
    auto levels = makeTestLevels();
    TilePyramid pyramid(40000, 20000, levels);

    // Scale 1.0 means 1 screen pixel = 1 slide pixel → level 0
    REQUIRE(pyramid.bestLevelForScale(1.0) == 0);

    // Scale 0.5 → level 1
    REQUIRE(pyramid.bestLevelForScale(0.5) == 1);

    // Scale 0.3 → between level 1 (0.5) and level 2 (0.25), pick level 1 (closest >= 0.3)
    REQUIRE(pyramid.bestLevelForScale(0.3) == 1);

    // Scale 0.1 → below all levels, pick coarsest
    REQUIRE(pyramid.bestLevelForScale(0.1) == 2);

    // Scale 2.0 → above all levels, pick finest
    REQUIRE(pyramid.bestLevelForScale(2.0) == 0);
}

TEST_CASE("TilePyramid visibleTiles", "[core][TilePyramid]")
{
    auto levels = makeTestLevels();
    TilePyramid pyramid(40000, 20000, levels);

    // A viewport covering the top-left corner at level 0
    auto tiles = pyramid.visibleTiles(0, 0, 0, 512, 512);

    // With 256x256 tiles, a 512x512 region should need 2x2 = 4 tiles
    REQUIRE(tiles.size() == 4);

    // All should be level 0
    for (const auto& t : tiles) {
        REQUIRE(t.level() == 0);
    }
}

TEST_CASE("TilePyramid visibleTiles handles single tile", "[core][TilePyramid]")
{
    auto levels = makeTestLevels();
    TilePyramid pyramid(40000, 20000, levels);

    // A region smaller than one tile
    auto tiles = pyramid.visibleTiles(0, 100, 100, 50, 50);
    REQUIRE(tiles.size() == 1);
    REQUIRE(tiles[0].level() == 0);
    REQUIRE(tiles[0].column() == 0);
    REQUIRE(tiles[0].row() == 0);
}

TEST_CASE("TilePyramid tileKeyAt", "[core][TilePyramid]")
{
    auto levels = makeTestLevels();
    TilePyramid pyramid(40000, 20000, levels);

    auto key = pyramid.tileKeyAt(0, 300, 100);
    REQUIRE(key.level() == 0);
    REQUIRE(key.column() == 1); // 300 / 256 = 1
    REQUIRE(key.row() == 0);    // 100 / 256 = 0
}

TEST_CASE("TilePyramid single level", "[core][TilePyramid]")
{
    std::vector<LevelInfo> levels = {
        {0, 1000, 1000, 1.0, 20.0, 256, 256, 4, 4},
    };
    TilePyramid pyramid(1000, 1000, levels);

    REQUIRE(pyramid.numLevels() == 1);
    REQUIRE(pyramid.bestLevelForScale(0.1) == 0);
    REQUIRE(pyramid.bestLevelForScale(10.0) == 0);
}
