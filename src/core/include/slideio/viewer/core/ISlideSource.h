#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

#include <functional>
#include <vector>

namespace slideio::viewer::core
{

class ISlideSource
{
public:
    virtual ~ISlideSource() = default;
    virtual SlideInfo slideInfo() const = 0;
    virtual std::vector<LevelInfo> levels() const = 0;
    virtual TileData readTile(const TileKey& key) = 0;
    // Read an arbitrary slide region resampled to (targetWidth, targetHeight).
    // Implementations should pick the best pyramid level internally. For Z-stack
    // or T-series sources, zIndex/tFrame selects which slice to read; default
    // is 0 to keep 2D callers unchanged.
    virtual TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                                int targetWidth, int targetHeight,
                                int zIndex = 0, int tFrame = 0) = 0;

    // Called the first time the implementation decides a pyramid level is
    // unreliable (e.g., a corrupt tile-offset table) and starts routing reads
    // around it. Default no-op for backends that don't track this.
    virtual void setOnLevelMarkedUnreliable(std::function<void(int)> /*callback*/) {}
};

} // namespace slideio::viewer::core
