#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/OverviewGrid.h"

using slideio::viewer::core::OverviewGrid;
using slideio::viewer::core::Rect;

namespace
{
Rect<double> box(double x, double y, double w, double h) { return Rect<double>{x, y, w, h}; }
const Rect<double> kScene = box(0, 0, 1024, 1024);
const Rect<double> kAll = box(-10, -10, 5000, 5000);
}

TEST_CASE("OverviewGrid SquareSizeCapsLongSide", "[core][OverviewGrid]")
{
    OverviewGrid g(box(0, 0, 100000, 50000), {10});
    REQUIRE(g.columns() <= 1024);
    REQUIRE(g.rows() <= 1024);
    REQUIRE(g.squareSize() >= 100000.0 / 1024.0);
}

TEST_CASE("OverviewGrid CountsAtCentre", "[core][OverviewGrid]")
{
    OverviewGrid g(kScene, {1});
    g.add(box(0, 0, 3, 1), {0}); // straddles squares 0 and 1; centre 1.5 -> square 1
    g.finish();
    const auto cells = g.cells(kAll, {true}, true);
    REQUIRE(cells.size() == 1);
    REQUIRE(cells[0].column == 1);
    REQUIRE(cells[0].row == 0);
}

TEST_CASE("OverviewGrid TwoClassObjectCountsUnderBoth", "[core][OverviewGrid]")
{
    OverviewGrid g(kScene, {5, 3});
    g.add(box(0, 0, 1, 1), {0, 1});
    g.finish();
    auto c = g.cells(kAll, {true, true}, true);
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].dominant == 0);                  // tie: legend order
    REQUIRE(c[0].opacity == Catch::Approx(0.75)); // counted once: 1 / p99(1)
    c = g.cells(kAll, {false, true}, true);
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].dominant == 1);
}

TEST_CASE("OverviewGrid SixteenthClassMergesIntoOther", "[core][OverviewGrid]")
{
    std::vector<int64_t> counts;
    for (int i = 0; i < 16; ++i)
        counts.push_back(100 - i); // class 15 is the smallest
    OverviewGrid g(kScene, counts);
    g.add(box(0, 0, 1, 1), {15});
    g.finish();
    std::vector<bool> vis(16, false);
    REQUIRE(g.cells(kAll, vis, true).empty());
    vis[15] = true;
    const auto c = g.cells(kAll, vis, false);
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].dominant == OverviewGrid::kOtherClasses);
}

TEST_CASE("OverviewGrid DominantSkipsHiddenClasses", "[core][OverviewGrid]")
{
    OverviewGrid g(kScene, {9, 2});
    for (int i = 0; i < 3; ++i)
        g.add(box(0, 0, 1, 1), {0});
    g.add(box(0, 0, 1, 1), {1});
    g.add(box(0, 0, 1, 1), {});
    g.finish();
    REQUIRE(g.cells(kAll, {true, true}, true)[0].dominant == 0);
    REQUIRE(g.cells(kAll, {false, true}, true)[0].dominant == 1);
    REQUIRE(g.cells(kAll, {false, false}, true)[0].dominant == OverviewGrid::kUnclassified);
    REQUIRE(g.cells(kAll, {false, false}, false).empty());
}

TEST_CASE("OverviewGrid OpacityUsesP99", "[core][OverviewGrid]")
{
    OverviewGrid g(kScene, {1});
    for (int s = 0; s < 100; ++s)
        for (int i = 0; i < 10; ++i)
            g.add(box(s, 0, 1, 1), {0});
    for (int i = 0; i < 1000; ++i)
        g.add(box(500, 5, 1, 1), {0});
    g.finish();
    int tens = 0;
    for (const auto& c : g.cells(kAll, {true}, true))
        if (c.row == 0)
        {
            ++tens;
            REQUIRE(c.opacity > 0.7); // p99 is 10, not 1000
        }
    REQUIRE(tens == 100);
}

TEST_CASE("OverviewGrid CellsOutsideViewAreNotReturned", "[core][OverviewGrid]")
{
    OverviewGrid g(kScene, {1});
    g.add(box(0, 0, 1, 1), {0});
    g.add(box(900, 900, 1, 1), {0});
    g.finish();
    const auto c = g.cells(box(800, 800, 150, 200), {true}, true);
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].column == 900);
}
