#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/BuiltPyramidSource.h"
#include "slideio/viewer/core/BuiltLevels.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace slideio::viewer::core;
using namespace slideio::viewer::infra;

namespace
{

constexpr auto kWait = std::chrono::seconds(20);

uint8_t sourcePixel(int x, int y)
{
    return static_cast<uint8_t>((x * 7 + y * 3) & 0xFF);
}

struct BlockCall
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int tw = 0;
    int th = 0;
    int z = 0;
    int t = 0;
    ColorMode mode = ColorMode::Raw;
    bool liveOverload = false;
};

// One stored level, tile 256, one Byte channel, pixel (x*7 + y*3) & 0xFF.
// Records every readBlock; can block block reads on a latch and fail the band
// that starts at a given y.
class FakeSource : public ISlideSource
{
public:
    explicit FakeSource(int width = 4100, int height = 3000)
        : m_width(width)
        , m_height(height)
    {
        m_level.level = 0;
        m_level.width = width;
        m_level.height = height;
        m_level.scale = 1.0;
        m_level.tileWidth = 256;
        m_level.tileHeight = 256;
        m_level.tilesX = (width + 255) / 256;
        m_level.tilesY = (height + 255) / 256;
    }

    SlideInfo slideInfo() const override
    {
        SlideInfo info;
        info.width = m_width;
        info.height = m_height;
        info.numChannels = 1;
        info.channelDataType = DataType::Byte;
        info.numZoomLevels = 1;
        info.levels = {m_level};
        return info;
    }

    std::vector<LevelInfo> levels() const override { return {m_level}; }

    TileData readTile(const TileKey& key) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tileReads.push_back(key);
        }
        std::vector<uint8_t> buffer(16 * 16, static_cast<uint8_t>(42));
        return TileData(std::move(buffer), 16, 16, 1, DataType::Byte);
    }

    TileData readBlock(int x, int y, int w, int h, int tw, int th, int z, int t) override
    {
        return serve(x, y, w, h, tw, th, z, t, colorMode(), true);
    }

    TileData readBlock(int x, int y, int w, int h, int tw, int th, int z, int t, ColorMode mode) override
    {
        return serve(x, y, w, h, tw, th, z, t, mode, false);
    }

    void setColorMode(ColorMode mode) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mode = mode;
    }

    ColorMode colorMode() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mode;
    }

    void blockReads()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blocking = true;
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_blocking = false;
        }
        m_cv.notify_all();
    }

    bool waitUntilBlocked()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this] { return m_waiting > 0; });
    }

    void failBandAt(int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failY = y;
    }

    std::vector<BlockCall> calls() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_calls;
    }

    void clearCalls()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_calls.clear();
    }

    std::vector<TileKey> tileReads() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tileReads;
    }

private:
    TileData serve(int x, int y, int w, int h, int tw, int th, int z, int t, ColorMode mode, bool live)
    {
        bool fail = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_calls.push_back(BlockCall{x, y, w, h, tw, th, z, t, mode, live});
            fail = (y == m_failY);
            if (m_blocking) {
                ++m_waiting;
                m_cv.notify_all();
                m_cv.wait(lock, [this] { return !m_blocking; });
                --m_waiting;
            }
        }
        if (fail) {
            return TileData::createError(tw, th);
        }
        std::vector<uint8_t> buffer(static_cast<size_t>(tw) * static_cast<size_t>(th), 0);
        if (tw == w && th == h) {
            for (int j = 0; j < th; ++j) {
                for (int i = 0; i < tw; ++i) {
                    buffer[static_cast<size_t>(j) * tw + i] = sourcePixel(x + i, y + j);
                }
            }
        }
        return TileData(std::move(buffer), tw, th, 1, DataType::Byte);
    }

    int m_width;
    int m_height;
    LevelInfo m_level;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    ColorMode m_mode = ColorMode::Raw;
    bool m_blocking = false;
    int m_waiting = 0;
    int m_failY = -1;
    std::vector<BlockCall> m_calls;
    std::vector<TileKey> m_tileReads;
};

