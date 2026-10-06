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

    /// Keys for the cached coarser-level tiles covering the viewport at `level`,
    /// used as a blurry backdrop beneath tiles that have not loaded yet.
    ///
    /// Returns fully stamped keys. `TilePyramid` yields geometry only, so the
    /// keys it builds carry the default `ColorMode::Raw` and z/t of 0. A backdrop
    /// looked up with those draws raw pixels behind a colour-managed view, and
    /// the wrong focal plane behind the right one -- both of which the viewer
    /// shipped before this was factored out of two divergent copies in
    /// ViewportWidget::paintGL.
    ///
    /// A viewport may extend past the slide edge (fitToSlide letterboxes), so
    /// the origin is clamped to the slide before the lookup.
    std::vector<TileKey> coarseTiles(const Viewport& vp, int level, ColorMode colorMode,
                                     int zIndex = 0, int tFrame = 0) const;

    Rect<double> tileSlideRect(const TileKey& key) const;

    Rect<double> tileScreenRect(const TileKey& key, const Viewport& vp) const;

private:
    const TilePyramid& m_pyramid;
};

} // namespace slideio::viewer::core
