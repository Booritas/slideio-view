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
class BuiltPyramidSource : public core::ISlideSource
{
public:
    // Plans the built levels; does not start the worker.
    explicit BuiltPyramidSource(std::shared_ptr<core::ISlideSource> inner,
                                size_t budgetBytes = core::kBuiltLevelBudgetBytes,
                                int bandChunkMaxBytes = 64 * 1024 * 1024);
    ~BuiltPyramidSource() override;

    BuiltPyramidSource(const BuiltPyramidSource&) = delete;
    BuiltPyramidSource& operator=(const BuiltPyramidSource&) = delete;

    // Starts the build in the inner source's current colour mode, if there is
    // anything resident to build. Idempotent. Kept apart from the constructor
    // so listeners can be registered before the first band lands.
    void start();

    bool hasBuiltLevels() const;

    // The coarsest resident level's whole raster once the build is complete,
    // else an empty tile.
    core::TileData smallestBuiltLevel() const;

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
};

} // namespace slideio::viewer::infra
