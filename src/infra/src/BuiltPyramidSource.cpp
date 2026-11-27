#include "slideio/viewer/infra/BuiltPyramidSource.h"

#include "slideio/viewer/core/BuiltLevelAccumulator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace slideio::viewer::infra
{

namespace
{

const char* modeName(core::ColorMode mode)
{
    return mode == core::ColorMode::Managed ? "Managed" : "Raw";
}

} // namespace

BuiltPyramidSource::BuiltPyramidSource(std::shared_ptr<core::ISlideSource> inner, size_t budgetBytes,
                                       int bandChunkMaxBytes)
    : m_inner(std::move(inner))
    , m_bandChunkMaxBytes(std::max(1, bandChunkMaxBytes))
{
    const core::SlideInfo info = m_inner->slideInfo();
    m_levels = m_inner->levels();
    m_storedCount = static_cast<int>(m_levels.size());
    m_slideWidth = info.width;
    m_slideHeight = info.height;
    m_numChannels = info.numChannels;
    m_dataType = info.channelDataType;
    m_buildMode = m_inner->colorMode();

    core::BuiltLevelPlan plan = core::planBuiltLevels(m_levels, info, budgetBytes);
    if (plan.levels.empty() || plan.sourceLevel < 0) {
        return;
    }
    // The accumulator would refuse these; refuse them here, before a level is
    // advertised that nothing could ever fill.
    if (m_numChannels <= 0 || core::dataTypeSize(m_dataType) == 0) {
        spdlog::warn("BuiltPyramidSource: no built levels for a slide of {} channel(s) of an unsized type",
                     m_numChannels);
        return;
    }
    m_sourceLevel = plan.sourceLevel;
    for (const core::LevelInfo& level : plan.levels) {
        if (level.resident) {
            m_residentFactors.push_back(level.builtFactor);
        }
        m_levels.push_back(level);
    }
    if (m_residentFactors.empty()) {
        return;
    }
    // The build reads the source level through readBlock, which has none of
    // readTile's routing around an unreliable level, so a level the inner
    // source distrusts must not be averaged in. Subscribed before start(): a
    // level marked earlier is reported at once. The listener only raises a
    // shared flag -- it runs on whichever thread read the bad tile, possibly
    // after this object is gone, so it may neither touch `this` nor join.
    auto flag = m_sourceUnreliable;
    const int sourceLevel = m_sourceLevel;
    m_inner->addOnLevelMarkedUnreliable([flag, sourceLevel](int level) {
        if (level != sourceLevel || flag->exchange(true)) {
            return;
        }
        spdlog::warn("BuiltPyramidSource: source level {} was marked unreliable; built levels abandoned, "
                     "their unbuilt tiles read as errors",
                     level);
    });
}

bool BuiltPyramidSource::sourceUnreliable() const
{
    return m_sourceUnreliable->load();
}

int BuiltPyramidSource::bandChunkWidth(int levelWidth, int tileWidth, size_t bytesPerColumn, size_t maxBytes)
{
    if (levelWidth <= 0) {
        return 0;
    }
    const size_t tile = static_cast<size_t>(tileWidth > 0 ? tileWidth : 256);
    const size_t tileBytes = std::max<size_t>(1, tile * std::max<size_t>(1, bytesPerColumn));
    const size_t tiles = std::max<size_t>(1, maxBytes / tileBytes);
    const size_t width = std::min(tiles * tile, static_cast<size_t>(levelWidth));
    return static_cast<int>(width);
}

BuiltPyramidSource::~BuiltPyramidSource()
{
    std::lock_guard<std::mutex> control(m_controlMutex);
    stopWorkerLocked();
}

void BuiltPyramidSource::setLevelStore(std::shared_ptr<core::IBuiltLevelStore> store,
                                       core::BuiltLevelCacheIdentity identity,
                                       core::BuiltLevelCacheDescription description)
{
    std::lock_guard<std::mutex> control(m_controlMutex);
    if (m_started) {
        // The worker reads these unlocked; changing them under it would race.
        spdlog::warn("BuiltPyramidSource: level store set after start() ignored for {}", description.path);
        return;
    }
    m_store = std::move(store);
    m_cacheIdentity = std::move(identity);
    m_cacheDescription = std::move(description);
}

void BuiltPyramidSource::start()
{
    std::lock_guard<std::mutex> control(m_controlMutex);
    if (!hasBuiltLevels() || m_started) {
        return;
    }
    m_started = true;
    launchLocked(m_inner->colorMode());
}

bool BuiltPyramidSource::hasBuiltLevels() const
{
    return !m_residentFactors.empty();
}

core::TileData BuiltPyramidSource::smallestBuiltLevel() const
{
    if (sourceUnreliable()) {
        return {};
    }
    std::shared_lock<std::shared_mutex> state(m_stateMutex);
    if (!m_complete || !m_accumulator) {
        return {};
    }
    const int coarsest = static_cast<int>(m_residentFactors.size()) - 1;
    const int height = m_accumulator->levelHeight(coarsest);
    // A failed row holds zeros, and so does a row a stopped build never
    // reached -- complete is set then too -- and the minimap would paint
    // either as a black stripe across the slide. Only a level whose every row
    // is Ready is an overview; no overview beats a wrong one.
    int unreadyRows = 0;
    for (int y = 0; y < height; ++y) {
        if (m_accumulator->regionState(coarsest, y, y + 1) != core::BuiltRegionState::Ready) {
            ++unreadyRows;
        }
    }
    if (unreadyRows > 0) {
        if (!m_withheldLogged.exchange(true)) {
            spdlog::info("BuiltPyramidSource: overview withheld because {} of {} rows of the smallest built "
                         "level failed or were never built",
                         unreadyRows, height);
        }
        return {};
    }
    return m_accumulator->extract(coarsest, 0, 0, m_accumulator->levelWidth(coarsest), height);
}

core::SlideInfo BuiltPyramidSource::slideInfo() const
{
    return m_inner->slideInfo();
}

std::vector<core::LevelInfo> BuiltPyramidSource::levels() const
{
    return m_levels;
}

int BuiltPyramidSource::residentIndexOf(int level) const
{
    int index = -1;
    for (int i = m_storedCount; i <= level; ++i) {
        if (m_levels[static_cast<size_t>(i)].resident) {
            ++index;
        }
    }
    return m_levels[static_cast<size_t>(level)].resident ? index : -1;
}

BuiltPyramidSource::ResidentAnswer BuiltPyramidSource::residentAnswerLocked(const core::TileKey& key,
                                                                           const core::TileSlideRect& rect) const
{
    // A pending tile of a build abandoned for an unreliable source level will
    // never be filled: answer it as failed so nothing waits on it.
    const ResidentAnswer pending = sourceUnreliable() ? ResidentAnswer::Failed : ResidentAnswer::Pending;
    if (!m_accumulator) {
        // Before start(), or after a build whose rasters could not be allocated.
        return m_complete ? ResidentAnswer::Failed : pending;
    }
    if (key.colorMode() != m_buildMode) {
        return pending; // never served from the other rendition
    }
    // The tile's own columns: a failed chunk elsewhere in its rows is not its failure.
    switch (m_accumulator->regionState(residentIndexOf(key.level()), rect.levelX, rect.levelY,
                                       rect.levelX + rect.tileW, rect.levelY + rect.tileH)) {
    case core::BuiltRegionState::Ready:
        return ResidentAnswer::Ready;
    case core::BuiltRegionState::Failed:
        return ResidentAnswer::Failed;
    case core::BuiltRegionState::Pending:
        break;
    }
    // A complete build left these rows unfilled only if it stopped on an
    // exception; they will never arrive either.
    return m_complete ? ResidentAnswer::Failed : pending;
}

core::TileData BuiltPyramidSource::readTile(const core::TileKey& key)
{
    const int level = key.level();
    if (level < 0 || level >= static_cast<int>(m_levels.size())) {
        return core::TileData::createError(0, 0);
    }
    if (level < m_storedCount) {
        return m_inner->readTile(key);
    }
    const core::LevelInfo& info = m_levels[static_cast<size_t>(level)];
    const core::TileSlideRect rect = core::tileSlideRect(info, key.column(), key.row(), m_slideWidth, m_slideHeight);
    if (!rect.valid) {
        return core::TileData::createError(info.tileWidth, info.tileHeight);
    }
    if (!info.resident) {
        return m_inner->readBlock(rect.slideX, rect.slideY, rect.slideW, rect.slideH, rect.tileW, rect.tileH,
                                  key.zIndex(), key.tFrame(), key.colorMode());
    }
    if (key.zIndex() != 0 || key.tFrame() != 0) {
        return core::TileData::createError(rect.tileW, rect.tileH);
    }

    std::shared_lock<std::shared_mutex> state(m_stateMutex);
    switch (residentAnswerLocked(key, rect)) {
    case ResidentAnswer::Pending:
        return core::TileData::createPending();
    case ResidentAnswer::Failed:
        return core::TileData::createError(rect.tileW, rect.tileH);
    case ResidentAnswer::Ready:
        break;
    }
    return m_accumulator->extract(residentIndexOf(level), rect.levelX, rect.levelY, rect.tileW, rect.tileH);
}

bool BuiltPyramidSource::isTileReady(const core::TileKey& key) const
{
    const int level = key.level();
    if (level < m_storedCount || level >= static_cast<int>(m_levels.size())) {
        return true;
    }
    const core::LevelInfo& info = m_levels[static_cast<size_t>(level)];
    if (!info.resident || key.zIndex() != 0 || key.tFrame() != 0) {
        return true;
    }
    const core::TileSlideRect rect = core::tileSlideRect(info, key.column(), key.row(), m_slideWidth, m_slideHeight);
    if (!rect.valid) {
        return true; // an error tile, answered at once
    }
    std::shared_lock<std::shared_mutex> state(m_stateMutex);
    return residentAnswerLocked(key, rect) != ResidentAnswer::Pending;
}

core::TileData BuiltPyramidSource::readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                                             int targetWidth, int targetHeight, int zIndex, int tFrame)
{
    return m_inner->readBlock(slideX, slideY, slideWidth, slideHeight, targetWidth, targetHeight, zIndex, tFrame);
}

