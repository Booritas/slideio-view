#pragma once

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

class SlideIOAdapterPool;

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
    TileLoadScheduler(std::shared_ptr<SlideIOAdapterPool> adapterPool,
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

    std::shared_ptr<SlideIOAdapterPool> m_adapterPool;
    std::shared_ptr<core::ITileCache> m_tileCache;
    std::function<void(const core::TileKey&)> m_onTileLoaded;

    std::priority_queue<TileRequest, std::vector<TileRequest>, PriorityCompare> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stopping;
};

} // namespace slideio::viewer::infra
