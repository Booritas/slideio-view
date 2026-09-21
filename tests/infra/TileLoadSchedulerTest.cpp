#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <thread>

#include "slideio/viewer/infra/TileLoadScheduler.h"
#include "slideio/viewer/infra/LruTileCache.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"

using namespace slideio::viewer::core;
using namespace slideio::viewer::infra;

namespace
{

constexpr int kTileSize = 16;

// A slide source whose readTile() records how many calls are inside it at once,
// so a test can tell a genuinely concurrent scheduler from a serialised one.
// Every call also records its key, which is what proves nothing is dropped.
class RecordingSlideSource : public ISlideSource
{
public:
    SlideInfo slideInfo() const override { return SlideInfo{}; }
    std::vector<LevelInfo> levels() const override { return {}; }

    TileData readTile(const TileKey& key) override
    {
        const int now = ++m_inFlight;
        // Peak is monotonic, so a plain compare-exchange loop is enough.
        int observed = m_peakInFlight.load();
        while (now > observed && !m_peakInFlight.compare_exchange_weak(observed, now)) {
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_seen.insert(key);
        }

        // Hold the call open long enough that concurrent workers overlap here.
        // Without a delay a fast serial scheduler could finish each read before
        // the next begins and the peak would read 1 on a correct implementation.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        --m_inFlight;
        m_completed.fetch_add(1);
        m_completedCondition.notify_all();

        std::vector<uint8_t> buffer(static_cast<size_t>(kTileSize) * kTileSize * 3, 0);
        return TileData(std::move(buffer), kTileSize, kTileSize, 3, DataType::Byte);
    }

    TileData readBlock(int, int, int, int, int targetWidth, int targetHeight, int, int) override
    {
        std::vector<uint8_t> buffer(static_cast<size_t>(targetWidth) * targetHeight * 3, 0);
        return TileData(std::move(buffer), targetWidth, targetHeight, 3, DataType::Byte);
    }

    // Returns false on timeout rather than blocking the suite forever.
    bool waitForCompleted(int count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_completedMutex);
        return m_completedCondition.wait_for(lock, timeout,
                                             [this, count] { return m_completed.load() >= count; });
    }

    int peakInFlight() const { return m_peakInFlight.load(); }

    std::unordered_set<TileKey> seen() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_seen;
    }

private:
    std::atomic<int> m_inFlight{0};
    std::atomic<int> m_peakInFlight{0};
    std::atomic<int> m_completed{0};
    mutable std::mutex m_mutex;
    std::unordered_set<TileKey> m_seen;
    std::mutex m_completedMutex;
    std::condition_variable m_completedCondition;
};

// Counts tiles the scheduler has reported as loaded. onTileLoaded fires after
// the worker has inserted into the cache, so it -- not the return of readTile --
// is the signal that a tile is observable to the renderer.
class LoadedLatch
{
public:
    void notifyLoaded()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_count;
        }
        m_condition.notify_all();
    }

    // Returns false on timeout rather than blocking the suite forever.
    bool wait(int count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_condition.wait_for(lock, timeout, [this, count] { return m_count >= count; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    int m_count = 0;
};

std::vector<TileRequest> makeRequests(int count)
{
    std::vector<TileRequest> requests;
    requests.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        requests.push_back(TileRequest{TileKey{0, i, 0}, TilePriority::Visible});
    }
    return requests;
}

} // namespace

TEST_CASE("TileLoadScheduler reads one shared slide source concurrently", "[infra][TileLoadScheduler]")
{
    auto source = std::make_shared<RecordingSlideSource>();
    auto cache = std::make_shared<LruTileCache>(4 * 1024 * 1024);

    constexpr int kWorkers = 4;
    constexpr int kTiles = 16;

    TileLoadScheduler scheduler(source, cache, kWorkers);
    scheduler.requestTiles(makeRequests(kTiles));

    REQUIRE(source->waitForCompleted(kTiles, std::chrono::seconds(10)));

    // The point of dropping the adapter pool: several workers are inside the
    // same source at once. A pool of exclusively-loaned adapters, or any other
    // serialising gate, would pin this at 1.
    REQUIRE(source->peakInFlight() > 1);
}

TEST_CASE("TileLoadScheduler loads every requested tile exactly once", "[infra][TileLoadScheduler]")
{
    auto source = std::make_shared<RecordingSlideSource>();
    auto cache = std::make_shared<LruTileCache>(4 * 1024 * 1024);

    constexpr int kTiles = 12;

    LoadedLatch latch;
    TileLoadScheduler scheduler(source, cache, 4);
    scheduler.setOnTileLoaded([&latch](const TileKey&) { latch.notifyLoaded(); });
    scheduler.requestTiles(makeRequests(kTiles));

    REQUIRE(latch.wait(kTiles, std::chrono::seconds(10)));

    const auto seen = source->seen();
    REQUIRE(static_cast<int>(seen.size()) == kTiles);
    for (int i = 0; i < kTiles; ++i) {
        REQUIRE(seen.count(TileKey{0, i, 0}) == 1);
    }

    // Every tile reached the cache, so the renderer has something to draw.
    for (int i = 0; i < kTiles; ++i) {
        REQUIRE(cache->lookup(TileKey{0, i, 0}) != nullptr);
    }
}

TEST_CASE("TileLoadScheduler rejects a null slide source", "[infra][TileLoadScheduler]")
{
    auto cache = std::make_shared<LruTileCache>(1024 * 1024);
    REQUIRE_THROWS_AS(TileLoadScheduler(nullptr, cache, 2), std::invalid_argument);
}
