#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ResultGeometry.h"

using namespace slideio::viewer::core;

namespace
{
ResultRing square(double x0, double y0, double x1, double y1, bool hole = false)
{
    return ResultRing{{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, hole};
}
}

TEST_CASE("HoleIsAMiss", "[results]")
{
    ResultObject o;
    o.rings = {square(0, 0, 100, 100), square(40, 40, 60, 60, true)};
    CHECK_FALSE(resultContains(o, {50, 50}));
    CHECK(resultContains(o, {10, 10}));
    CHECK_FALSE(resultContains(o, {150, 50}));
}

TEST_CASE("SecondOuterRingIsAHit", "[results]")
{
    ResultObject o;
    o.rings = {square(0, 0, 10, 10), square(20, 20, 30, 30)};
    CHECK(resultContains(o, {25, 25}));
    CHECK_FALSE(resultContains(o, {15, 15}));
}

TEST_CASE("ThinKeepsEndsAndMinimum", "[results]")
{
    std::vector<PointF> line;
    for (int i = 0; i < 1000; ++i) {
        line.push_back({i * 0.01, 0.0});
    }
    const auto thin = thinForDrawing(line, 1.0);
    CHECK(thin.front().x == line.front().x);
    CHECK(thin.back().x == line.back().x);
    CHECK(thin.size() >= 3);
    CHECK(thin.size() < line.size());

    const std::vector<PointF> tri{{0, 0}, {1, 0}, {0, 1}};
    CHECK(thinForDrawing(tri, 1000.0).size() == 3);
}
