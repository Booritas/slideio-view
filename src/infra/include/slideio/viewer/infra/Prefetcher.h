#pragma once

#include "slideio/viewer/infra/TileLoadScheduler.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TilePyramid.h"

#include <vector>

namespace slideio::viewer::infra
{

class Prefetcher
{
public:
    Prefetcher() = delete;

    static std::vector<TileRequest> spatialNeighbors(const std::vector<core::TileKey>& visibleTiles,
                                                     const core::TilePyramid& pyramid);

    static std::vector<TileRequest> zoomNeighbors(const std::vector<core::TileKey>& visibleTiles,
                                                  int currentLevel,
                                                  int maxLevel);
};

} // namespace slideio::viewer::infra