core::TileData BuiltPyramidSource::readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                                             int targetWidth, int targetHeight, int zIndex, int tFrame,
                                             core::ColorMode mode)
{
    return m_inner->readBlock(slideX, slideY, slideWidth, slideHeight, targetWidth, targetHeight, zIndex, tFrame,
                              mode);
}

void BuiltPyramidSource::addOnBuiltRowsReady(std::function<void(bool complete)> listener)
{
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    m_listeners.push_back(std::move(listener));
}

bool BuiltPyramidSource::builtOverviewPending() const
{
    if (!hasBuiltLevels() || sourceUnreliable()) {
        return false;
    }
    std::shared_lock<std::shared_mutex> state(m_stateMutex);
    return !m_complete;
}

void BuiltPyramidSource::addOnLevelMarkedUnreliable(std::function<void(int)> listener)
{
    m_inner->addOnLevelMarkedUnreliable(std::move(listener));
}

void BuiltPyramidSource::setColorMode(core::ColorMode mode)
{
    if (!hasBuiltLevels()) {
        m_inner->setColorMode(mode);
        return;
    }
    std::lock_guard<std::mutex> control(m_controlMutex);
    bool differs = false;
    {
        std::shared_lock<std::shared_mutex> state(m_stateMutex);
        differs = mode != m_buildMode;
    }
    // Cancel before the inner source changes mode, so once anyone can observe
    // the new mode the old build reads no further chunk.
    if (differs) {
        m_cancel = true;
    }
    m_inner->setColorMode(mode);
    if (!differs) {
        return;
    }
    stopWorkerLocked();
    if (m_started) {
        launchLocked(mode);
        return;
    }
    // Not started: nothing is built yet, so only the mode moves.
    std::unique_lock<std::shared_mutex> state(m_stateMutex);
    ++m_generation;
    m_buildMode = mode;
}

