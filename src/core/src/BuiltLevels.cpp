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

bool hasBuiltLevels(const std::vector<LevelInfo>& levels)
{
    return std::any_of(levels.begin(), levels.end(), [](const LevelInfo& l) { return l.built; });
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

namespace
{

bool isResidentBuilt(const std::vector<LevelInfo>& levels, int level)
{
    return level >= 0 && level < static_cast<int>(levels.size()) && levels[static_cast<size_t>(level)].built
        && levels[static_cast<size_t>(level)].resident;
}

void collectReadyChildren(const std::vector<LevelInfo>& levels, const TileKey& key,
                          const std::function<bool(const TileKey&)>& isReady, std::vector<TileKey>& out)
{
    const int finer = key.level() - 1;
    if (!isResidentBuilt(levels, finer)) {
        return; // the finest resident level: a not-ready tile here is dropped
    }
    const LevelInfo& child = levels[static_cast<size_t>(finer)];
    for (int dy = 0; dy < 2; ++dy) {
        const int row = 2 * key.row() + dy;
        if (row >= child.tilesY) {
            continue;
        }
        for (int dx = 0; dx < 2; ++dx) {
            const int column = 2 * key.column() + dx;
            if (column >= child.tilesX) {
                continue;
            }
            const TileKey childKey(finer, column, row, key.zIndex(), key.tFrame(), key.colorMode());
            if (isReady(childKey)) {
                out.push_back(childKey);
            } else {
                collectReadyChildren(levels, childKey, isReady, out);
            }
        }
    }
}

} // namespace

std::vector<TileKey> readyStandIns(const std::vector<LevelInfo>& levels, const TileKey& key,
                                   const std::function<bool(const TileKey&)>& isReady)
{
    if (key.level() < 0 || key.level() >= static_cast<int>(levels.size())) {
        return {};
    }
    if (!isResidentBuilt(levels, key.level()) || isReady(key)) {
        return {key};
    }
    std::vector<TileKey> out;
    collectReadyChildren(levels, key, isReady, out);
    return out;
}

} // namespace slideio::viewer::core
