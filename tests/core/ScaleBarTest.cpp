#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ScaleBar.h"

using namespace slideio::viewer::core;

TEST_CASE("pickScaleBarLength picks 20 um at 40x native zoom", "[core][ScaleBar]")
{
    // mpp = 0.25 um/px (typical 40x), max bar = 172 px.
    // Candidate fitting check: 50/0.25=200 (too wide), 20/0.25=80 (fits).
    auto tick = pickScaleBarLength(0.25, 172);
    REQUIRE(tick.lengthMicrons == 20.0);
    REQUIRE(tick.pixelWidth == 80);
}

TEST_CASE("pickScaleBarLength picks 50 um at 20x native zoom", "[core][ScaleBar]")
{
    // mpp = 0.5 um/px. 100/0.5=200 (too wide), 50/0.5=100 (fits).
    auto tick = pickScaleBarLength(0.5, 172);
    REQUIRE(tick.lengthMicrons == 50.0);
    REQUIRE(tick.pixelWidth == 100);
}

TEST_CASE("pickScaleBarLength picks 2 um when zoomed in 10x from 40x base", "[core][ScaleBar]")
{
    // mpp = 0.025 um/px. 5/0.025=200 (too wide), 2/0.025=80 (fits).
    auto tick = pickScaleBarLength(0.025, 172);
    REQUIRE(tick.lengthMicrons == 2.0);
    REQUIRE(tick.pixelWidth == 80);
}

TEST_CASE("pickScaleBarLength picks 0.2 um when zoomed in 100x from 40x base", "[core][ScaleBar]")
{
    // mpp = 0.0025 um/px. 0.5/0.0025=200 (too wide), 0.2/0.0025=80 (fits).
    auto tick = pickScaleBarLength(0.0025, 172);
    REQUIRE(tick.lengthMicrons == 0.2);
    REQUIRE(tick.pixelWidth == 80);
}

TEST_CASE("pickScaleBarLength picks 1000 um when zoomed out", "[core][ScaleBar]")
{
    // mpp = 10 um/px. 2000/10=200 (too wide), 1000/10=100 (fits).
    auto tick = pickScaleBarLength(10.0, 172);
    REQUIRE(tick.lengthMicrons == 1000.0);
    REQUIRE(tick.pixelWidth == 100);
}

TEST_CASE("pickScaleBarLength returns zero on zero input", "[core][ScaleBar]")
{
    auto tick = pickScaleBarLength(0.0, 172);
    REQUIRE(tick.lengthMicrons == 0.0);
    REQUIRE(tick.pixelWidth == 0);
}

TEST_CASE("pickScaleBarLength returns zero on negative input", "[core][ScaleBar]")
{
    auto tick = pickScaleBarLength(-1.0, 172);
    REQUIRE(tick.lengthMicrons == 0.0);
    REQUIRE(tick.pixelWidth == 0);
}

TEST_CASE("pickScaleBarLength returns smallest candidate when even 0.1 um overflows", "[core][ScaleBar]")
{
    // Below the table's floor: 0.0001 um/px. 0.1/0.0001 = 1000 px (overflows 172).
    // We still return 0.1 um with its computed pixel width — caller decides.
    auto tick = pickScaleBarLength(0.0001, 172);
    REQUIRE(tick.lengthMicrons == 0.1);
    REQUIRE(tick.pixelWidth == 1000);
}

TEST_CASE("pickScaleBarLength returns largest candidate when zoomed out beyond table", "[core][ScaleBar]")
{
    // Above the table's ceiling: 1000 um/px. 100000/1000 = 100 px (fits).
    auto tick = pickScaleBarLength(1000.0, 172);
    REQUIRE(tick.lengthMicrons == 100000.0);
    REQUIRE(tick.pixelWidth == 100);
}