core::ColorMode BuiltPyramidSource::colorMode() const
{
    return m_inner->colorMode();
}

core::ColorProfileInfo BuiltPyramidSource::activeColorProfileInfo() const
{
    return m_inner->activeColorProfileInfo();
}

void BuiltPyramidSource::stopWorkerLocked()
{
    m_cancel = true;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void BuiltPyramidSource::launchLocked(core::ColorMode mode)
{
    uint64_t generation = 0;
    {
        std::unique_lock<std::shared_mutex> state(m_stateMutex);
        generation = ++m_generation;
        m_buildMode = mode;
        m_complete = false;
        m_withheldLogged = false;
        // Free the old rasters before allocating new ones: never two builds' worth.
        m_accumulator.reset();
        if (sourceUnreliable()) {
            // Nothing to build from; every resident tile already reads as an error.
            spdlog::debug("BuiltPyramidSource: no build in mode {}: the source level is unreliable", modeName(mode));
            return;
        }
        const core::LevelInfo& source = m_levels[static_cast<size_t>(m_sourceLevel)];
        try {
            m_accumulator = std::make_unique<core::BuiltLevelAccumulator>(source.width, source.height, m_numChannels,
                                                                          m_dataType, m_residentFactors);
        } catch (const std::exception& e) {
            // Every resident tile reads as an error rather than staying pending forever.
            spdlog::error("BuiltPyramidSource: cannot hold the built levels: {}", e.what());
            m_complete = true;
            return;
        }
    }
    m_cancel = false;
    m_worker = std::thread(&BuiltPyramidSource::run, this, generation, mode);
}

void BuiltPyramidSource::notify(bool complete)
{
    std::vector<std::function<void(bool)>> listeners;
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        listeners = m_listeners;
    }
    // A throwing listener must neither stop the build nor, escaping the
    // worker's std::thread, terminate the application.
    for (const auto& listener : listeners) {
        try {
            listener(complete);
        } catch (const std::exception& e) {
            spdlog::warn("BuiltPyramidSource: a built-rows listener threw: {}", e.what());
        } catch (...) {
            spdlog::warn("BuiltPyramidSource: a built-rows listener threw");
        }
    }
}

