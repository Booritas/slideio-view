#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slideio/viewer/core/BuiltLevels.h"

#include <algorithm>
#include <cmath>
#include <functional>
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


namespace
{

LevelInfo residentBuiltLevel(int index, int width, int height, int factor)
{
    LevelInfo l = storedLevel(index, width, height);
    l.built = true;
    l.resident = true;
    l.builtFactor = factor;
    return l;
}

/// Stored level 0; resident built levels 1 (finest, 5x3 tiles), 2 (3x2) and
/// 3 (coarsest, 2x1), each half the previous one. Odd tile counts exercise the
/// clipping at the right and bottom edge.
std::vector<LevelInfo> standInLevels()
{
    return {storedLevel(0, 2400, 1200), residentBuiltLevel(1, 1200, 600, 2), residentBuiltLevel(2, 600, 300, 4),
            residentBuiltLevel(3, 300, 150, 8)};
}

std::function<bool(const TileKey&)> readyAmong(std::vector<TileKey> ready)
{
    return [ready = std::move(ready)](const TileKey& k) {
        return std::find(ready.begin(), ready.end(), k) != ready.end();
    };
}

} // namespace

TEST_CASE("a ready key stands in for itself", "[core][BuiltLevels]")
{
    const TileKey key(3, 0, 0);
    const auto out = readyStandIns(standInLevels(), key, readyAmong({key, TileKey(2, 0, 0)}));
    REQUIRE(out.size() == 1);
    CHECK(out[0] == key);
}

TEST_CASE("a stored or virtual key stands in for itself even when not ready", "[core][BuiltLevels]")
{
    auto levels = standInLevels();
    const auto never = [](const TileKey&) { return false; };

    const auto stored = readyStandIns(levels, TileKey(0, 3, 2), never);
    REQUIRE(stored.size() == 1);
    CHECK(stored[0] == TileKey(0, 3, 2));

    levels[2].resident = false; // a virtual built level
    const auto virt = readyStandIns(levels, TileKey(2, 1, 1), never);
    REQUIRE(virt.size() == 1);
    CHECK(virt[0] == TileKey(2, 1, 1));
}

TEST_CASE("a not-ready coarse key is replaced by its ready children", "[core][BuiltLevels]")
{
    const auto out = readyStandIns(standInLevels(), TileKey(3, 0, 0),
                                   readyAmong({TileKey(2, 0, 0), TileKey(2, 1, 0), TileKey(2, 0, 1),
                                               TileKey(2, 1, 1)}));
    const std::vector<TileKey> expected{TileKey(2, 0, 0), TileKey(2, 1, 0), TileKey(2, 0, 1), TileKey(2, 1, 1)};
    CHECK(out == expected);
}

TEST_CASE("children are clipped at the right and bottom edge", "[core][BuiltLevels]")
{
    const auto always = [](const TileKey& k) { return k.level() != 3; };
    const auto out = readyStandIns(standInLevels(), TileKey(3, 1, 0), always);
    const std::vector<TileKey> expected{TileKey(2, 2, 0), TileKey(2, 2, 1)};
    CHECK(out == expected);

    // Level 2's bottom-right tile (2, 1) covers only column 4 and row 2 of level 1.
    const auto corner = readyStandIns(standInLevels(), TileKey(2, 2, 1), [](const TileKey& k) {
        return k.level() == 1;
    });
    const std::vector<TileKey> expectedCorner{TileKey(1, 4, 2)};
    CHECK(corner == expectedCorner);
}

TEST_CASE("descent continues to the finest resident level and drops not-ready leaves", "[core][BuiltLevels]")
{
    // Level 3 (0,0) not ready. Of its level-2 children, (0,0) is ready, (1,0)
    // is not and has two ready level-1 children, (0,1) is not and has one,
    // (1,1) is ready. Level-1 (2,0), (3,1) and (1,2) are not ready: dropped.
    const auto out = readyStandIns(standInLevels(), TileKey(3, 0, 0),
                                   readyAmong({TileKey(2, 0, 0), TileKey(1, 3, 0), TileKey(1, 2, 1),
                                               TileKey(2, 1, 1), TileKey(1, 0, 2)}));
    const std::vector<TileKey> expected{TileKey(2, 0, 0), TileKey(1, 3, 0), TileKey(1, 2, 1), TileKey(1, 0, 2),
                                        TileKey(2, 1, 1)};
    CHECK(out == expected);
}

TEST_CASE("nothing ready yields no stand-ins", "[core][BuiltLevels]")
{
    const auto never = [](const TileKey&) { return false; };
    CHECK(readyStandIns(standInLevels(), TileKey(3, 0, 0), never).empty());
    CHECK(readyStandIns(standInLevels(), TileKey(3, 1, 0), never).empty());
}

TEST_CASE("stand-ins keep z, t and colour mode", "[core][BuiltLevels]")
{
    const auto out = readyStandIns(standInLevels(), TileKey(3, 1, 0, 2, 5, ColorMode::Managed),
                                   [](const TileKey& k) { return k.level() == 2; });
    const std::vector<TileKey> expected{TileKey(2, 2, 0, 2, 5, ColorMode::Managed),
                                        TileKey(2, 2, 1, 2, 5, ColorMode::Managed)};
    CHECK(out == expected);
}

TEST_CASE("no descent below the finest resident level", "[core][BuiltLevels]")
{
    // Level 0 would be ready, but it is stored: reading it is a full decode.
    const auto out = readyStandIns(standInLevels(), TileKey(1, 0, 0), [](const TileKey& k) {
        return k.level() == 0;
    });
    CHECK(out.empty());

    CHECK(readyStandIns(standInLevels(), TileKey(4, 0, 0), [](const TileKey&) { return true; }).empty());
    CHECK(readyStandIns(standInLevels(), TileKey(-1, 0, 0), [](const TileKey&) { return true; }).empty());
}

TEST_CASE("a striped or untiled source level is read by one thread", "[core][BuiltLevels]")
{
    LevelInfo tiled;
    tiled.width = 63803;
    tiled.height = 44997;
    tiled.tileWidth = 512;
    tiled.tileHeight = 512;
    CHECK(levelReadsConcurrently(tiled));
    CHECK(builtPassReadersFor(tiled, 4) == 4);
    CHECK(builtPassReadersFor(tiled, 0) == 1);

    // SlideIO's GDAL driver reports a striped level as one tile the size of the level.
    LevelInfo striped = tiled;
    striped.tileWidth = tiled.width;
    striped.tileHeight = tiled.height;
    CHECK_FALSE(levelReadsConcurrently(striped));
    CHECK(builtPassReadersFor(striped, 4) == 1);

    // No tile size of its own: the adapter imposed a grid on a level decoded whole.
    LevelInfo untiled = tiled;
    untiled.nativelyTiled = false;
    CHECK_FALSE(levelReadsConcurrently(untiled));
    CHECK(builtPassReadersFor(untiled, 4) == 1);

    // A tiled level one tile wide but many tiles tall is still read tile by tile.
    LevelInfo column = tiled;
    column.width = 512;
    CHECK(levelReadsConcurrently(column));
}
