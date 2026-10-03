#pragma once

namespace slideio::viewer::core
{

// How to walk a level's tile grid when scanning it for display-range
// statistics. A pyramid's coarsest level is small enough to read whole, but a
// slide with no downsampled level has only its full-resolution level, where
// reading every tile means decoding the entire slide.
struct TileSamplingPlan
{
    int strideX = 1;
    int strideY = 1;
    bool complete = true;  // true when the stride visits every tile in the grid
};

// Tile budget for the display-range scan done while a slide opens. Thinning
// the scan costs only statistical accuracy — a few hundred tiles spread across
// the slide describe its range as well as every tile does — so this is set for
// speed. A full-resolution tile costs roughly 12ms to decode.
constexpr int kMaxCoarseScanTiles = 256;

// Tile budget for reading a level in full, which is what every whole-slide
// overview does. Set much higher than the scan budget because the cost of
// saying no here is a missing thumbnail, not a less precise number: it only
// needs to be low enough to rule out the pathological case (the 130732-tile
// full-resolution level that prompted this would take about 27 minutes) while
// leaving every level that is read acceptably fast today alone.
constexpr int kMaxOverviewTiles = 4096;

// Returns a plan that visits at most maxTiles tiles of a tilesX-by-tilesY grid,
// spread across its whole extent. Strides are never below one, and `complete`
// says whether the grid was small enough to read in full.
TileSamplingPlan planTileSampling(int tilesX, int tilesY, int maxTiles);

// Number of tiles the plan visits in a tilesX-by-tilesY grid.
int sampledTileCount(int tilesX, int tilesY, const TileSamplingPlan& plan);

} // namespace slideio::viewer::core
