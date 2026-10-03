#include "slideio/viewer/core/TileSampling.h"

#include <algorithm>

namespace slideio::viewer::core
{

namespace
{

// Tiles visited along one axis when stepping by stride.
int steps(int count, int stride)
{
    if (count <= 0) return 0;
    const int s = std::max(1, stride);
    return (count + s - 1) / s;
}

} // namespace

TileSamplingPlan planTileSampling(int tilesX, int tilesY, int maxTiles)
{
    TileSamplingPlan plan;
    if (tilesX <= 0 || tilesY <= 0) {
        return plan;
    }

    const int budget = std::max(1, maxTiles);
    if (static_cast<long long>(tilesX) * tilesY <= budget) {
        return plan;
    }

    // Grow both strides together so the samples stay spread over the whole
    // grid rather than bunching along one axis. Stepping one at a time keeps
    // the stride as small as the budget allows, which matters because every
    // skipped tile is slide area the display-range scan never sees.
    plan.complete = false;
    while (static_cast<long long>(steps(tilesX, plan.strideX))
           * steps(tilesY, plan.strideY) > budget) {
        if (plan.strideX >= tilesX && plan.strideY >= tilesY) {
            break;  // one tile per axis is as thin as sampling can get
        }
        if (steps(tilesX, plan.strideX) >= steps(tilesY, plan.strideY)) {
            ++plan.strideX;
        } else {
            ++plan.strideY;
        }
    }
    return plan;
}

int sampledTileCount(int tilesX, int tilesY, const TileSamplingPlan& plan)
{
    return steps(tilesX, plan.strideX) * steps(tilesY, plan.strideY);
}

} // namespace slideio::viewer::core
