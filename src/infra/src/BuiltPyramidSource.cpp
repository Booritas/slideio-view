#include "slideio/viewer/infra/BuiltPyramidSource.h"

#include "slideio/viewer/core/BuiltLevelAccumulator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
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
}

BuiltPyramidSource::~BuiltPyramidSource()
{
    std::lock_guard<std::mutex> control(m_controlMutex);
    stopWorkerLocked();
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
    std::shared_lock<std::shared_mutex> state(m_stateMutex);
    if (!m_complete || !m_accumulator) {
        return {};
    }
    const int coarsest = static_cast<int>(m_residentFactors.size()) - 1;
    return m_accumulator->extract(coarsest, 0, 0, m_accumulator->levelWidth(coarsest),
                                  m_accumulator->levelHeight(coarsest));
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
    if (!m_accumulator) {
        // Before start(), or after a build whose rasters could not be allocated.
        return m_complete ? ResidentAnswer::Failed : ResidentAnswer::Pending;
    }
    if (key.colorMode() != m_buildMode) {
        return ResidentAnswer::Pending; // never served from the other rendition
    }
    switch (m_accumulator->regionState(residentIndexOf(key.level()), rect.levelY, rect.levelY + rect.tileH)) {
    case core::BuiltRegionState::Ready:
        return ResidentAnswer::Ready;
    case core::BuiltRegionState::Failed:
        return ResidentAnswer::Failed;
    case core::BuiltRegionState::Pending:
        break;
    }
    return ResidentAnswer::Pending;
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
    if (!hasBuiltLevels()) {
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
    m_inner->setColorMode(mode);
    if (!hasBuiltLevels()) {
        return;
    }
    std::lock_guard<std::mutex> control(m_controlMutex);
    {
        std::shared_lock<std::shared_mutex> state(m_stateMutex);
        if (mode == m_buildMode) {
            return;
        }
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
        // Free the old rasters before allocating new ones: never two builds' worth.
        m_accumulator.reset();
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
    for (const auto& listener : listeners) {
        listener(complete);
    }
}

void BuiltPyramidSource::run(uint64_t generation, core::ColorMode mode)
{
    try {
        const core::LevelInfo& source = m_levels[static_cast<size_t>(m_sourceLevel)];
        const int tileHeight = source.tileHeight > 0 ? source.tileHeight : 256;
        const int bandHeight = m_residentFactors.front() * tileHeight;
        const size_t pixelBytes = static_cast<size_t>(m_numChannels) * core::dataTypeSize(m_dataType);
        const size_t bandRowBytes = static_cast<size_t>(bandHeight) * pixelBytes;
        const int chunkWidth = static_cast<int>(std::clamp<size_t>(
            static_cast<size_t>(m_bandChunkMaxBytes) / bandRowBytes, 1, static_cast<size_t>(source.width)));
        // Level 0's coordinates are slide coordinates; a coarser source level is
        // scaled back to the slide by the same rounding tileSlideRect uses.
        const double invScale = (m_sourceLevel == 0 || source.scale <= 0.0) ? 1.0 : 1.0 / source.scale;

        spdlog::info("BuiltPyramidSource: building {} levels from level {} ({}x{}) in mode {}",
                     m_residentFactors.size(), m_sourceLevel, source.width, source.height, modeName(mode));
        const auto started = std::chrono::steady_clock::now();
        bool warned = false;

        for (int y = 0; y < source.height; y += bandHeight) {
            const int h = std::min(bandHeight, source.height - y);
            for (int x = 0; x < source.width; x += chunkWidth) {
                if (m_cancel) {
                    spdlog::debug("BuiltPyramidSource: build in mode {} cancelled at row {}", modeName(mode), y);
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
        spdlog::info("BuiltPyramidSource: built levels in mode {} complete in {} ms", modeName(mode),
                     elapsed.count());
        if (!m_cancel) {
            notify(true);
        }
    } catch (const std::exception& e) {
        // Only a contract violation fed to the accumulator lands here; nothing
        // may escape a std::thread.
        spdlog::error("BuiltPyramidSource: build in mode {} stopped: {}", modeName(mode), e.what());
    }
}

} // namespace slideio::viewer::infra
