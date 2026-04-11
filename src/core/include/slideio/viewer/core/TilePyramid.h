#pragma once

#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

#include <vector>

namespace slideio::viewer::core
{

class TilePyramid
{
public:
    TilePyramid(int slideWidth, int slideHeight, const std::vector<LevelInfo>& levels);

    int slideWidth() const;
    int slideHeight() const;
    int numLevels() const;
    const LevelInfo& levelInfo(int level) const;

    int bestLevelForScale(double viewportScale) const;

    TileKey tileKeyAt(int level, int slideX, int slideY) const;

    std::vector<TileKey> visibleTiles(int level, int slideX, int slideY, int slideWidth, int slideHeight) const;

private:
    int m_slideWidth;
    int m_slideHeight;
    std::vector<LevelInfo> m_levels;
};

} // namespace slideio::viewer::core
