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

// --- coarseTiles: the backdrop keys both paintGL passes build ---------------
//
// This logic lived inline in ViewportWidget::paintGL, duplicated in the
// single-channel and the catch-all rendering path. The copies drifted: a
// colour-mode fix was applied to one and not the other, and only the path that
// can never be colour-managed got it. Extracting it here is what makes it
// testable at all -- nothing in it needs a GL context.

TEST_CASE("coarseTiles covers the whole slide when the viewport is fitted",
          "[core][CoordinateSystem]")
{
    // Level 2 is 1000x500 in 256px tiles -> 4x2 = 8 tiles. A fitted viewport
    // sees the entire slide, so every one of them is a backdrop candidate.
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    auto tiles = cs.coarseTiles(vp, 2, ColorMode::Raw);
    REQUIRE(tiles.size() == 8);
    for (const auto& key : tiles) {
        REQUIRE(key.level() == 2);
        REQUIRE(key.column() >= 0);
        REQUIRE(key.column() <= 3);
        REQUIRE(key.row() >= 0);
        REQUIRE(key.row() <= 1);
    }
}

TEST_CASE("coarseTiles stamps the requested colour mode on every key",
          "[core][CoordinateSystem]")
{
    // The whole point of the extraction. TilePyramid yields geometry only, so
    // its keys default to Raw; a backdrop looked up with those draws raw pixels
    // behind a colour-managed view.
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    auto managed = cs.coarseTiles(vp, 2, ColorMode::Managed);
    REQUIRE_FALSE(managed.empty());
    for (const auto& key : managed) {
        REQUIRE(key.colorMode() == ColorMode::Managed);
    }

    auto raw = cs.coarseTiles(vp, 2, ColorMode::Raw);
    REQUIRE_FALSE(raw.empty());
    for (const auto& key : raw) {
        REQUIRE(key.colorMode() == ColorMode::Raw);
    }
}

TEST_CASE("coarseTiles stamps the requested z and t on every key",
          "[core][CoordinateSystem]")
{
    // Without this the backdrop under a Z-stack is looked up at z=0 while the
    // fine tiles over it are at the viewed plane -- the wrong focal plane
    // rendered behind the right one.
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    auto tiles = cs.coarseTiles(vp, 2, ColorMode::Raw, 3, 7);
    REQUIRE_FALSE(tiles.empty());
    for (const auto& key : tiles) {
        REQUIRE(key.zIndex() == 3);
        REQUIRE(key.tFrame() == 7);
    }
}

TEST_CASE("coarseTiles defaults z and t to zero", "[core][CoordinateSystem]")
{
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    auto tiles = cs.coarseTiles(vp, 2, ColorMode::Raw);
    REQUIRE_FALSE(tiles.empty());
    for (const auto& key : tiles) {
        REQUIRE(key.zIndex() == 0);
        REQUIRE(key.tFrame() == 0);
    }
}

TEST_CASE("coarseTiles clamps a negative viewport origin to the slide edge",
          "[core][CoordinateSystem]")
{
    // fitToSlide letterboxes this 2:1 slide into a 4:3 screen, so the visible
    // slide rect starts above the slide (negative y). Feeding that straight to
    // TilePyramid would ask for negative rows.
    auto pyramid = makeTestPyramid();
    CoordinateSystem cs(pyramid);

    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);
    REQUIRE(vp.visibleSlideRect().y < 0.0);

    auto tiles = cs.coarseTiles(vp, 2, ColorMode::Raw);
    REQUIRE_FALSE(tiles.empty());
    for (const auto& key : tiles) {
        REQUIRE(key.row() >= 0);
        REQUIRE(key.column() >= 0);
    }
}
