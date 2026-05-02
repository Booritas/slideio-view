#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

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
};

} // namespace slideio::viewer::core