void BuiltPyramidSource::run(uint64_t generation, core::ColorMode mode)
{
    // A build abandoned for an unreliable source level still reports that it
    // is over, so the UI requests its visible tiles again -- error tiles now --
    // and stops waiting for an overview.
    const auto abandon = [this, mode](int y) {
        spdlog::info("BuiltPyramidSource: build in mode {} abandoned at row {}: the source level is unreliable",
                     modeName(mode), y);
        if (!m_cancel) {
            notify(true);
        }
    };
    try {
        const core::LevelInfo& source = m_levels[static_cast<size_t>(m_sourceLevel)];
        const int tileHeight = source.tileHeight > 0 ? source.tileHeight : 256;
        const int bandHeight = m_residentFactors.front() * tileHeight;
        const size_t pixelBytes = static_cast<size_t>(m_numChannels) * core::dataTypeSize(m_dataType);
        const size_t bandColumnBytes = static_cast<size_t>(bandHeight) * pixelBytes;
        // Whole source tiles per chunk: a chunk edge inside a tile would decode
        // that tile twice, once for each chunk it straddles.
        const int chunkWidth = bandChunkWidth(source.width, source.tileWidth, bandColumnBytes,
                                              static_cast<size_t>(m_bandChunkMaxBytes));
        const int chunksPerBand = (source.width + chunkWidth - 1) / chunkWidth;
        const int bands = (source.height + bandHeight - 1) / bandHeight;
        // Level 0's coordinates are slide coordinates; a coarser source level is
        // scaled back to the slide by the same rounding tileSlideRect uses.
        const double invScale = (m_sourceLevel == 0 || source.scale <= 0.0) ? 1.0 : 1.0 / source.scale;

        // No key -- no store, or a Managed build with no profile fingerprint --
        // means neither a load nor a save: built exactly as without a store.
        std::optional<core::BuiltLevelCacheKey> cacheKey;
        if (m_store) {
            cacheKey = core::builtLevelCacheKeyFor(m_cacheIdentity, m_sourceLevel, source, m_numChannels, m_dataType,
                                                   m_residentFactors, mode);
        }
        if (cacheKey && loadFromStore(generation, mode, *cacheKey)) {
            return;
        }

        spdlog::info("BuiltPyramidSource: building {} resident of {} built levels from level {} ({}x{}) in mode {}: "
                     "{} bands of {} chunks, each up to {}x{} px",
                     m_residentFactors.size(), m_levels.size() - static_cast<size_t>(m_storedCount), m_sourceLevel,
                     source.width, source.height, modeName(mode), bands, chunksPerBand, chunkWidth, bandHeight);
        const auto started = std::chrono::steady_clock::now();
        bool warned = false;
        long long chunksRead = 0;

        for (int y = 0; y < source.height; y += bandHeight) {
            const int h = std::min(bandHeight, source.height - y);
            for (int x = 0; x < source.width; x += chunkWidth) {
                if (m_cancel) {
                    spdlog::debug("BuiltPyramidSource: build in mode {} cancelled at row {}", modeName(mode), y);
                    return;
                }
                if (sourceUnreliable()) {
                    abandon(y);
                    return;
                }
                const int w = std::min(chunkWidth, source.width - x);
                const int slideX = static_cast<int>(std::round(x * invScale));
                const int slideY = static_cast<int>(std::round(y * invScale));
                const int slideW = std::min(static_cast<int>(std::round((x + w) * invScale)), m_slideWidth) - slideX;
                const int slideH = std::min(static_cast<int>(std::round((y + h) * invScale)), m_slideHeight) - slideY;

                // Read unlocked: readers must not wait behind a SlideIO call.
                core::TileData tile;
                bool ok = false;
                if (slideW > 0 && slideH > 0) {
                    try {
                        tile = m_inner->readBlock(slideX, slideY, slideW, slideH, w, h, 0, 0, mode);
                        ok = !tile.isError() && !tile.isPending() && tile.width() == w && tile.height() == h
                            && tile.byteSize() == static_cast<size_t>(w) * static_cast<size_t>(h) * pixelBytes;
                    } catch (const std::exception& e) {
                        if (!warned) {
                            spdlog::warn("BuiltPyramidSource: band read at ({}, {}) failed: {}", x, y, e.what());
                            warned = true;
                        }
                    } catch (...) {
                        if (!warned) {
                            spdlog::warn("BuiltPyramidSource: band read at ({}, {}) failed", x, y);
                            warned = true;
                        }
                    }
                }
                ++chunksRead;
                if (sourceUnreliable()) {
                    // Marked while this chunk was in flight: its pixels came
                    // from the distrusted level, so they are not averaged in.
                    abandon(y);
                    return;
                }

                std::unique_lock<std::shared_mutex> state(m_stateMutex);
                if (m_generation != generation) {
                    return; // a newer build owns the accumulator
                }
                if (ok) {
                    m_accumulator->addChunk(x, y, w, h, tile.buffer().data());
                } else {
                    m_accumulator->failChunk(x, y, w, h);
                }
            }
            if (m_cancel) {
                spdlog::debug("BuiltPyramidSource: build in mode {} cancelled at row {}", modeName(mode), y + h);
                return;
            }
            notify(false);
        }

        {
            std::unique_lock<std::shared_mutex> state(m_stateMutex);
            if (m_generation != generation) {
                return;
            }
            m_complete = true;
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        spdlog::info("BuiltPyramidSource: built levels in mode {} complete in {} ms ({} chunks read)",
                     modeName(mode), elapsed.count(), chunksRead);
        if (!m_cancel) {
            notify(true);
        }
        if (cacheKey) {
            saveToStore(generation, mode, *cacheKey);
        }
        return;
    } catch (const std::exception& e) {
        // A contract violation fed to the accumulator, or an allocation failure
        // in its per-band buffers, lands here; nothing may escape a std::thread.
        spdlog::error("BuiltPyramidSource: build in mode {} stopped: {}", modeName(mode), e.what());
    } catch (...) {
        spdlog::error("BuiltPyramidSource: build in mode {} stopped", modeName(mode));
    }
    // A stopped build is over: complete, so builtOverviewPending() does not stay
    // true forever and the rows it never reached read as errors, not pending.
    try {
        {
            std::unique_lock<std::shared_mutex> state(m_stateMutex);
            if (m_generation != generation) {
                return;
            }
            m_complete = true;
        }
        if (!m_cancel) {
            notify(true);
        }
    } catch (...) {
        spdlog::error("BuiltPyramidSource: could not report the stopped build in mode {}", modeName(mode));
    }
}

bool BuiltPyramidSource::loadFromStore(uint64_t generation, core::ColorMode mode, const core::BuiltLevelCacheKey& key)
{
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::pair<int, int>> sizes;
    {
        std::shared_lock<std::shared_mutex> state(m_stateMutex);
        if (m_generation != generation || !m_accumulator) {
            return true; // a newer build owns the accumulator
        }
        for (int i = 0; i < static_cast<int>(m_residentFactors.size()); ++i) {
            sizes.emplace_back(m_accumulator->levelWidth(i), m_accumulator->levelHeight(i));
        }
    }
    const size_t pixelBytes = static_cast<size_t>(m_numChannels) * core::dataTypeSize(m_dataType);
    const std::string name = core::builtLevelCacheKeyName(key);

    // Disk I/O, unlocked: readers must not wait behind it. The store logs its
    // own miss; a throw is a miss too, never a reason to stop the build.
    std::optional<std::vector<std::vector<uint8_t>>> levels;
    try {
        levels = m_store->load(key, sizes, pixelBytes);
    } catch (const std::exception& e) {
        spdlog::warn("BuiltPyramidSource: level cache {} could not be read for {}: {}; building", name,
                     m_cacheDescription.path, e.what());
        return false;
    } catch (...) {
        spdlog::warn("BuiltPyramidSource: level cache {} could not be read for {}; building", name,
                     m_cacheDescription.path);
        return false;
    }
    if (!levels) {
        return false;
    }
    if (m_cancel) {
        return true; // superseded while reading; the next build owns the state
    }
    {
        std::unique_lock<std::shared_mutex> state(m_stateMutex);
        if (m_generation != generation) {
            return true;
        }
        try {
            m_accumulator->loadComplete(std::move(*levels));
        } catch (const std::exception& e) {
            // Touches nothing on a throw, so the build can still run.
            spdlog::warn("BuiltPyramidSource: level cache {} for {} answered unusable levels: {}; building", name,
                         m_cacheDescription.path, e.what());
            return false;
        }
        m_complete = true;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    spdlog::info("BuiltPyramidSource: loaded built levels in mode {} from cache {} in {} ms ({})", modeName(mode),
                 name, elapsed.count(), m_cacheDescription.path);
    if (!m_cancel) {
        notify(true);
    }
    return true;
}

void BuiltPyramidSource::saveToStore(uint64_t generation, core::ColorMode mode, const core::BuiltLevelCacheKey& key)
{
    if (m_cancel || sourceUnreliable()) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    const std::string name = core::builtLevelCacheKeyName(key);
    // Shared for the whole save: the views point into the accumulator. Readers
    // still get through, and nobody can take it exclusively meanwhile -- this
    // worker is the only writer while it runs, and a restart or the destructor
    // raises m_cancel (which the store polls) and joins this thread before it
    // takes the lock exclusively.
    std::shared_lock<std::shared_mutex> state(m_stateMutex);
    if (m_generation != generation || !m_accumulator || !m_complete) {
        return;
    }
    if (m_accumulator->hasFailures()) {
        spdlog::debug("BuiltPyramidSource: built levels in mode {} for {} have failed rows; not cached",
                      modeName(mode), m_cacheDescription.path);
        return;
    }
    std::vector<core::BuiltLevelView> views;
    for (int i = 0; i < static_cast<int>(m_residentFactors.size()); ++i) {
        const std::vector<uint8_t>& bytes = m_accumulator->levelBytes(i);
        views.push_back(
            core::BuiltLevelView{m_accumulator->levelWidth(i), m_accumulator->levelHeight(i), bytes.data(), bytes.size()});
    }
    bool saved = false;
    try {
        saved = m_store->save(key, m_cacheDescription, views, m_cancel);
    } catch (const std::exception& e) {
        spdlog::warn("BuiltPyramidSource: built levels in mode {} not saved to cache {} for {}: {}", modeName(mode),
                     name, m_cacheDescription.path, e.what());
        return;
    } catch (...) {
        spdlog::warn("BuiltPyramidSource: built levels in mode {} not saved to cache {} for {}", modeName(mode), name,
                     m_cacheDescription.path);
        return;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    if (saved) {
        spdlog::info("BuiltPyramidSource: saved built levels in mode {} to cache {} in {} ms ({})", modeName(mode),
                     name, elapsed.count(), m_cacheDescription.path);
    } else {
        spdlog::info("BuiltPyramidSource: built levels in mode {} not saved to cache {} ({}){}", modeName(mode), name,
                     m_cacheDescription.path, m_cancel ? ": cancelled" : "");
    }
}

} // namespace slideio::viewer::infra
