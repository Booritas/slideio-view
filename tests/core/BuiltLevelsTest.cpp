#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slideio/viewer/core/BuiltLevels.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

LevelInfo storedLevel(int index, int width, int height, int tile = 256, bool natively = true)
{
    LevelInfo l;
    l.level = index;
    l.width = width;
    l.height = height;
    l.tileWidth = tile;
    l.tileHeight = tile;
    l.tilesX = (width + tile - 1) / tile;
    l.tilesY = (height + tile - 1) / tile;
    l.nativelyTiled = natively;
    return l;
}

SlideInfo slideOf(int width, int height, int channels = 3, DataType dt = DataType::Byte)
{
    SlideInfo info;
    info.width = width;
    info.height = height;
    info.numChannels = channels;
    info.channelDataType = dt;
    return info;
}

} // namespace

TEST_CASE("no built levels when the coarsest stored level is at most 2048 px", "[core][BuiltLevels]")
{
    CHECK(planBuiltLevels({storedLevel(0, 2048, 1500)}, slideOf(2048, 1500)).levels.empty());
    CHECK_FALSE(planBuiltLevels({storedLevel(0, 2049, 100)}, slideOf(2049, 100)).levels.empty());
}

TEST_CASE("no built levels for a Z-stack or a T-series", "[core][BuiltLevels]")
{
    SlideInfo z = slideOf(63803, 44997);
    z.numZSlices = 3;
    CHECK(planBuiltLevels({storedLevel(0, 63803, 44997)}, z).levels.empty());

    SlideInfo t = slideOf(63803, 44997);
    t.numTFrames = 2;
    CHECK(planBuiltLevels({storedLevel(0, 63803, 44997)}, t).levels.empty());
}

TEST_CASE("the example slide gets five levels, three resident", "[core][BuiltLevels]")
{
    const auto plan = planBuiltLevels({storedLevel(0, 63803, 44997)}, slideOf(63803, 44997));
    REQUIRE(plan.levels.size() == 5);
    CHECK(plan.sourceLevel == 0);

    const int widths[] = {31902, 15951, 7976, 3988, 1994};
    const int heights[] = {22499, 11250, 5625, 2813, 1407};
    const int factors[] = {2, 4, 8, 16, 32};
    const bool resident[] = {false, false, true, true, true};
    for (size_t i = 0; i < 5; ++i) {
        const auto& l = plan.levels[i];
        CHECK(l.width == widths[i]);
        CHECK(l.height == heights[i]);
        CHECK(l.builtFactor == factors[i]);
        CHECK(l.resident == resident[i]);
        CHECK(l.level == static_cast<int>(i) + 1);
        CHECK(l.built);
        CHECK(l.tileWidth == 256);
        CHECK(l.tileHeight == 256);
        CHECK(l.nativelyTiled);
        CHECK(l.tilesX == (widths[i] + 255) / 256);
        CHECK(l.tilesY == (heights[i] + 255) / 256);
        // The adapter's rule: the smaller of the two axis ratios.
        const double expected = std::min(widths[i] / 63803.0, heights[i] / 44997.0);
        CHECK(std::abs(l.scale - expected) < 1e-12);
    }
}

TEST_CASE("budget boundary", "[core][BuiltLevels]")
{
    // 8192 x 8192, one byte per pixel: /2 is 4096^2, /4 is 2048^2.
    const std::vector<LevelInfo> stored{storedLevel(0, 8192, 8192)};
    const SlideInfo info = slideOf(8192, 8192, 1, DataType::Byte);
    const size_t exact = 4096ull * 4096ull + 2048ull * 2048ull;

    auto fits = planBuiltLevels(stored, info, exact);
    REQUIRE(fits.levels.size() == 2);
    CHECK(fits.levels[0].resident);
    CHECK(fits.levels[1].resident);

    auto over = planBuiltLevels(stored, info, exact - 1);
    REQUIRE(over.levels.size() == 2);
    CHECK_FALSE(over.levels[0].resident);
    CHECK(over.levels[1].resident);
}

TEST_CASE("nothing resident means nothing built", "[core][BuiltLevels]")
{
    // The coarsest built level, 2048 x 2048 x 40 x 2 bytes, is 320 MiB.
    const auto plan = planBuiltLevels({storedLevel(0, 4096, 4096)}, slideOf(4096, 4096, 40, DataType::UInt16));
    CHECK(plan.levels.empty());
}

TEST_CASE("tile size follows a natively tiled source, else 256", "[core][BuiltLevels]")
{
    const auto native = planBuiltLevels({storedLevel(0, 8192, 8192, 512, true)}, slideOf(8192, 8192));
    REQUIRE_FALSE(native.levels.empty());
    CHECK(native.levels[0].tileWidth == 512);
    CHECK(native.levels[0].tileHeight == 512);

    const auto flat = planBuiltLevels({storedLevel(0, 8192, 8192, 256, false)}, slideOf(8192, 8192));
    REQUIRE_FALSE(flat.levels.empty());
    CHECK(flat.levels[0].tileWidth == 256);
}

TEST_CASE("coarsestStoredLevelIndex skips built levels", "[core][BuiltLevels]")
{
    LevelInfo stored;
    LevelInfo built;
    built.built = true;
    CHECK(coarsestStoredLevelIndex({stored, stored, built, built}) == 1);
    CHECK(coarsestStoredLevelIndex({stored, stored, stored}) == 2);
    CHECK(coarsestStoredLevelIndex({}) == -1);
}

TEST_CASE("hasBuiltLevels is true only when a built level is present", "[core][BuiltLevels]")
{
    LevelInfo stored;
    LevelInfo built;
    built.built = true;
    CHECK(hasBuiltLevels({stored, built}));
    CHECK(hasBuiltLevels({built}));
    CHECK_FALSE(hasBuiltLevels({stored, stored}));
    CHECK_FALSE(hasBuiltLevels({}));
}

TEST_CASE("tileSlideRect matches the adapter's arithmetic", "[core][BuiltLevels]")
{
    LevelInfo level = storedLevel(5, 1994, 1407);
    level.scale = 1994 / 63803.0;

    const auto r = tileSlideRect(level, 7, 5, 63803, 44997);
    REQUIRE(r.valid);
    CHECK(r.levelX == 1792);
    CHECK(r.levelY == 1280);
    CHECK(r.tileW == 1994 - 1792);
    CHECK(r.tileH == 1407 - 1280);
    CHECK(r.slideX == static_cast<int>(std::round(1792 * 63803 / 1994.0)));
    CHECK(r.slideY == static_cast<int>(std::round(1280 * 63803 / 1994.0)));
    CHECK(r.slideX + r.slideW == 63803);
    CHECK(r.slideY + r.slideH <= 44997);
    CHECK(r.slideY + r.slideH >= 44997 - 1);

    CHECK_FALSE(tileSlideRect(level, 8, 0, 63803, 44997).valid);
    CHECK_FALSE(tileSlideRect(level, 0, 6, 63803, 44997).valid);
    CHECK_FALSE(tileSlideRect(level, -1, 0, 63803, 44997).valid);
}

TEST_CASE("requestableTiles drops not-ready keys and keeps order", "[core][BuiltLevels]")
{
    const std::vector<TileKey> keys{TileKey(1, 0, 0), TileKey(1, 1, 0), TileKey(2, 0, 0), TileKey(1, 2, 0)};
    const auto ready = requestableTiles(keys, [](const TileKey& k) { return k.level() == 1; });
    REQUIRE(ready.size() == 3);
    CHECK(ready[0] == keys[0]);
    CHECK(ready[1] == keys[1]);
    CHECK(ready[2] == keys[3]);
}