// Counts addOnBuiltRowsReady calls and lets a test wait for completions.
struct Progress
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<bool> events;
    int completes = 0;

    void attach(ISlideSource& source)
    {
        source.addOnBuiltRowsReady([this](bool complete) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                events.push_back(complete);
                if (complete) {
                    ++completes;
                }
            }
            cv.notify_all();
        });
    }

    bool waitForCompletes(int n)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, kWait, [this, n] { return completes >= n; });
    }

    size_t eventCount()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return events.size();
    }
};

// Level-1 (/2) pixel at (x, y), the rounded box average of the source.
uint8_t halfPixel(int x, int y, int sourceW, int sourceH)
{
    double sum = 0.0;
    int n = 0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const int sx = 2 * x + dx;
            const int sy = 2 * y + dy;
            if (sx < sourceW && sy < sourceH) {
                sum += sourcePixel(sx, sy);
                ++n;
            }
        }
    }
    return static_cast<uint8_t>(std::round(sum / n));
}

} // namespace

TEST_CASE("levels appends the plan and slideInfo keeps the stored levels", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);

    REQUIRE(source.hasBuiltLevels());
    const auto levels = source.levels();
    REQUIRE(levels.size() == 3);
    CHECK_FALSE(levels[0].built);
    CHECK(levels[1].built);
    CHECK(levels[1].resident);
    CHECK(levels[1].builtFactor == 2);
    CHECK(levels[1].width == 2050);
    CHECK(levels[1].height == 1500);
    CHECK(levels[2].built);
    CHECK(levels[2].resident);
    CHECK(levels[2].builtFactor == 4);
    CHECK(levels[2].width == 1025);
    CHECK(levels[2].height == 750);

    CHECK(source.slideInfo().levels.size() == 1);
    // Not started yet, so the overview is still to come.
    CHECK(source.builtOverviewPending());
    CHECK(source.smallestBuiltLevel().isEmpty());
}

TEST_CASE("reads the source level once, in bands, top to bottom, in the build's mode",
          "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    // 512-row bands of one byte per pixel: 1953 columns per chunk, three chunks a band.
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000);
    Progress progress;
    progress.attach(source);

    fake->setColorMode(ColorMode::Managed); // switched behind the wrapper's back, before start()
    source.start();
    REQUIRE(progress.waitForCompletes(1));
    CHECK_FALSE(source.builtOverviewPending());

    const auto calls = fake->calls();
    REQUIRE(calls.size() == 18); // 6 bands x 3 chunks
    std::vector<uint8_t> covered(static_cast<size_t>(4100) * 3000, 0);
    int lastY = 0;
    for (const BlockCall& c : calls) {
        CHECK_FALSE(c.liveOverload);
        CHECK(c.mode == ColorMode::Managed);
        CHECK(c.z == 0);
        CHECK(c.t == 0);
        CHECK(c.tw == c.w);
        CHECK(c.th == c.h);
        CHECK(c.y >= lastY);
        lastY = c.y;
        REQUIRE(c.x >= 0);
        REQUIRE(c.y >= 0);
        REQUIRE(c.x + c.w <= 4100);
        REQUIRE(c.y + c.h <= 3000);
        for (int y = c.y; y < c.y + c.h; ++y) {
            for (int x = c.x; x < c.x + c.w; ++x) {
                ++covered[static_cast<size_t>(y) * 4100 + x];
            }
        }
    }
    bool exactlyOnce = true;
    for (uint8_t v : covered) {
        exactlyOnce = exactlyOnce && v == 1;
    }
    CHECK(exactlyOnce);
}

