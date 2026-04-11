#include "slideio/viewer/infra/TileLoadScheduler.h"
#include "slideio/viewer/infra/SlideIOAdapterPool.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace slideio::viewer::infra
{

TileLoadScheduler::TileLoadScheduler(std::shared_ptr<SlideIOAdapterPool> adapterPool,
                                     std::shared_ptr<core::ITileCache> tileCache,
                                     int numWorkers)
    : m_adapterPool(std::move(adapterPool))
    , m_tileCache(std::move(tileCache))
    , m_stopping(false)
{
    if (!m_adapterPool) {
        throw std::invalid_argument("TileLoadScheduler: adapterPool must not be null");
    }
    if (!m_tileCache) {
        throw std::invalid_argument("TileLoadScheduler: tileCache must not be null");
    }

    if (numWorkers <= 0) {
        int hw = static_cast<int>(std::thread::hardware_concurrency());
        numWorkers = std::max(2, hw - 2);
    }

    spdlog::info("TileLoadScheduler: starting {} worker threads", numWorkers);

    m_workers.reserve(static_cast<size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i) {
        m_workers.emplace_back(&TileLoadScheduler::workerLoop, this);
    }
}

TileLoadScheduler::~TileLoadScheduler()
{
    stop();
}

void TileLoadScheduler::requestTiles(const std::vector<TileRequest>& requests)
{
    if (requests.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (const auto& req : requests) {
            m_queue.push(req);
        }
    }

    m_queueCondition.notify_all();

    spdlog::debug("TileLoadScheduler: enqueued {} tile requests, queue size: {}",
                  requests.size(), m_queue.size());
}

void TileLoadScheduler::cancelAll()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Clear the priority queue by swapping with empty
    std::priority_queue<TileRequest, std::vector<TileRequest>, PriorityCompare> empty;
    m_queue.swap(empty);

    spdlog::debug("TileLoadScheduler: cancelled all pending requests");
}

void TileLoadScheduler::cancelLevel(int level)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Rebuild the queue without entries matching the given level
    std::priority_queue<TileRequest, std::vector<TileRequest>, PriorityCompare> filtered;
    while (!m_queue.empty()) {
        TileRequest req = m_queue.top();
        m_queue.pop();
        if (req.key.level() != level) {
            filtered.push(req);
        }
    }
    m_queue.swap(filtered);

    spdlog::debug("TileLoadScheduler: cancelled all requests for level {}", level);
}

void TileLoadScheduler::stop()
{
    bool expected = false;
    if (!m_stopping.compare_exchange_strong(expected, true)) {
        return; // Already stopped or stopping
    }

    spdlog::info("TileLoadScheduler: stopping worker threads");

    m_queueCondition.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    spdlog::info("TileLoadScheduler: all worker threads stopped");
}

void TileLoadScheduler::setOnTileLoaded(std::function<void(const core::TileKey&)> callback)
{
    m_onTileLoaded = std::move(callback);
}

void TileLoadScheduler::workerLoop()
{
    spdlog::debug("TileLoadScheduler: worker thread started");

    while (!m_stopping.load()) {
        TileRequest request;
        bool gotRequest = false;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait(lock, [this] {
                return m_stopping.load() || !m_queue.empty();
            });

            if (m_stopping.load()) {
                break;
            }

            if (!m_queue.empty()) {
                request = m_queue.top();
                m_queue.pop();
                gotRequest = true;
            }
        }

        if (!gotRequest) {
            continue;
        }

        // Check if already in cache - skip if so
        auto cached = m_tileCache->lookup(request.key);
        if (cached) {
            spdlog::trace("TileLoadScheduler: tile {} already cached, skipping", request.key.toString());
            continue;
        }

        // Acquire adapter from pool (blocking)
        auto loan = m_adapterPool->acquire();

        // Check stopping flag again after potentially long wait
        if (m_stopping.load()) {
            break;
        }

        // Read the tile
        spdlog::trace("TileLoadScheduler: reading tile {}", request.key.toString());
        core::TileData tileData = loan->readTile(request.key);

        // Insert into cache (even error tiles, so we don't retry endlessly)
        auto tilePtr = std::make_shared<core::TileData>(std::move(tileData));
        m_tileCache->insert(request.key, tilePtr);

        // Notify callback
        if (m_onTileLoaded) {
            try {
                m_onTileLoaded(request.key);
            }
            catch (const std::exception& ex) {
                spdlog::warn("TileLoadScheduler: onTileLoaded callback threw: {}", ex.what());
            }
        }
    }

    spdlog::debug("TileLoadScheduler: worker thread exiting");
}

} // namespace slideio::viewer::infra
