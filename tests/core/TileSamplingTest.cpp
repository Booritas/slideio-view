#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/TileSampling.h"

using namespace slideio::viewer::core;

TEST_CASE("planTileSampling visits every tile when the grid fits the budget", "[core][TileSampling]")
{
    auto plan = planTileSampling(4, 5, 256);
    REQUIRE(plan.strideX == 1);
    REQUIRE(plan.strideY == 1);
    REQUIRE(plan.complete);
    REQUIRE(sampledTileCount(4, 5, plan) == 20);
}

TEST_CASE("planTileSampling is complete when the grid is exactly the budget", "[core][TileSampling]")
{
    auto plan = planTileSampling(16, 16, 256);
    REQUIRE(plan.complete);
    REQUIRE(sampledTileCount(16, 16, plan) == 256);
}

TEST_CASE("planTileSampling thins an oversized grid to within the budget", "[core][TileSampling]")
{
    // The case that prompted this: a single-level WSI whose only level is full
    // resolution, so the "coarsest" level is a 322x406 grid of 256px tiles.
    auto plan = planTileSampling(322, 406, 256);
    REQUIRE_FALSE(plan.complete);
    REQUIRE(plan.strideX > 1);
    REQUIRE(plan.strideY > 1);
    REQUIRE(sampledTileCount(322, 406, plan) <= 256);
}

TEST_CASE("planTileSampling still samples the whole extent of a thinned grid", "[core][TileSampling]")
{
    // Display-range detection is only meaningful if the samples are spread
    // across the slide, so the stride must not overshoot the grid.
    auto plan = planTileSampling(322, 406, 256);
    REQUIRE(plan.strideX <= 322);
    REQUIRE(plan.strideY <= 406);
    REQUIRE(sampledTileCount(322, 406, plan) > 0);
}

TEST_CASE("planTileSampling thins a very lopsided grid", "[core][TileSampling]")
{
    auto plan = planTileSampling(2000, 1, 100);
    REQUIRE_FALSE(plan.complete);
    REQUIRE(sampledTileCount(2000, 1, plan) <= 100);
    REQUIRE(sampledTileCount(2000, 1, plan) > 0);
}

TEST_CASE("planTileSampling never returns a stride below one", "[core][TileSampling]")
{
    auto plan = planTileSampling(1, 1, 1);
    REQUIRE(plan.strideX >= 1);
    REQUIRE(plan.strideY >= 1);
    REQUIRE(plan.complete);
}

TEST_CASE("planTileSampling tolerates a degenerate grid", "[core][TileSampling]")
{
    auto plan = planTileSampling(0, 0, 256);
    REQUIRE(plan.strideX >= 1);
    REQUIRE(plan.strideY >= 1);
    REQUIRE(sampledTileCount(0, 0, plan) == 0);
}

TEST_CASE("planTileSampling tolerates a non-positive budget", "[core][TileSampling]")
{
    // Treated as "sample as little as possible" rather than dividing by zero.
    auto plan = planTileSampling(322, 406, 0);
    REQUIRE(plan.strideX >= 1);
    REQUIRE(plan.strideY >= 1);
    REQUIRE(sampledTileCount(322, 406, plan) > 0);
}
