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
    // Implementations should pick the best pyramid level internally.
    virtual TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                                int targetWidth, int targetHeight) = 0;

    // Called the first time the implementation decides a pyramid level is
    // unreliable (e.g., a corrupt tile-offset table) and starts routing reads
    // around it. Default no-op for backends that don't track this.
    virtual void setOnLevelMarkedUnreliable(std::function<void(int)> /*callback*/) {}
};

} // namespace slideio::viewer::core
