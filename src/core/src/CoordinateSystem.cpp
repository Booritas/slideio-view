#include "slideio/viewer/core/CoordinateSystem.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

CoordinateSystem::CoordinateSystem(const TilePyramid& pyramid)
    : m_pyramid(pyramid)
{
}

int CoordinateSystem::bestLevel(double viewportScale) const
{
    return m_pyramid.bestLevelForScale(viewportScale);
}

std::vector<TileKey> CoordinateSystem::visibleTiles(const Viewport& vp) const
{
    Rect<double> slideRect = vp.visibleSlideRect();

    int level = m_pyramid.bestLevelForScale(vp.scale());

    // Clamp the visible slide rect to the slide bounds
    double left = std::max(0.0, slideRect.x);
    double top = std::max(0.0, slideRect.y);
    double right = std::min(static_cast<double>(m_pyramid.slideWidth()), slideRect.x + slideRect.width);
    double bottom = std::min(static_cast<double>(m_pyramid.slideHeight()), slideRect.y + slideRect.height);

    double clampedWidth = right - left;
    double clampedHeight = bottom - top;

    if (clampedWidth <= 0.0 || clampedHeight <= 0.0) {
        return {};
    }

    int slideX = static_cast<int>(std::floor(left));
    int slideY = static_cast<int>(std::floor(top));
    int slideW = static_cast<int>(std::ceil(clampedWidth));
    int slideH = static_cast<int>(std::ceil(clampedHeight));

    return m_pyramid.visibleTiles(level, slideX, slideY, slideW, slideH);
}

Rect<double> CoordinateSystem::tileSlideRect(const TileKey& key) const
{
    const auto& info = m_pyramid.levelInfo(key.level());

    // Tile position in level-pixel space
    double levelX = static_cast<double>(key.column()) * info.tileWidth;
    double levelY = static_cast<double>(key.row()) * info.tileHeight;

    // Clamp tile size for edge tiles that extend beyond the level dimensions
    double levelW = std::min(static_cast<double>(info.tileWidth),
                             static_cast<double>(info.width) - levelX);
    double levelH = std::min(static_cast<double>(info.tileHeight),
                             static_cast<double>(info.height) - levelY);

    // Convert from level-pixel space to slide-pixel space
    // level_pixels = slide_pixels * scale, so slide_pixels = level_pixels / scale
    Rect<double> rect;
    if (info.scale > 0.0) {
        rect.x = levelX / info.scale;
        rect.y = levelY / info.scale;
        rect.width = levelW / info.scale;
        rect.height = levelH / info.scale;
    }

    return rect;
}

Rect<double> CoordinateSystem::tileScreenRect(const TileKey& key, const Viewport& vp) const
{
    Rect<double> slideRect = tileSlideRect(key);

    // Convert slide-space corners to screen-space
    double screenX1 = 0.0;
    double screenY1 = 0.0;
    double screenX2 = 0.0;
    double screenY2 = 0.0;

    vp.slideToScreen(slideRect.x, slideRect.y, screenX1, screenY1);
    vp.slideToScreen(slideRect.x + slideRect.width, slideRect.y + slideRect.height, screenX2, screenY2);

    Rect<double> rect;
    rect.x = screenX1;
    rect.y = screenY1;
    rect.width = screenX2 - screenX1;
    rect.height = screenY2 - screenY1;

    return rect;
}

} // namespace slideio::viewer::core
