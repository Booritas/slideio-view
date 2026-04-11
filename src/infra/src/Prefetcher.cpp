#include "slideio/viewer/infra/Prefetcher.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace slideio::viewer::infra
{

std::vector<TileRequest> Prefetcher::spatialNeighbors(const std::vector<core::TileKey>& visibleTiles,
                                                      const core::TilePyramid& pyramid)
{
    if (visibleTiles.empty()) {
        return {};
    }

    // Build a set of visible tile keys for quick lookup
    std::unordered_set<core::TileKey> visibleSet(visibleTiles.begin(), visibleTiles.end());

    std::vector<TileRequest> result;

    // Direction offsets for ring-1 neighbors (8 directions)
    static constexpr int kOffsets[][2] = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0},          {1,  0},
        {-1,  1}, {0,  1}, {1,  1}
    };

    // Track already-added neighbors to avoid duplicates
    std::unordered_set<core::TileKey> addedSet;

    for (const auto& tile : visibleTiles) {
        int level = tile.level();

        if (level < 0 || level >= pyramid.numLevels()) {
            continue;
        }

        const auto& lvl = pyramid.levelInfo(level);

        for (const auto& offset : kOffsets) {
            int neighborCol = tile.column() + offset[0];
            int neighborRow = tile.row() + offset[1];

            // Check bounds
            if (neighborCol < 0 || neighborCol >= lvl.tilesX ||
                neighborRow < 0 || neighborRow >= lvl.tilesY) {
                continue;
            }

            core::TileKey neighborKey(level, neighborCol, neighborRow);

            // Skip if already visible or already added as a neighbor
            if (visibleSet.count(neighborKey) > 0 || addedSet.count(neighborKey) > 0) {
                continue;
            }

            addedSet.insert(neighborKey);
            result.push_back(TileRequest{neighborKey, TilePriority::Prefetch});
        }
    }

    spdlog::debug("Prefetcher::spatialNeighbors: {} visible tiles -> {} prefetch requests",
                  visibleTiles.size(), result.size());

    return result;
}

std::vector<TileRequest> Prefetcher::zoomNeighbors(const std::vector<core::TileKey>& visibleTiles,
                                                   int currentLevel,
                                                   int maxLevel)
{
    if (visibleTiles.empty()) {
        return {};
    }

    std::vector<TileRequest> result;
    std::unordered_set<core::TileKey> addedSet;

    // For each visible tile, compute overlapping tiles at adjacent zoom levels.
    // Zoom level +1 means lower resolution (fewer tiles cover the same area).
    // Zoom level -1 means higher resolution (more tiles cover the same area).
    //
    // The mapping between levels uses the fact that each level typically has
    // half the dimensions of the previous level (scale doubles).
    // For level+1 (coarser): tile at (col, row) in level L overlaps tile at (col/2, row/2) in level L+1.
    // For level-1 (finer): tile at (col, row) in level L overlaps tiles at (col*2..col*2+1, row*2..row*2+1) in level L-1.

    for (const auto& tile : visibleTiles) {
        int col = tile.column();
        int row = tile.row();

        // Level + 1 (coarser, lower resolution)
        if (currentLevel + 1 <= maxLevel) {
            int coarseCol = col / 2;
            int coarseRow = row / 2;
            core::TileKey coarseKey(currentLevel + 1, coarseCol, coarseRow);
            if (addedSet.count(coarseKey) == 0) {
                addedSet.insert(coarseKey);
                result.push_back(TileRequest{coarseKey, TilePriority::Background});
            }
        }

        // Level - 1 (finer, higher resolution)
        if (currentLevel - 1 >= 0) {
            for (int dr = 0; dr <= 1; ++dr) {
                for (int dc = 0; dc <= 1; ++dc) {
                    int fineCol = col * 2 + dc;
                    int fineRow = row * 2 + dr;
                    core::TileKey fineKey(currentLevel - 1, fineCol, fineRow);
                    if (addedSet.count(fineKey) == 0) {
                        addedSet.insert(fineKey);
                        result.push_back(TileRequest{fineKey, TilePriority::Background});
                    }
                }
            }
        }
    }

    spdlog::debug("Prefetcher::zoomNeighbors: {} visible tiles at level {} -> {} background requests",
                  visibleTiles.size(), currentLevel, result.size());

    return result;
}

} // namespace slideio::viewer::infra
