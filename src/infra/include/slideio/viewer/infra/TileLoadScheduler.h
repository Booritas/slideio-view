#pragma once

#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/TileKey.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace slideio::viewer::infra
{

enum class TilePriority
{
    Visible = 0,
    Prefetch = 1,
    Background = 2
};

struct TileRequest
{
    core::TileKey key;
    TilePriority priority;
};

class TileLoadScheduler
{
public:
    // The slide source is shared, not owned exclusively: since SlideIO 2.10 a
    // scene's block reads are safe to call from several threads at once, so every
    // worker reads through this one source. Keep it alive until stop() returns --
    // SlideIO does not make a close wait for in-flight reads.
    TileLoadScheduler(std::shared_ptr<core::ISlideSource> slideSource,
                      std::shared_ptr<core::ITileCache> tileCache,
                      int numWorkers = 0);
    ~TileLoadScheduler();

    TileLoadScheduler(const TileLoadScheduler&) = delete;
    TileLoadScheduler& operator=(const TileLoadScheduler&) = delete;

    void requestTiles(const std::vector<TileRequest>& requests);
    void cancelAll();
    void cancelLevel(int level);
    void stop();

    void setOnTileLoaded(std::function<void(const core::TileKey&)> callback);

private:
    struct PriorityCompare
    {
        bool operator()(const TileRequest& a, const TileRequest& b) const
        {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        }
    };

    void workerLoop();

    std::shared_ptr<core::ISlideSource> m_slideSource;
    std::shared_ptr<core::ITileCache> m_tileCache;
    std::function<void(const core::TileKey&)> m_onTileLoaded;

    std::priority_queue<TileRequest, std::vector<TileRequest>, PriorityCompare> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stopping;
};

} // namespace slideio::viewer::infra