TEST_CASE("a resident tile is pending before its band and exact after", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);

    fake->blockReads();
    source.start();
    REQUIRE(fake->waitUntilBlocked());

    const TileKey key(1, 1, 1);
    CHECK(source.readTile(key).isPending());
    CHECK_FALSE(source.isTileReady(key));
    CHECK(source.builtOverviewPending());
    CHECK(source.smallestBuiltLevel().isEmpty());

    fake->release();
    REQUIRE(progress.waitForCompletes(1));

    CHECK(source.isTileReady(key));
    const TileData tile = source.readTile(key);
    REQUIRE_FALSE(tile.isPending());
    REQUIRE_FALSE(tile.isError());
    REQUIRE(tile.width() == 256);
    REQUIRE(tile.height() == 256);
    REQUIRE(tile.numChannels() == 1);
    REQUIRE(tile.dataType() == DataType::Byte);
    bool exact = true;
    for (int j = 0; j < 256; ++j) {
        for (int i = 0; i < 256; ++i) {
            exact = exact && tile.buffer()[static_cast<size_t>(j) * 256 + i] == halfPixel(256 + i, 256 + j, 4100, 3000);
        }
    }
    CHECK(exact);

    const TileData smallest = source.smallestBuiltLevel();
    CHECK(smallest.width() == 1025);
    CHECK(smallest.height() == 750);
    CHECK(smallest.byteSize() == static_cast<size_t>(1025) * 750);
}

TEST_CASE("edge tile of a resident level is clipped", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    const LevelInfo level = source.levels()[1];
    const int col = level.tilesX - 1;
    const int row = level.tilesY - 1;
    const TileSlideRect rect = tileSlideRect(level, col, row, 4100, 3000);
    const TileData tile = source.readTile(TileKey(1, col, row));
    REQUIRE_FALSE(tile.isError());
    CHECK(tile.width() == rect.tileW);
    CHECK(tile.height() == rect.tileH);
    CHECK(tile.width() == 2);
    CHECK(tile.height() == 220);
    CHECK(tile.buffer()[0] == halfPixel(rect.levelX, rect.levelY, 4100, 3000));
}

TEST_CASE("a key in the other mode is pending", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    const TileKey managed(2, 0, 0, 0, 0, ColorMode::Managed);
    CHECK(source.readTile(managed).isPending());
    CHECK_FALSE(source.isTileReady(managed));
    CHECK(source.isTileReady(TileKey(2, 0, 0, 0, 0, ColorMode::Raw)));
}

TEST_CASE("a virtual tile is one mode-explicit block read with its key's rect, size and mode",
          "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    {
        BuiltPyramidSource both(fake);
        const auto levels = both.levels();
        CHECK(levels[1].resident);
        CHECK(levels[2].resident);
    }

    BuiltPyramidSource source(fake, 1'000'000);
    const auto levels = source.levels();
    REQUIRE(levels.size() == 3);
    CHECK_FALSE(levels[1].resident);
    CHECK(levels[2].resident);

    const TileKey key(1, 2, 3, 0, 0, ColorMode::Managed);
    CHECK(source.isTileReady(key));
    const TileData tile = source.readTile(key);
    const TileSlideRect rect = tileSlideRect(levels[1], 2, 3, 4100, 3000);
    REQUIRE(rect.valid);

    const auto calls = fake->calls();
    REQUIRE(calls.size() == 1);
    const BlockCall& c = calls[0];
    CHECK_FALSE(c.liveOverload);
    CHECK(c.mode == ColorMode::Managed);
    CHECK(c.x == rect.slideX);
    CHECK(c.y == rect.slideY);
    CHECK(c.w == rect.slideW);
    CHECK(c.h == rect.slideH);
    CHECK(c.tw == rect.tileW);
    CHECK(c.th == rect.tileH);
    CHECK(tile.width() == rect.tileW);
    CHECK(tile.height() == rect.tileH);
}

TEST_CASE("a stored tile forwards to the inner readTile", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);

    const TileKey key(0, 3, 2, 0, 0, ColorMode::Managed);
    const TileData tile = source.readTile(key);
    const auto reads = fake->tileReads();
    REQUIRE(reads.size() == 1);
    CHECK(reads[0] == key);
    CHECK(tile.width() == 16);
    CHECK(tile.buffer()[0] == 42);
    CHECK(source.isTileReady(key));
    CHECK(fake->calls().empty());
}

