#pragma once

#include "slideio/viewer/core/BuiltLevels.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace slideio::viewer::core
{
class BuiltLevelAccumulator;
} // namespace slideio::viewer::core

namespace slideio::viewer::infra
{

// Wraps a slide source and appends viewer-built coarser levels to its pyramid
// (spec 2026-11-27 §5.3). Stored levels forward to the inner source. A virtual
// built level is one mode-explicit block read per tile. Resident built levels
// are filled by one background worker that reads the coarsest stored level
// once, band by band, in the colour mode the build belongs to; until a tile's
// rows are accumulated, reading it answers TileData::createPending().
//
// slideInfo() is the inner source's, unchanged: built levels must never look
// like levels of the file.
//
// Threading: the accumulator and build state sit behind a shared_mutex --
// exclusive while the worker feeds one chunk or a build restarts, shared for
// readers. The worker never holds it across an inner read and nobody holds it
// across a listener call. Listeners run on the worker thread and must not call
// setColorMode() or destroy this object from there (both join that thread).
//
// A band is read in chunks whose width is a whole number of the source level's
// tiles, as many as keep one chunk within bandChunkMaxBytes (at least one tile),
// so no source tile is decoded twice and the one read a close or a colour toggle
// waits for stays short.
//
// The source level marked unreliable by the inner source (a corrupt tile-offset
// table, found by whichever thread read the tile) abandons the build: the
// notification only raises a flag, the worker stops at its next chunk without
// accumulating the one in flight, and every resident tile not already built is
// answered as an error tile -- ready, so nothing waits on it. No overview is
// ever published for such a source.
class BuiltPyramidSource : public core::ISlideSource
{
public:
    static constexpr int kDefaultBandChunkMaxBytes = 4 * 1024 * 1024;

    // Plans the built levels and subscribes to the inner source's unreliable-
    // level notifications; does not start the worker.
    explicit BuiltPyramidSource(std::shared_ptr<core::ISlideSource> inner,
                                size_t budgetBytes = core::kBuiltLevelBudgetBytes,
                                int bandChunkMaxBytes = kDefaultBandChunkMaxBytes);
    ~BuiltPyramidSource() override;

    BuiltPyramidSource(const BuiltPyramidSource&) = delete;
    BuiltPyramidSource& operator=(const BuiltPyramidSource&) = delete;

    // Starts the build in the inner source's current colour mode, if there is
    // anything resident to build. Idempotent. Kept apart from the constructor
    // so listeners can be registered before the first band lands.
    void start();

    bool hasBuiltLevels() const;

    // The coarsest resident level's whole raster once the build is complete,
    // else an empty tile. Also empty when any of its rows failed -- a minimap
    // with black stripes is worse than none -- or when the source level was
    // marked unreliable.
    core::TileData smallestBuiltLevel() const;

    // The width of one band chunk for a source level of the given width and
    // tile width, at bytesPerColumn bytes per chunk column: the largest whole
    // number of tiles within maxBytes, at least one tile, never wider than the
    // level. Public so the rule can be tested directly.
    static int bandChunkWidth(int levelWidth, int tileWidth, size_t bytesPerColumn, size_t maxBytes);

    core::SlideInfo slideInfo() const override;
    std::vector<core::LevelInfo> levels() const override;
    core::TileData readTile(const core::TileKey& key) override;

    using core::ISlideSource::readBlock;
    core::TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight, int targetWidth,
                             int targetHeight, int zIndex = 0, int tFrame = 0) override;
    core::TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight, int targetWidth,
                             int targetHeight, int zIndex, int tFrame, core::ColorMode mode) override;

    bool isTileReady(const core::TileKey& key) const override;
    void addOnBuiltRowsReady(std::function<void(bool complete)> listener) override;
    bool builtOverviewPending() const override;
    void addOnLevelMarkedUnreliable(std::function<void(int)> listener) override;

    // Forwards; a mode other than the build's cancels the running build, joins
    // it, frees its rasters and -- if start() was called -- builds again in the
    // new mode. Waits for at most the one inner read the worker is inside.
    void setColorMode(core::ColorMode mode) override;
    core::ColorMode colorMode() const override;
    core::ColorProfileInfo activeColorProfileInfo() const override;

private:
    enum class ResidentAnswer
    {
        Pending,
        Failed,
        Ready
    };

    // Caller holds m_stateMutex (shared or exclusive).
    ResidentAnswer residentAnswerLocked(const core::TileKey& key, const core::TileSlideRect& rect) const;
    int residentIndexOf(int level) const;

    // Caller holds m_controlMutex and no worker is running.
    void launchLocked(core::ColorMode mode);
    // Caller holds m_controlMutex.
    void stopWorkerLocked();

    void run(uint64_t generation, core::ColorMode mode);
    void notify(bool complete);
    bool sourceUnreliable() const;

    std::shared_ptr<core::ISlideSource> m_inner;
    std::vector<core::LevelInfo> m_levels; // stored then built; fixed after construction
    int m_storedCount = 0;
    int m_sourceLevel = -1;
    std::vector<int> m_residentFactors; // finest to coarsest, relative to m_sourceLevel
    int m_slideWidth = 0;
    int m_slideHeight = 0;
    int m_numChannels = 0;
    core::DataType m_dataType = core::DataType::None;
    int m_bandChunkMaxBytes;

    mutable std::shared_mutex m_stateMutex;
    std::unique_ptr<core::BuiltLevelAccumulator> m_accumulator;
    core::ColorMode m_buildMode = core::ColorMode::Raw;
    bool m_complete = false;
    uint64_t m_generation = 0;

    std::mutex m_controlMutex; // start / setColorMode / destructor
    std::thread m_worker;
    bool m_started = false;
    std::atomic<bool> m_cancel{false};

    std::mutex m_listenersMutex;
    std::vector<std::function<void(bool)>> m_listeners;

    // Raised, from any thread, when the inner source marks the source level
    // unreliable. Shared with the listener registered on the inner source, which
    // cannot be unregistered and may outlive this object; the listener touches
    // nothing else, so it takes no lock and joins nothing.
    std::shared_ptr<std::atomic<bool>> m_sourceUnreliable = std::make_shared<std::atomic<bool>>(false);
    // One info line per build when the overview is withheld for failed rows.
    mutable std::atomic<bool> m_withheldLogged{false};
};

} // namespace slideio::viewer::infra
