#include "slideio/viewer/infra/BuiltPyramidSource.h"

#include "slideio/viewer/core/BuiltLevelAccumulator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
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

// The reader threads of one build and the ring of slots they park chunks in.
// Readers claim chunk indices in order, at most `window` ahead of the next one
// to feed, and read them through `read`, which touches only the inner source.
// The worker takes them back strictly in order. Destroying this stops the
// readers and joins them -- on every exit path of run(), exceptions included,
// since it lives on run()'s stack.
class ChunkReaders
{
public:
    using ReadFn = std::function<bool(int chunk, core::TileData& tile)>;

    ChunkReaders(int total, int window, ReadFn read, std::function<bool()> stopReading)
        : m_total(total)
        , m_window(std::max(1, window))
        , m_read(std::move(read))
        , m_stopReading(std::move(stopReading))
        , m_slots(static_cast<size_t>(m_window))
    {
    }

    ~ChunkReaders()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (std::thread& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    ChunkReaders(const ChunkReaders&) = delete;
    ChunkReaders& operator=(const ChunkReaders&) = delete;

    // May throw if a thread cannot be created; those already running are
    // joined by the destructor.
    void spawn(int count)
    {
        m_threads.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_active;
            }
            try {
                m_threads.emplace_back(&ChunkReaders::loop, this);
            } catch (...) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    --m_active;
                }
                m_cv.notify_all();
                throw;
            }
        }
    }

    // Waits for `chunk`, the next one to feed. False when every reader has
    // stopped without reading it: a cancel, an unreliable source level, or a
    // reader that failed outside its read.
    bool take(int chunk, core::TileData& tile, bool& ok)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        Slot& slot = m_slots[static_cast<size_t>(chunk % m_window)];
        m_cv.wait(lock, [this, &slot] { return slot.ready || m_active == 0; });
        if (!slot.ready) {
            return false;
        }
        tile = std::move(slot.tile);
        ok = slot.ok;
        slot = Slot{};
        m_fed = chunk + 1;
        lock.unlock();
        m_cv.notify_all(); // the window moved on
        return true;
    }

private:
    struct Slot
    {
        bool ready = false;
        bool ok = false;
        core::TileData tile;
    };

    void loop()
    {
        try {
            for (;;) {
                int chunk = 0;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this] { return m_stop || m_next >= m_total || m_next < m_fed + m_window; });
                    if (m_stop || m_next >= m_total || m_stopReading()) {
                        break;
                    }
                    chunk = m_next++;
                }
                core::TileData tile;
                const bool ok = m_read(chunk, tile);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    Slot& slot = m_slots[static_cast<size_t>(chunk % m_window)];
                    slot.tile = std::move(tile);
                    slot.ok = ok;
                    slot.ready = true;
                }
                m_cv.notify_all();
            }
        } catch (...) {
            // m_read never throws; this is a failure of the machinery itself.
            // A reader that dies may hold a claimed chunk that will never be
            // filled; the others would then fill the window and block forever.
            // Stop them all, so take() returns false and the worker ends the
            // build with error tiles instead of hanging.
            spdlog::error("BuiltPyramidSource: a built-level reader stopped");
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stop = true;
            }
            m_cv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_active;
        }
        m_cv.notify_all();
    }

    const int m_total;
    const int m_window;
    const ReadFn m_read;
    const std::function<bool()> m_stopReading;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<Slot> m_slots;
    int m_next = 0;   // the next chunk to claim
    int m_fed = 0;    // chunks taken by the worker
    int m_active = 0; // readers not yet stopped
    bool m_stop = false;
    std::vector<std::thread> m_threads;
};

} // namespace

int BuiltPyramidSource::defaultBuiltPassReaders()
{
    return builtPassReadersForThreads(std::thread::hardware_concurrency());
}

int BuiltPyramidSource::builtPassReadersForThreads(unsigned hardwareThreads)
{
    // SlideIO's ContextPool::defaultMax(): min(8, threads), 1 when unknown.
    const int handles = hardwareThreads == 0 ? 1 : static_cast<int>(std::min(8u, hardwareThreads));
    return std::max(1, (handles - 1) / 2);
}

