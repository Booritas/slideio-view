#pragma once

#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/core/Viewport.h"

#include <vector>

namespace slideio::viewer::core
{

class CoordinateSystem
{
public:
    explicit CoordinateSystem(const TilePyramid& pyramid);

    int bestLevel(double viewportScale) const;

    std::vector<TileKey> visibleTiles(const Viewport& vp) const;

    Rect<double> tileSlideRect(const TileKey& key) const;

    Rect<double> tileScreenRect(const TileKey& key, const Viewport& vp) const;

private:
    const TilePyramid& m_pyramid;
};

} // namespace slideio::viewer::core
