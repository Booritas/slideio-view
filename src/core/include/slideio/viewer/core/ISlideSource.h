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

    // As above, but the colour mode is the caller's: a read in flight across a
    // colour toggle is filed under the rendition it asked for. The default
    // ignores the mode, for backends that cannot convert colour.
    virtual TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                                int targetWidth, int targetHeight,
                                int zIndex, int tFrame, ColorMode /*mode*/)
    {
        return readBlock(slideX, slideY, slideWidth, slideHeight, targetWidth, targetHeight, zIndex, tFrame);
    }

    // False while a read of this tile would answer TileData::createPending().
    virtual bool isTileReady(const TileKey& /*key*/) const { return true; }

    // Told, on a worker thread, after each band of a viewer-built level is
    // accumulated (complete == false) and once with true at the end.
    virtual void addOnBuiltRowsReady(std::function<void(bool complete)> /*listener*/) {}

    // True while a build is running, or not yet started, for a slide that has
    // resident built levels.
    virtual bool builtOverviewPending() const { return false; }

    // Registers a listener told when the implementation decides a pyramid level
    // is unreliable (e.g., a corrupt tile-offset table) and starts routing reads
    // around it. The listener is invoked once per unreliable level, including
    // any found before it registered -- a caller built late in slide open, such
    // as the tile scheduler, still learns which levels to distrust. Default
    // no-op for backends that don't track this.
    virtual void addOnLevelMarkedUnreliable(std::function<void(int)> /*listener*/) {}

    // Colour mode selection. Backends that cannot convert colour ignore the
    // setter and always report Raw, so callers need not special-case them.
    virtual void setColorMode(ColorMode /*mode*/) {}
    virtual ColorMode colorMode() const { return ColorMode::Raw; }

    // The profile of the scene currently selected, which differs from
    // SlideInfo::colorProfileInfo: that one records what the file embeds, while
    // a colour-managed scene reports the profile it actually bound -- a supplied
    // default, for a slide that embeds none.
    virtual ColorProfileInfo activeColorProfileInfo() const { return {}; }
};

} // namespace slideio::viewer::core
