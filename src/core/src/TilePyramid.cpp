#include "slideio/viewer/core/TilePyramid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace slideio::viewer::core
{

TilePyramid::TilePyramid(int slideWidth, int slideHeight, const std::vector<LevelInfo>& levels)
    : m_slideWidth(slideWidth)
    , m_slideHeight(slideHeight)
    , m_levels(levels)
{
}

int TilePyramid::slideWidth() const
{
    return m_slideWidth;
}

int TilePyramid::slideHeight() const
{
    return m_slideHeight;
}

int TilePyramid::numLevels() const
{
    return static_cast<int>(m_levels.size());
}

const LevelInfo& TilePyramid::levelInfo(int level) const
{
    if (level < 0 || level >= numLevels()) {
        throw std::out_of_range("TilePyramid::levelInfo: level " + std::to_string(level) + " out of range");
    }
    return m_levels[static_cast<size_t>(level)];
}

int TilePyramid::bestLevelForScale(double viewportScale) const
{
    if (m_levels.empty()) {
        throw std::runtime_error("TilePyramid::bestLevelForScale: no levels available");
    }

    // Pick the level whose scale is closest to but not less than viewportScale.
    // Level scale represents the ratio of level pixels to slide pixels
    // (e.g., scale=0.5 means the level has half the resolution of the full slide).
    // viewportScale is screen_pixels / slide_pixels.
    // We want the finest level that still has enough resolution, i.e., level.scale >= viewportScale.

    int bestLevel = 0;
    double bestScale = m_levels[0].scale;

    for (size_t i = 0; i < m_levels.size(); ++i) {
        double levelScale = m_levels[i].scale;
        if (levelScale >= viewportScale) {
            // This level has sufficient resolution. Prefer the one with smallest
            // scale that is still >= viewportScale (least overshoot).
            if (bestScale < viewportScale || levelScale < bestScale) {
                bestLevel = static_cast<int>(i);
                bestScale = levelScale;
            }
        }
    }

    // If no level has scale >= viewportScale (all are too coarse),
    // return the level with the highest scale (finest available).
    if (bestScale < viewportScale) {
        for (size_t i = 0; i < m_levels.size(); ++i) {
            if (m_levels[i].scale > bestScale) {
                bestLevel = static_cast<int>(i);
                bestScale = m_levels[i].scale;
            }
        }
    }

    return bestLevel;
}

TileKey TilePyramid::tileKeyAt(int level, int slideX, int slideY) const
{
    const auto& info = levelInfo(level);

    // Convert slide coordinates to level coordinates
    double levelX = static_cast<double>(slideX) * info.scale;
    double levelY = static_cast<double>(slideY) * info.scale;

    int col = 0;
    int row = 0;

    if (info.tileWidth > 0) {
        col = static_cast<int>(std::floor(levelX / info.tileWidth));
        col = std::max(0, std::min(col, info.tilesX - 1));
    }
    if (info.tileHeight > 0) {
        row = static_cast<int>(std::floor(levelY / info.tileHeight));
        row = std::max(0, std::min(row, info.tilesY - 1));
    }

    return TileKey(level, col, row);
}

std::vector<TileKey> TilePyramid::visibleTiles(int level, int slideX, int slideY,
                                                 int slideWidth, int slideHeight) const
{
    const auto& info = levelInfo(level);
    std::vector<TileKey> result;

    if (info.tileWidth <= 0 || info.tileHeight <= 0 || info.tilesX <= 0 || info.tilesY <= 0) {
        return result;
    }

    // Convert slide-coordinate rect to level-pixel coordinates
    double levelLeft = static_cast<double>(slideX) * info.scale;
    double levelTop = static_cast<double>(slideY) * info.scale;
    double levelRight = static_cast<double>(slideX + slideWidth) * info.scale;
    double levelBottom = static_cast<double>(slideY + slideHeight) * info.scale;

    // Compute tile index range
    int colStart = static_cast<int>(std::floor(levelLeft / info.tileWidth));
    int colEnd = static_cast<int>(std::floor((levelRight - 1.0) / info.tileWidth));
    int rowStart = static_cast<int>(std::floor(levelTop / info.tileHeight));
    int rowEnd = static_cast<int>(std::floor((levelBottom - 1.0) / info.tileHeight));

    // Clamp to valid range
    colStart = std::max(0, colStart);
    colEnd = std::min(colEnd, info.tilesX - 1);
    rowStart = std::max(0, rowStart);
    rowEnd = std::min(rowEnd, info.tilesY - 1);

    for (int r = rowStart; r <= rowEnd; ++r) {
        for (int c = colStart; c <= colEnd; ++c) {
            result.emplace_back(level, c, r);
        }
    }

    return result;
}

} // namespace slideio::viewer::core
