#include "slideio/viewer/core/BuiltLevels.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

int coarsestStoredLevelIndex(const std::vector<LevelInfo>& levels)
{
    for (int i = static_cast<int>(levels.size()) - 1; i >= 0; --i) {
        if (!levels[static_cast<size_t>(i)].built) {
            return i;
        }
    }
    return -1;
}

BuiltLevelPlan planBuiltLevels(const std::vector<LevelInfo>& stored, const SlideInfo& info,
                               size_t budgetBytes, int defaultTileSize)
{
    BuiltLevelPlan plan;
    if (info.numZSlices > 1 || info.numTFrames > 1) {
        return plan;
    }
    const int source = coarsestStoredLevelIndex(stored);
    if (source < 0) {
        return plan;
    }
    const LevelInfo& coarsest = stored[static_cast<size_t>(source)];
    if (coarsest.width <= 0 || coarsest.height <= 0
        || std::max(coarsest.width, coarsest.height) <= kBuiltTopLevelMaxSide) {
        return plan;
    }

    const bool keepTiles = coarsest.nativelyTiled && coarsest.tileWidth > 0 && coarsest.tileHeight > 0;
    const int tileW = keepTiles ? coarsest.tileWidth : defaultTileSize;
    const int tileH = keepTiles ? coarsest.tileHeight : defaultTileSize;
    int nextIndex = static_cast<int>(stored.size());

    int factor = 1;
    int width = coarsest.width;
    int height = coarsest.height;
    while (std::max(width, height) > kBuiltTopLevelMaxSide) {
        factor *= 2;
        width = (coarsest.width + factor - 1) / factor;
        height = (coarsest.height + factor - 1) / factor;

        LevelInfo lvl;
        lvl.level = nextIndex++;
        lvl.width = width;
        lvl.height = height;
        // Same rule as SlideIOAdapter: the smaller of the two axis ratios.
        double scale = 0.0;
        if (info.width > 0 && info.height > 0) {
            scale = std::min(static_cast<double>(width) / info.width,
                             static_cast<double>(height) / info.height);
        }
        lvl.scale = scale > 0.0 ? scale : 1.0;
        lvl.tileWidth = std::min(tileW, width);
        lvl.tileHeight = std::min(tileH, height);
        lvl.tilesX = (width + lvl.tileWidth - 1) / lvl.tileWidth;
        lvl.tilesY = (height + lvl.tileHeight - 1) / lvl.tileHeight;
        lvl.nativelyTiled = true;
        lvl.built = true;
        lvl.builtFactor = factor;
        plan.levels.push_back(lvl);
    }

    // Resident from the coarsest upward while the running total fits.
    const size_t pixelBytes =
        static_cast<size_t>(std::max(info.numChannels, 0)) * dataTypeSize(info.channelDataType);
    size_t total = 0;
    bool anyResident = false;
    for (auto it = plan.levels.rbegin(); it != plan.levels.rend(); ++it) {
        total += static_cast<size_t>(it->width) * static_cast<size_t>(it->height) * pixelBytes;
        if (total > budgetBytes) {
            break;
        }
        it->resident = true;
        anyResident = true;
    }
    if (!anyResident) {
        plan.levels.clear();
        return plan;
    }
    plan.sourceLevel = source;
    return plan;
}

TileSlideRect tileSlideRect(const LevelInfo& level, int column, int row, int slideWidth, int slideHeight)
{
    TileSlideRect r;
    if (column < 0 || column >= level.tilesX || row < 0 || row >= level.tilesY) {
        return r;
    }
    r.levelX = column * level.tileWidth;
    r.levelY = row * level.tileHeight;
    r.tileW = std::min(level.tileWidth, level.width - r.levelX);
    r.tileH = std::min(level.tileHeight, level.height - r.levelY);
    if (r.tileW <= 0 || r.tileH <= 0) {
        return r;
    }
    const double invScale = (level.scale > 0.0) ? (1.0 / level.scale) : 1.0;
    r.slideX = static_cast<int>(std::round(r.levelX * invScale));
    r.slideY = static_cast<int>(std::round(r.levelY * invScale));
    const int right = static_cast<int>(std::round((r.levelX + r.tileW) * invScale));
    const int bottom = static_cast<int>(std::round((r.levelY + r.tileH) * invScale));
    r.slideW = std::min(right - r.slideX, slideWidth - r.slideX);
    r.slideH = std::min(bottom - r.slideY, slideHeight - r.slideY);
    r.valid = r.slideW > 0 && r.slideH > 0;
    return r;
}

std::vector<TileKey> requestableTiles(const std::vector<TileKey>& keys,
                                      const std::function<bool(const TileKey&)>& isReady)
{
    std::vector<TileKey> out;
    for (const TileKey& key : keys) {
        if (isReady(key)) {
            out.push_back(key);
        }
    }
    return out;
}

} // namespace slideio::viewer::core