TEST_CASE("keys outside the pyramid are error tiles", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);

    CHECK(source.readTile(TileKey(3, 0, 0)).isError());
    CHECK(source.readTile(TileKey(-1, 0, 0)).isError());
    CHECK(source.readTile(TileKey(1, 99, 0)).isError());
    // Built levels exist only for a single Z slice and T frame.
    CHECK(source.readTile(TileKey(1, 0, 0, 1, 0)).isError());
    CHECK(source.readTile(TileKey(1, 0, 0, 0, 1)).isError());
    CHECK(source.isTileReady(TileKey(1, 0, 0, 1, 0)));
    CHECK(fake->tileReads().empty());
    CHECK(fake->calls().empty());
}

TEST_CASE("a failed band yields error tiles and the build completes", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    fake->failBandAt(512); // source rows 512..1023 = level-1 rows 256..511
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    CHECK_FALSE(source.builtOverviewPending());
    CHECK_FALSE(source.readTile(TileKey(1, 0, 0)).isError());
    const TileKey failed(1, 0, 1);
    CHECK(source.isTileReady(failed));
    CHECK(source.readTile(failed).isError());
    CHECK_FALSE(source.readTile(TileKey(1, 0, 2)).isError());
    CHECK(source.readTile(TileKey(2, 0, 0)).isError()); // its rows average the failed ones
}

TEST_CASE("a mode change rebuilds in the new mode", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));
    const size_t firstPass = fake->calls().size();
    REQUIRE(firstPass > 0);
    fake->clearCalls();

    const TileKey managed(1, 0, 0, 0, 0, ColorMode::Managed);
    fake->blockReads();
    source.setColorMode(ColorMode::Managed);
    CHECK(fake->colorMode() == ColorMode::Managed);
    CHECK(source.colorMode() == ColorMode::Managed);
    REQUIRE(fake->waitUntilBlocked());
    CHECK_FALSE(source.isTileReady(managed));
    CHECK(source.readTile(managed).isPending());
    CHECK(source.builtOverviewPending());

    fake->release();
    REQUIRE(progress.waitForCompletes(2));

    CHECK(source.isTileReady(managed));
    CHECK_FALSE(source.readTile(managed).isPending());
    CHECK(source.readTile(TileKey(1, 0, 0)).isPending()); // the Raw build is gone
    const auto calls = fake->calls();
    CHECK(calls.size() == firstPass);
    for (const BlockCall& c : calls) {
        CHECK(c.mode == ColorMode::Managed);
        CHECK_FALSE(c.liveOverload);
    }
}

TEST_CASE("destruction mid-build joins and stops callbacks", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    auto source = std::make_unique<BuiltPyramidSource>(fake);
    std::atomic<int> callbacks{0};
    source->addOnBuiltRowsReady([&callbacks](bool) { ++callbacks; });

    fake->blockReads();
    source->start();
    REQUIRE(fake->waitUntilBlocked());

    std::thread releaser([&fake] {
        // The destructor must be waiting in join() when the read returns; 50 ms
        // is the brief's figure and no latch can observe "inside join()".
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fake->release();
    });
    source.reset();
    const int afterDestruction = callbacks.load();
    releaser.join();
    // Nothing can fire now -- the worker is joined -- but give a stray thread a
    // moment to prove it, should one exist.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(callbacks.load() == afterDestruction);
    CHECK(afterDestruction == 0);
}

TEST_CASE("the ready listener fires once per band and once with complete", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);
    source.start();
    source.start(); // idempotent
    REQUIRE(progress.waitForCompletes(1));

    std::lock_guard<std::mutex> lock(progress.mutex);
    REQUIRE(progress.events.size() == 7); // ceil(3000 / 512) bands, then complete
    for (size_t i = 0; i < 6; ++i) {
        CHECK_FALSE(progress.events[i]);
    }
    CHECK(progress.events[6]);
    CHECK(fake->calls().size() == 6);
}

TEST_CASE("no thread starts for a slide without resident levels", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>(2000, 1000);
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);

    CHECK_FALSE(source.hasBuiltLevels());
    CHECK(source.levels().size() == 1);
    CHECK_FALSE(source.builtOverviewPending());
    source.start();
    CHECK(fake->calls().empty());
    CHECK(progress.eventCount() == 0);
    CHECK(source.smallestBuiltLevel().isEmpty());
}