BuiltPyramidSource::BuiltPyramidSource(std::shared_ptr<core::ISlideSource> inner, size_t budgetBytes,
                                       int bandChunkMaxBytes, int readers)
    : m_inner(std::move(inner))
    , m_bandChunkMaxBytes(std::max(1, bandChunkMaxBytes))
    , m_readers(std::max(1, readers))
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
    // No sourceUnreliable() test here, deliberately. The flag abandons a build
    // still reading the source; levels that FINISHED -- built in full, or
    // loaded from the cache, which only ever holds a build with no failed read
    // -- came from reads that all succeeded, and their tiles keep being served
    // as Ready. Withholding their overview would leave the minimap disagreeing
    // with the slide. An abandoned build never completes, so it is withheld below.
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
        // The accumulator allocates its rasters on the first chunk, so a cache
        // hit never holds a zero-filled set beside the loaded one. A build that
        // cannot allocate them stops in run() and completes with every resident
        // tile an error.
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

        const int totalChunks = bands * chunksPerBand;
        // One reader for a level decoded whole on every read: SlideIO runs such
        // reads one at a time per file, so more readers would only queue more
        // whole-level decodes ahead of the visible tiles and of a cancel.
        const int readers = std::max(1, std::min(core::builtPassReadersFor(source, m_readers), totalChunks));
        spdlog::info("BuiltPyramidSource: building {} resident of {} built levels from level {} ({}x{}) in mode {}: "
                     "{} bands of {} chunks, each up to {}x{} px, with {} reader(s)",
                     m_residentFactors.size(), m_levels.size() - static_cast<size_t>(m_storedCount), m_sourceLevel,
                     source.width, source.height, modeName(mode), bands, chunksPerBand, chunkWidth, bandHeight,
                     readers);
        const auto started = std::chrono::steady_clock::now();
        std::atomic<bool> warned{false};
        long long chunksRead = 0;

        // Chunk k's place, band-major and left to right: the one arithmetic
        // both the readers and the feeding loop below use.
        struct Chunk
        {
            int x, y, w, h;
            int slideX, slideY, slideW, slideH;
            bool lastOfBand;
        };
        const auto chunkAt = [&](int k) {
            Chunk c{};
            const int band = k / chunksPerBand;
            const int column = k % chunksPerBand;
            c.y = band * bandHeight;
            c.h = std::min(bandHeight, source.height - c.y);
            c.x = column * chunkWidth;
            c.w = std::min(chunkWidth, source.width - c.x);
            c.slideX = static_cast<int>(std::round(c.x * invScale));
            c.slideY = static_cast<int>(std::round(c.y * invScale));
            c.slideW = std::min(static_cast<int>(std::round((c.x + c.w) * invScale)), m_slideWidth) - c.slideX;
            c.slideH = std::min(static_cast<int>(std::round((c.y + c.h) * invScale)), m_slideHeight) - c.slideY;
            c.lastOfBand = column == chunksPerBand - 1;
            return c;
        };
        // Reads one chunk unlocked -- nobody may wait behind a SlideIO call --
        // and touches only the inner source: no accumulator, no state lock, no
        // listener, so it can run on a reader thread. Never throws.
        const auto readChunk = [&](int k, core::TileData& tile) {
            const Chunk c = chunkAt(k);
            if (c.slideW <= 0 || c.slideH <= 0) {
                return false;
            }
            try {
                tile = m_inner->readBlock(c.slideX, c.slideY, c.slideW, c.slideH, c.w, c.h, 0, 0, mode);
                return !tile.isError() && !tile.isPending() && tile.width() == c.w && tile.height() == c.h
                    && tile.byteSize() == static_cast<size_t>(c.w) * static_cast<size_t>(c.h) * pixelBytes;
            } catch (const std::exception& e) {
                if (!warned.exchange(true)) {
                    spdlog::warn("BuiltPyramidSource: band read at ({}, {}) failed: {}", c.x, c.y, e.what());
                }
            } catch (...) {
                if (!warned.exchange(true)) {
                    spdlog::warn("BuiltPyramidSource: band read at ({}, {}) failed", c.x, c.y);
                }
            }
            return false;
        };

        // One reader reads inline, exactly the sequential pass. More run as
        // threads owned by this frame: leaving it on any path, a throw
        // included, stops and joins them before run() returns.
        std::optional<ChunkReaders> pool;
        if (readers > 1) {
            pool.emplace(totalChunks, 2 * readers, readChunk,
                         [this] { return m_cancel.load() || sourceUnreliable(); });
            try {
                pool->spawn(readers);
            } catch (const std::exception& e) {
                // The readers are a speed-up, not a way for the build to fail:
                // read on this thread instead, as the sequential pass did.
                spdlog::warn("BuiltPyramidSource: could not start {} built-level readers ({}); reading on the "
                             "build thread",
                             readers, e.what());
                pool.reset();
            }
        }
        // An abandoned build says it is over; join the readers first, so no
        // read of the distrusted level is still running once it has.
        const auto abandonBuild = [&](int y) {
            pool.reset();
            abandon(y);
        };

        for (int k = 0; k < totalChunks; ++k) {
            const Chunk c = chunkAt(k);
            if (m_cancel) {
                spdlog::debug("BuiltPyramidSource: build in mode {} cancelled at row {}", modeName(mode), c.y);
                return;
            }
            if (sourceUnreliable()) {
                abandonBuild(c.y);
                return;
            }
            core::TileData tile;
            bool ok = false;
            if (!pool) {
                ok = readChunk(k, tile);
            } else if (!pool->take(k, tile, ok)) {
                // The readers stopped short of this chunk: for a cancel or an
                // unreliable source level, or on a failure of their own.
                if (m_cancel) {
                    spdlog::debug("BuiltPyramidSource: build in mode {} cancelled at row {}", modeName(mode), c.y);
                    return;
                }
                if (sourceUnreliable()) {
                    abandonBuild(c.y);
                    return;
                }
                // Their own failure is not the build's: join them, drop what they
                // read ahead, and read this chunk and the rest on this thread.
                spdlog::warn("BuiltPyramidSource: built-level readers stopped at chunk {} of {}; reading the rest "
                             "on the build thread",
                             k, totalChunks);
                pool.reset();
                ok = readChunk(k, tile);
            }
            ++chunksRead;
            if (sourceUnreliable()) {
                // Marked while this chunk was in flight: its pixels came
                // from the distrusted level, so they are not averaged in.
                abandonBuild(c.y);
                return;
            }

            {
                std::unique_lock<std::shared_mutex> state(m_stateMutex);
                if (m_generation != generation) {
                    return; // a newer build owns the accumulator
                }
                if (ok) {
                    m_accumulator->addChunk(c.x, c.y, c.w, c.h, tile.buffer().data());
                } else {
                    m_accumulator->failChunk(c.x, c.y, c.w, c.h);
                }
            }
            if (c.lastOfBand) {
                if (m_cancel) {
                    spdlog::debug("BuiltPyramidSource: build in mode {} cancelled at row {}", modeName(mode),
                                  c.y + c.h);
                    return;
                }
                notify(false);
            }
        }
        pool.reset(); // every reader has stopped: nothing is left to claim

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
        levels = m_store->load(key, sizes, pixelBytes, m_cancel);
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
        // A load cut short by a cancel is not a miss: there is nothing to build for.
        return m_cancel.load();
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
        views.push_back(core::BuiltLevelView{m_accumulator->levelWidth(i), m_accumulator->levelHeight(i),
                                             bytes.data(), bytes.size()});
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
        // Debug: the store already logged a failure, and a cancel is not one.
        spdlog::debug("BuiltPyramidSource: built levels in mode {} not saved to cache {} ({}){}", modeName(mode), name,
                     m_cacheDescription.path, m_cancel ? ": cancelled" : "");
    }
}

} // namespace slideio::viewer::infra
