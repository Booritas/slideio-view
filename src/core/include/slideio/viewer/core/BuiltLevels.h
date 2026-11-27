#pragma once

#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace slideio::viewer::core
{

/// Building stops at the first level no longer than this on its longer side.
constexpr int kBuiltTopLevelMaxSide = 2048;

/// Memory the resident built levels may occupy in total.
constexpr size_t kBuiltLevelBudgetBytes = 256ull * 1024ull * 1024ull;

/// The levels to append to a slide's stored ones.
struct BuiltLevelPlan
{
    /// Index of the stored level the built ones are derived from, or -1.
    int sourceLevel = -1;
    /// Only the appended levels, finest to coarsest, indices continuing after
    /// the stored ones. Empty means nothing to build.
    std::vector<LevelInfo> levels;
};

/// Plans levels of 1/2, 1/4, ... the coarsest stored level until one fits in
/// kBuiltTopLevelMaxSide. The coarsest levels that fit the budget are marked
/// resident; when not even the coarsest does, or the slide is a Z-stack or a
/// T-series, the plan is empty.
BuiltLevelPlan planBuiltLevels(const std::vector<LevelInfo>& stored, const SlideInfo& info,
                               size_t budgetBytes = kBuiltLevelBudgetBytes, int defaultTileSize = 256);

/// Index of the last level that is stored in the file, or -1.
int coarsestStoredLevelIndex(const std::vector<LevelInfo>& levels);

/// True when any level is one the viewer builds rather than the file stores.
bool hasBuiltLevels(const std::vector<LevelInfo>& levels);

/// One tile expressed in level pixels and in slide pixels.
struct TileSlideRect
{
    int levelX = 0;
    int levelY = 0;
    int tileW = 0;
    int tileH = 0;
    int slideX = 0;
    int slideY = 0;
    int slideW = 0;
    int slideH = 0;
    bool valid = false;
};

/// The arithmetic SlideIOAdapter::readTile uses: clipped tile size, level
/// pixels converted by 1/scale and rounded, then clamped to the slide.
TileSlideRect tileSlideRect(const LevelInfo& level, int column, int row, int slideWidth, int slideHeight);

/// The ready keys that stand in for `key` while it is not ready (§5.8 of the
/// built-pyramid-levels design). Returns {key} when `key` is ready or its level
/// is not a resident built level -- stored and virtual levels are always ready,
/// and descending into them would cost full-resolution decodes. Otherwise
/// `key` is replaced by its 2x2 children at the next finer level, clipped at
/// that level's right and bottom edge, provided that level is resident and
/// built: a ready child is emitted, a not-ready one is descended into, and a
/// not-ready tile at the finest resident level is dropped. Depth-first,
/// children in row-major order; every key keeps `key`'s z, t and colour mode.
/// An out-of-range level yields nothing.
std::vector<TileKey> readyStandIns(const std::vector<LevelInfo>& levels, const TileKey& key,
                                   const std::function<bool(const TileKey&)>& isReady);

/// Whether reads of `level` can run in parallel to any effect. False for a
/// level the driver decodes whole on every read: one with no tile size of its
/// own (`nativelyTiled` false), or one reported as a single tile covering the
/// whole level, which is how SlideIO's GDAL driver marks a striped level. Such
/// reads are serialised per file (SlideIO's striped-decode mutex), so extra
/// readers only queue more whole-level decodes ahead of the visible tiles and
/// of a cancel.
bool levelReadsConcurrently(const LevelInfo& level);

/// The reader threads for the built-level pass over `source`: `requested`
/// when its reads can run in parallel, otherwise 1. Never below 1.
int builtPassReadersFor(const LevelInfo& source, int requested);

} // namespace slideio::viewer::core
