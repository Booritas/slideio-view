#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/BuiltPyramidSource.h"
#include "slideio/viewer/core/BuiltLevelCache.h"
#include "slideio/viewer/core/BuiltLevels.h"
#include "slideio/viewer/core/IBuiltLevelStore.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

// The fakes' addOnLevelMarkedUnreliable: keeps the listeners and tells them
// when a test marks a level, on the test's own thread -- as SlideIOAdapter
// tells them on whichever thread read the bad tile.
class UnreliableNotifier
{
public:
    void add(std::function<void(int)> listener)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listeners.push_back(std::move(listener));
    }

    void mark(int level)
    {
        std::vector<std::function<void(int)>> listeners;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            listeners = m_listeners;
        }
        for (const auto& listener : listeners) {
            listener(level);
        }
    }

    size_t count() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_listeners.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::function<void(int)>> m_listeners;
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
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_mode = mode;
        }
        m_cv.notify_all();
    }

    bool waitForMode(ColorMode mode)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this, mode] { return m_mode == mode; });
    }

    // Every read of the band starting at y throws std::runtime_error.
    void throwAt(int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_throwY = y;
    }

    // Every read of the band starting at y answers a tile one column narrower.
    void shrinkAt(int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shrinkY = y;
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
            m_holdX = -1;
            m_holdY = -1;
        }
        m_cv.notify_all();
    }

    bool waitUntilBlocked(int readers = 1)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this, readers] { return m_waiting >= readers; });
    }

    // Only the chunk read starting at (x, y) blocks, until release().
    void holdChunkAt(int x, int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_holdX = x;
        m_holdY = y;
    }

    // Every read of the band starting at y throws an int, not a std::exception.
    void throwNonStdAt(int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_throwIntY = y;
    }

    // Every read sleeps this long, outside the lock, so concurrent reads overlap.
    void setReadDelay(std::chrono::milliseconds delay)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_delay = delay;
    }

    // The most readBlock calls that were ever inside serve() at once.
    int maxInFlight() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_maxInFlight;
    }

    bool waitForCalls(size_t n)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this, n] { return m_calls.size() >= n; });
    }

    void failBandAt(int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failY = y;
    }

    // Only the chunk read starting at (x, y) answers an error tile.
    void failChunkAt(int x, int y)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failChunkX = x;
        m_failChunkY = y;
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

    void addOnLevelMarkedUnreliable(std::function<void(int)> listener) override
    {
        m_unreliable.add(std::move(listener));
    }
    void markUnreliable(int level) { m_unreliable.mark(level); }
    size_t unreliableListeners() const { return m_unreliable.count(); }

private:
    TileData serve(int x, int y, int w, int h, int tw, int th, int z, int t, ColorMode mode, bool live)
    {
        bool fail = false;
        bool shrink = false;
        std::chrono::milliseconds delay{0};
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_calls.push_back(BlockCall{x, y, w, h, tw, th, z, t, mode, live});
            m_cv.notify_all();
            fail = (y == m_failY) || (x == m_failChunkX && y == m_failChunkY);
            shrink = (y == m_shrinkY);
            delay = m_delay;
            if (y == m_throwY) {
                throw std::runtime_error("fake read failure");
            }
            if (y == m_throwIntY) {
                throw 7;
            }
            const bool held = x == m_holdX && y == m_holdY;
            if (m_blocking || held) {
                ++m_waiting;
                m_cv.notify_all();
                m_cv.wait(lock, [this, held] { return !m_blocking && !(held && m_holdX >= 0); });
                --m_waiting;
            }
            m_maxInFlight = std::max(m_maxInFlight, ++m_inFlight);
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_inFlight;
        }
        if (fail) {
            return TileData::createError(tw, th);
        }
        if (shrink) {
            std::vector<uint8_t> narrow(static_cast<size_t>(tw - 1) * static_cast<size_t>(th), 7);
            return TileData(std::move(narrow), tw - 1, th, 1, DataType::Byte);
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
    int m_failChunkX = -1;
    int m_failChunkY = -1;
    int m_throwY = -1;
    int m_shrinkY = -1;
    int m_throwIntY = -1;
    int m_holdX = -1;
    int m_holdY = -1;
    int m_inFlight = 0;
    int m_maxInFlight = 0;
    std::chrono::milliseconds m_delay{0};
    std::vector<BlockCall> m_calls;
    std::vector<TileKey> m_tileReads;
    UnreliableNotifier m_unreliable;
};

// A slide of 8200x6010 whose coarsest stored level is 4100x3000. The two axis
// ratios differ, so the level's scale is the smaller (3000/6010) one, and a
// level rect scaled back to the slide runs past the right edge and must be
// clamped. Level-1 pixel (x, y) is (x*7 + y*3) & 0xFF; a block read maps its
// slide rect back to level-1 coordinates with the same scale and serves those.
class TwoLevelFake : public ISlideSource
{
public:
    static constexpr int kSlideW = 8200;
    static constexpr int kSlideH = 6010;
    static constexpr int kLevelW = 4100;
    static constexpr int kLevelH = 3000;

    TwoLevelFake()
    {
        m_levels.resize(2);
        m_levels[0].level = 0;
        m_levels[0].width = kSlideW;
        m_levels[0].height = kSlideH;
        m_levels[0].scale = 1.0;
        m_levels[1].level = 1;
        m_levels[1].width = kLevelW;
        m_levels[1].height = kLevelH;
        m_levels[1].scale = scale();
        for (LevelInfo& l : m_levels) {
            l.tileWidth = 256;
            l.tileHeight = 256;
            l.tilesX = (l.width + 255) / 256;
            l.tilesY = (l.height + 255) / 256;
        }
    }

    static double scale()
    {
        return std::min(static_cast<double>(kLevelW) / kSlideW, static_cast<double>(kLevelH) / kSlideH);
    }

    static int toLevel(int slideCoord) { return static_cast<int>(std::round(slideCoord * scale())); }

    SlideInfo slideInfo() const override
    {
        SlideInfo info;
        info.width = kSlideW;
        info.height = kSlideH;
        info.numChannels = 1;
        info.channelDataType = DataType::Byte;
        info.numZoomLevels = 2;
        info.levels = m_levels;
        return info;
    }

    std::vector<LevelInfo> levels() const override { return m_levels; }

    TileData readTile(const TileKey&) override { return TileData::createError(0, 0); }

    TileData readBlock(int x, int y, int w, int h, int tw, int th, int z, int t) override
    {
        return serve(x, y, w, h, tw, th, z, t, ColorMode::Raw, true);
    }

    TileData readBlock(int x, int y, int w, int h, int tw, int th, int z, int t, ColorMode mode) override
    {
        return serve(x, y, w, h, tw, th, z, t, mode, false);
    }

    std::vector<BlockCall> calls() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_calls;
    }

    void addOnLevelMarkedUnreliable(std::function<void(int)> listener) override
    {
        m_unreliable.add(std::move(listener));
    }
    void markUnreliable(int level) { m_unreliable.mark(level); }

private:
    UnreliableNotifier m_unreliable;

    TileData serve(int x, int y, int w, int h, int tw, int th, int z, int t, ColorMode mode, bool live)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_calls.push_back(BlockCall{x, y, w, h, tw, th, z, t, mode, live});
        }
        const int lx = toLevel(x);
        const int ly = toLevel(y);
        std::vector<uint8_t> buffer(static_cast<size_t>(tw) * static_cast<size_t>(th), 0);
        for (int j = 0; j < th; ++j) {
            for (int i = 0; i < tw; ++i) {
                buffer[static_cast<size_t>(j) * tw + i] = sourcePixel(lx + i, ly + j);
            }
        }
        return TileData(std::move(buffer), tw, th, 1, DataType::Byte);
    }

    std::vector<LevelInfo> m_levels;
    mutable std::mutex m_mutex;
    std::vector<BlockCall> m_calls;
};

// Captures what the default spdlog logger writes for the life of one test.
class LogCapture
{
public:
    LogCapture()
        : m_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_stream))
    {
        spdlog::default_logger()->sinks().push_back(m_sink);
    }

    ~LogCapture()
    {
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    int count(const std::string& needle)
    {
        spdlog::default_logger()->flush();
        const std::string text = m_stream.str();
        int n = 0;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
            ++n;
        }
        return n;
    }

private:
    std::ostringstream m_stream;
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
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
    // One reader: the order the reads START in is only top to bottom when one
    // thread makes them; with several only the feeding order is (see [parallel]).
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000, 1);
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
    LogCapture log;
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
    // Failed rows are zeros; the minimap would show them as a black stripe, so
    // no overview at all is offered -- and the reason is logged once.
    CHECK(source.smallestBuiltLevel().isEmpty());
    CHECK(source.smallestBuiltLevel().isEmpty());
    CHECK(log.count("overview withheld because") == 1);
}

TEST_CASE("a failed chunk fails only the tiles over its own columns", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    // 512-row bands, chunks of seven 256-px tiles: x = 0, 1792, 3584.
    fake->failChunkAt(1792, 512); // level-1 columns 896..1791, rows 256..511
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    // Level-1 tile row 1 is the failed band; only tile columns 3..6 overlap the chunk.
    CHECK_FALSE(source.readTile(TileKey(1, 0, 1)).isError());
    CHECK_FALSE(source.readTile(TileKey(1, 2, 1)).isError());
    CHECK(source.readTile(TileKey(1, 3, 1)).isError());
    CHECK(source.readTile(TileKey(1, 6, 1)).isError());
    CHECK_FALSE(source.readTile(TileKey(1, 7, 1)).isError());

    const TileData beside = source.readTile(TileKey(1, 0, 1));
    REQUIRE(beside.width() == 256);
    CHECK(beside.buffer()[0] == halfPixel(0, 256, 4100, 3000));

    // Level 2 (/4): columns 448..895 failed, so tile column 0 is clean, 1..3 are not.
    CHECK_FALSE(source.readTile(TileKey(2, 0, 0)).isError());
    CHECK(source.readTile(TileKey(2, 2, 0)).isError());
    // Still no overview: it would carry the hole.
    CHECK(source.smallestBuiltLevel().isEmpty());
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

TEST_CASE("a coarser source level is read at its own resolution and mapped back to the slide",
          "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<TwoLevelFake>();
    // 512-row bands: 1953 columns per chunk, three chunks a band.
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1000000);
    const auto levels = source.levels();
    REQUIRE(levels.size() == 4);
    REQUIRE(levels[2].builtFactor == 2);
    REQUIRE(levels[2].resident);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    const auto calls = fake->calls();
    REQUIRE(calls.size() == 18);
    std::vector<uint8_t> slideCovered(static_cast<size_t>(TwoLevelFake::kSlideW) * TwoLevelFake::kSlideH, 0);
    std::vector<uint8_t> levelCovered(static_cast<size_t>(TwoLevelFake::kLevelW) * TwoLevelFake::kLevelH, 0);
    for (const BlockCall& c : calls) {
        CHECK_FALSE(c.liveOverload);
        // Never a full-resolution read: the output is the level's, about half the slide rect.
        CHECK(c.tw < c.w);
        CHECK(c.th < c.h);
        REQUIRE(c.x >= 0);
        REQUIRE(c.y >= 0);
        REQUIRE(c.x + c.w <= TwoLevelFake::kSlideW);
        REQUIRE(c.y + c.h <= TwoLevelFake::kSlideH);
        for (int y = c.y; y < c.y + c.h; ++y) {
            for (int x = c.x; x < c.x + c.w; ++x) {
                ++slideCovered[static_cast<size_t>(y) * TwoLevelFake::kSlideW + x];
            }
        }
        // The output size is the level-1 rect this chunk stands for.
        const int lx = TwoLevelFake::toLevel(c.x);
        const int ly = TwoLevelFake::toLevel(c.y);
        REQUIRE(lx + c.tw <= TwoLevelFake::kLevelW);
        REQUIRE(ly + c.th <= TwoLevelFake::kLevelH);
        for (int y = ly; y < ly + c.th; ++y) {
            for (int x = lx; x < lx + c.tw; ++x) {
                ++levelCovered[static_cast<size_t>(y) * TwoLevelFake::kLevelW + x];
            }
        }
    }
    CHECK(std::all_of(slideCovered.begin(), slideCovered.end(), [](uint8_t v) { return v == 1; }));
    CHECK(std::all_of(levelCovered.begin(), levelCovered.end(), [](uint8_t v) { return v == 1; }));
    // The rightmost chunks are clamped to the slide's right edge: 4100 / scale overshoots 8200.
    CHECK(std::round(TwoLevelFake::kLevelW / TwoLevelFake::scale()) > TwoLevelFake::kSlideW);
    const auto rightmost = std::max_element(calls.begin(), calls.end(),
                                            [](const BlockCall& a, const BlockCall& b) { return a.x < b.x; });
    CHECK(rightmost->x + rightmost->w == TwoLevelFake::kSlideW);

    const TileData tile = source.readTile(TileKey(2, 3, 2));
    REQUIRE_FALSE(tile.isPending());
    REQUIRE_FALSE(tile.isError());
    REQUIRE(tile.width() == 256);
    REQUIRE(tile.height() == 256);
    bool exact = true;
    for (int j = 0; j < 256; ++j) {
        for (int i = 0; i < 256; ++i) {
            exact = exact
                && tile.buffer()[static_cast<size_t>(j) * 256 + i]
                    == halfPixel(3 * 256 + i, 2 * 256 + j, TwoLevelFake::kLevelW, TwoLevelFake::kLevelH);
        }
    }
    CHECK(exact);
}

TEST_CASE("a mode change while the worker is inside a read restarts cleanly", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    // One reader, so exactly one Raw read is in flight; the four-reader case is
    // "a mode change while four readers are inside reads restarts cleanly".
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, BuiltPyramidSource::kDefaultBandChunkMaxBytes, 1);
    Progress progress;
    progress.attach(source);

    fake->blockReads();
    source.start(); // Raw
    REQUIRE(fake->waitUntilBlocked());

    std::thread toggler([&source] { source.setColorMode(ColorMode::Managed); });
    // setColorMode raises the cancel flag before it forwards the mode, so once
    // the inner source reports Managed the Raw build can read no further chunk.
    REQUIRE(fake->waitForMode(ColorMode::Managed));
    fake->release();
    toggler.join(); // returns once the Raw worker is joined and the Managed one launched
    REQUIRE(progress.waitForCompletes(1));

    const auto calls = fake->calls();
    REQUIRE(calls.size() == 7); // the one Raw read in flight, then 6 Managed bands
    CHECK(calls[0].mode == ColorMode::Raw);
    for (size_t i = 1; i < calls.size(); ++i) {
        CHECK(calls[i].mode == ColorMode::Managed);
        CHECK_FALSE(calls[i].liveOverload);
    }
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        CHECK(progress.completes == 1);
        CHECK(progress.events.size() == 7); // only the Managed build's 6 bands and its completion
    }
    const TileKey raw(1, 0, 0, 0, 0, ColorMode::Raw);
    CHECK(source.readTile(raw).isPending());
    CHECK_FALSE(source.isTileReady(raw));
    const TileKey managed(1, 0, 0, 0, 0, ColorMode::Managed);
    CHECK(source.isTileReady(managed));
    const TileData tile = source.readTile(managed);
    CHECK_FALSE(tile.isPending());
    CHECK_FALSE(tile.isError());
}

TEST_CASE("a read that throws fails its band, warns once and the build completes", "[infra][BuiltPyramidSource]")
{
    LogCapture log;
    auto fake = std::make_shared<FakeSource>();
    fake->throwAt(512); // all three chunks of the second band throw
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1000000);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    CHECK_FALSE(source.builtOverviewPending());
    CHECK(fake->calls().size() == 18);
    CHECK_FALSE(source.readTile(TileKey(1, 0, 0)).isError());
    CHECK(source.isTileReady(TileKey(1, 0, 1)));
    CHECK(source.readTile(TileKey(1, 0, 1)).isError());
    CHECK(source.readTile(TileKey(1, 8, 1)).isError());
    CHECK_FALSE(source.readTile(TileKey(1, 0, 2)).isError());
    CHECK(log.count("fake read failure") == 1);
}

TEST_CASE("a wrong-sized read is a failed chunk and the build completes", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    fake->shrinkAt(1024); // source rows 1024..1535 = level-1 rows 512..767
    BuiltPyramidSource source(fake);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    CHECK_FALSE(source.builtOverviewPending());
    CHECK_FALSE(source.readTile(TileKey(1, 0, 1)).isError());
    CHECK(source.readTile(TileKey(1, 0, 2)).isError());
    CHECK_FALSE(source.readTile(TileKey(1, 0, 3)).isError());
}

TEST_CASE("a band chunk is a whole number of source tiles within the byte cap", "[infra][BuiltPyramidSource]")
{
    CHECK(BuiltPyramidSource::kDefaultBandChunkMaxBytes == 4 * 1024 * 1024);
    // 512-row bands of one byte: a 256-wide tile is 131072 bytes, so 1 MB fits 7.
    CHECK(BuiltPyramidSource::bandChunkWidth(4100, 256, 512, 1'000'000) == 7 * 256);
    // One tile alone over the cap: still one tile, never less.
    CHECK(BuiltPyramidSource::bandChunkWidth(4100, 256, 512, 1000) == 256);
    // Never wider than the level.
    CHECK(BuiltPyramidSource::bandChunkWidth(100, 256, 512, 4 * 1024 * 1024) == 100);
    CHECK(BuiltPyramidSource::bandChunkWidth(4100, 256, 512, 4 * 1024 * 1024) == 4100);
    // RGB 4096-row bands: one 256-wide tile is 3 MiB, so a 4 MiB cap holds exactly one.
    CHECK(BuiltPyramidSource::bandChunkWidth(65000, 256, 4096 * 3, 4 * 1024 * 1024) == 256);
    // An unknown tile width is taken as 256.
    CHECK(BuiltPyramidSource::bandChunkWidth(4100, 0, 512, 1'000'000) == 7 * 256);

    for (const int cap : {1'000'000, 1000}) {
        auto fake = std::make_shared<FakeSource>();
        BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, cap);
        Progress progress;
        progress.attach(source);
        source.start();
        REQUIRE(progress.waitForCompletes(1));

        const auto calls = fake->calls();
        REQUIRE_FALSE(calls.empty());
        for (const BlockCall& c : calls) {
            CHECK(c.x % 256 == 0);
            const bool last = c.x + c.w == 4100;
            if (!last) {
                CHECK(c.w % 256 == 0);
            }
            const size_t bytes = static_cast<size_t>(c.w) * static_cast<size_t>(c.h);
            if (cap >= 256 * 512) {
                CHECK(bytes <= static_cast<size_t>(cap));
            } else {
                CHECK(c.w <= 256); // one tile alone exceeds the cap
            }
        }
        CHECK(calls.size() == (cap == 1000 ? 6u * 17u : 6u * 3u));
    }
}

TEST_CASE("the source level marked unreliable mid-build abandons the build", "[infra][BuiltPyramidSource]")
{
    LogCapture log;
    auto fake = std::make_shared<FakeSource>();
    // One reader, so exactly one read is in flight; the four-reader case is
    // "unreliable while chunks are in flight is abandoned".
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000, 1);
    CHECK(fake->unreliableListeners() == 1); // subscribed at construction, before start()
    Progress progress;
    progress.attach(source);

    fake->blockReads();
    source.start();
    REQUIRE(fake->waitUntilBlocked());
    const TileKey firstBand(1, 0, 0);
    CHECK_FALSE(source.isTileReady(firstBand));

    // Told on this thread, while the worker is inside a read: only a flag.
    fake->markUnreliable(0);
    fake->markUnreliable(0);
    CHECK_FALSE(source.builtOverviewPending());
    // Already answered before the worker notices: nothing waits on a tile.
    CHECK(source.isTileReady(firstBand));
    CHECK(source.readTile(firstBand).isError());

    fake->release();
    // The abandoned build still says it is over, so the UI requests again.
    REQUIRE(progress.waitForCompletes(1));

    // The one read in flight, and no further one; its pixels are not used.
    CHECK(fake->calls().size() == 1);
    CHECK_FALSE(source.builtOverviewPending());
    CHECK(source.smallestBuiltLevel().isEmpty());
    for (const TileKey& key : {TileKey(1, 0, 0), TileKey(1, 3, 4), TileKey(2, 0, 0),
                               TileKey(1, 0, 0, 0, 0, ColorMode::Managed)}) {
        CHECK(source.isTileReady(key));
        CHECK(source.readTile(key).isError());
    }
    CHECK(log.count("was marked unreliable") == 1);

    // A colour toggle does not rebuild from the distrusted level.
    fake->clearCalls();
    source.setColorMode(ColorMode::Managed);
    CHECK(fake->calls().empty());
    CHECK(source.readTile(TileKey(1, 0, 0, 0, 0, ColorMode::Managed)).isError());
    CHECK_FALSE(source.builtOverviewPending());
}

TEST_CASE("the source level marked unreliable before start reads nothing", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    fake->markUnreliable(0);
    source.start();
    CHECK_FALSE(source.builtOverviewPending());
    CHECK(source.isTileReady(TileKey(1, 0, 0)));
    CHECK(source.readTile(TileKey(1, 0, 0)).isError());
    CHECK(source.smallestBuiltLevel().isEmpty());
    CHECK(fake->calls().empty());
}

TEST_CASE("marking a level other than the source level has no effect", "[infra][BuiltPyramidSource]")
{
    auto fake = std::make_shared<TwoLevelFake>(); // builds from stored level 1
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000);
    Progress progress;
    progress.attach(source);
    fake->markUnreliable(0);
    CHECK(source.builtOverviewPending());
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    CHECK(fake->calls().size() == 18);
    CHECK_FALSE(source.builtOverviewPending());
    CHECK_FALSE(source.smallestBuiltLevel().isEmpty());
    const TileData tile = source.readTile(TileKey(2, 0, 0));
    CHECK_FALSE(tile.isError());
    CHECK_FALSE(tile.isPending());
}

TEST_CASE("a throwing listener neither stops the build nor reaches the thread", "[infra][BuiltPyramidSource]")
{
    LogCapture log;
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    source.addOnBuiltRowsReady([](bool) { throw std::runtime_error("listener boom"); });
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));

    CHECK_FALSE(source.builtOverviewPending());
    CHECK(fake->calls().size() == 6);
    CHECK_FALSE(source.smallestBuiltLevel().isEmpty());
    CHECK(log.count("listener boom") == 7); // six bands and the completion
}

// ---------------------------------------------------------------------------
// The level store (built-level disk cache, spec 2026-11-27 §5.3).
// ---------------------------------------------------------------------------

namespace
{

// Keeps saved rasters in memory by key name, records every call, and can hold
// a save or a load until the cancel flag it was handed is raised.
class MemoryStore : public IBuiltLevelStore
{
public:
    std::optional<std::vector<std::vector<uint8_t>>> load(const BuiltLevelCacheKey& key,
                                                          const std::vector<std::pair<int, int>>& levelSizes,
                                                          size_t pixelBytes, const std::atomic<bool>& cancel) override
    {
        bool block = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            block = m_blockLoads;
            m_inLoad = true;
        }
        m_cv.notify_all();
        if (block) {
            // Polls as the real store does between blocks; gives up after kWait.
            const auto deadline = std::chrono::steady_clock::now() + kWait;
            while (!cancel && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inLoad = false;
        if (cancel) {
            ++m_cancelledLoads;
        }
        // A cancelled load still answers what it holds, so a test can tell that
        // the source itself refuses to install levels read after a cancel.
        ++m_loads;
        const auto it = m_entries.find(builtLevelCacheKeyName(key));
        if (it == m_entries.end() || it->second.key != key || it->second.levels.size() != levelSizes.size()) {
            return std::nullopt;
        }
        for (size_t i = 0; i < levelSizes.size(); ++i) {
            const size_t expected = static_cast<size_t>(levelSizes[i].first)
                * static_cast<size_t>(levelSizes[i].second) * pixelBytes;
            if (it->second.levels[i].size() != expected) {
                return std::nullopt;
            }
        }
        ++m_hits;
        return it->second.levels;
    }

    bool save(const BuiltLevelCacheKey& key, const BuiltLevelCacheDescription& description,
              const std::vector<BuiltLevelView>& levels, const std::atomic<bool>& cancel) override
    {
        bool block = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_saves;
            m_description = description;
            block = m_blockSaves;
            m_inSave = true;
        }
        m_cv.notify_all();
        if (block) {
            // Holds until the caller cancels, polling as the real store does
            // between blocks; gives up after kWait so a broken test cannot hang.
            const auto deadline = std::chrono::steady_clock::now() + kWait;
            while (!cancel && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inSave = false;
        if (m_failSaves) {
            ++m_failedSaves;
            m_cv.notify_all();
            return false;
        }
        if (cancel) {
            ++m_cancelledSaves;
            m_cv.notify_all();
            return false;
        }
        Entry entry;
        entry.key = key;
        for (const BuiltLevelView& view : levels) {
            entry.levels.emplace_back(view.data, view.data + view.size);
        }
        m_entries[builtLevelCacheKeyName(key)] = std::move(entry);
        ++m_completedSaves;
        m_cv.notify_all();
        return true;
    }

    void blockSaves()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blockSaves = true;
    }

    // Every save fails as a full disk would: false, not cancelled.
    void failSaves()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failSaves = true;
    }

    bool waitForFailedSaves(int n)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this, n] { return m_failedSaves >= n; });
    }

    void blockLoads()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blockLoads = true;
    }

    bool waitUntilInLoad()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this] { return m_inLoad; });
    }

    int cancelledLoads() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cancelledLoads;
    }

    bool waitForCompletedSaves(int n)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this, n] { return m_completedSaves >= n; });
    }

    bool waitUntilInSave()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kWait, [this] { return m_inSave; });
    }

    int loads() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_loads;
    }
    int hits() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hits;
    }
    int saves() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_saves;
    }
    int cancelledSaves() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cancelledSaves;
    }
    size_t entryCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries.size();
    }
    std::string savedPath() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_description.path;
    }
    std::vector<BuiltLevelCacheKey> savedKeys() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<BuiltLevelCacheKey> keys;
        for (const auto& entry : m_entries) {
            keys.push_back(entry.second.key);
        }
        return keys;
    }

private:
    struct Entry
    {
        BuiltLevelCacheKey key;
        std::vector<std::vector<uint8_t>> levels;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<std::string, Entry> m_entries;
    BuiltLevelCacheDescription m_description;
    bool m_blockSaves = false;
    bool m_inSave = false;
    bool m_failSaves = false;
    int m_failedSaves = 0;
    bool m_blockLoads = false;
    bool m_inLoad = false;
    int m_cancelledLoads = 0;
    int m_loads = 0;
    int m_hits = 0;
    int m_saves = 0;
    int m_cancelledSaves = 0;
    int m_completedSaves = 0;
};

BuiltLevelCacheIdentity cacheIdentity(const std::string& fingerprint = "0123456789abcdef")
{
    BuiltLevelCacheIdentity identity;
    identity.slideId = "slide-under-test";
    identity.sceneIndex = 0;
    identity.fileSize = 123456;
    identity.fileModifiedMs = 1700000000000;
    identity.managedProfileFingerprint = fingerprint;
    identity.managedRenditionTag = "sRGB/Perceptual";
    identity.slideioVersion = "test";
    return identity;
}

BuiltLevelCacheDescription cacheDescription()
{
    BuiltLevelCacheDescription description;
    description.path = "D:/slides/under-test.tif";
    description.fileName = "under-test.tif";
    description.driverId = "GDAL";
    return description;
}

// Every resident tile of the mode is ready in b and byte-equal to a's.
void requireSameResidentTiles(BuiltPyramidSource& a, BuiltPyramidSource& b, ColorMode mode)
{
    int compared = 0;
    for (const LevelInfo& level : a.levels()) {
        if (!level.resident) {
            continue;
        }
        for (int row = 0; row < level.tilesY; ++row) {
            for (int column = 0; column < level.tilesX; ++column) {
                const TileKey key(level.level, column, row, 0, 0, mode);
                REQUIRE(b.isTileReady(key));
                const TileData ta = a.readTile(key);
                const TileData tb = b.readTile(key);
                REQUIRE_FALSE(ta.isPending());
                REQUIRE_FALSE(ta.isError());
                REQUIRE_FALSE(tb.isPending());
                REQUIRE_FALSE(tb.isError());
                REQUIRE(ta.width() == tb.width());
                REQUIRE(ta.height() == tb.height());
                REQUIRE(ta.buffer() == tb.buffer());
                ++compared;
            }
        }
    }
    REQUIRE(compared > 0);
}

} // namespace

TEST_CASE("a clean build is saved once, then a second source hits without a band read",
          "[infra][BuiltPyramidSource][cache]")
{
    LogCapture log;
    auto store = std::make_shared<MemoryStore>();

    auto fakeA = std::make_shared<FakeSource>();
    BuiltPyramidSource a(fakeA);
    a.setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progressA;
    progressA.attach(a);
    a.start();
    REQUIRE(progressA.waitForCompletes(1));
    REQUIRE(store->waitForCompletedSaves(1));
    CHECK(store->saves() == 1);
    CHECK(store->savedPath() == "D:/slides/under-test.tif");

    auto fakeB = std::make_shared<FakeSource>();
    auto b = std::make_unique<BuiltPyramidSource>(fakeB);
    b->setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progressB;
    progressB.attach(*b);
    b->start();
    REQUIRE(progressB.waitForCompletes(1));

    CHECK(fakeB->calls().empty());
    CHECK(store->hits() == 1);
    CHECK_FALSE(b->builtOverviewPending());
    CHECK(b->smallestBuiltLevel().byteSize() == static_cast<size_t>(1025) * 750);
    requireSameResidentTiles(a, *b, ColorMode::Raw);

    b.reset(); // joins: nothing further can be notified or saved
    CHECK(progressB.eventCount() == 1);
    CHECK(store->saves() == 1); // a hit is not saved again
    CHECK(log.count("from cache") == 1);
    // Both lines name the slide: the save and the hit.
    CHECK(log.count("D:/slides/under-test.tif") >= 2);
}

TEST_CASE("a build with a failed chunk is not saved", "[infra][BuiltPyramidSource][cache]")
{
    auto store = std::make_shared<MemoryStore>();
    auto fake = std::make_shared<FakeSource>();
    fake->failChunkAt(0, 0);
    auto source = std::make_unique<BuiltPyramidSource>(fake);
    source->setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progress;
    progress.attach(*source);
    source->start();
    REQUIRE(progress.waitForCompletes(1));
    source.reset();

    CHECK(store->loads() == 1);
    CHECK(store->saves() == 0);
}

TEST_CASE("an abandoned build is not saved", "[infra][BuiltPyramidSource][cache]")
{
    auto store = std::make_shared<MemoryStore>();
    auto fake = std::make_shared<FakeSource>();
    auto source = std::make_unique<BuiltPyramidSource>(fake, kBuiltLevelBudgetBytes, 1'000'000);
    source->setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progress;
    progress.attach(*source);

    fake->blockReads();
    source->start();
    REQUIRE(fake->waitUntilBlocked());
    fake->markUnreliable(0);
    fake->release();
    REQUIRE(progress.waitForCompletes(1));
    source.reset();

    CHECK(store->loads() == 1);
    CHECK(store->saves() == 0);
}

TEST_CASE("finished levels keep their overview when the source is marked unreliable later",
          "[infra][BuiltPyramidSource][cache]")
{
    // One rule for finished levels, built or loaded: the tiles stay Ready, so
    // the overview is published too and the minimap agrees with the slide.
    auto store = std::make_shared<MemoryStore>();
    auto fakeA = std::make_shared<FakeSource>();
    BuiltPyramidSource a(fakeA);
    a.setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progressA;
    progressA.attach(a);
    a.start();
    REQUIRE(progressA.waitForCompletes(1));
    REQUIRE(store->waitForCompletedSaves(1));

    SECTION("a completed build")
    {
        fakeA->markUnreliable(0);
        CHECK_FALSE(a.readTile(TileKey(2, 0, 0)).isError());
        CHECK(a.smallestBuiltLevel().byteSize() == static_cast<size_t>(1025) * 750);
    }
    SECTION("a cache hit")
    {
        auto fakeB = std::make_shared<FakeSource>();
        BuiltPyramidSource b(fakeB);
        b.setLevelStore(store, cacheIdentity(), cacheDescription());
        Progress progressB;
        progressB.attach(b);
        b.start();
        REQUIRE(progressB.waitForCompletes(1));
        REQUIRE(store->hits() == 1);

        fakeB->markUnreliable(0);
        CHECK_FALSE(b.readTile(TileKey(2, 0, 0)).isError());
        CHECK(b.smallestBuiltLevel().byteSize() == static_cast<size_t>(1025) * 750);
    }
}

TEST_CASE("each colour mode has its own entry", "[infra][BuiltPyramidSource][cache]")
{
    auto store = std::make_shared<MemoryStore>();
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake);
    source.setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));
    REQUIRE(store->waitForCompletedSaves(1));

    source.setColorMode(ColorMode::Managed);
    REQUIRE(progress.waitForCompletes(2));
    REQUIRE(store->waitForCompletedSaves(2));

    const auto keys = store->savedKeys();
    REQUIRE(keys.size() == 2);
    CHECK(builtLevelCacheKeyName(keys[0]) != builtLevelCacheKeyName(keys[1]));
    const bool rawThenManaged = keys[0].colorMode == ColorMode::Raw && keys[1].colorMode == ColorMode::Managed;
    const bool managedThenRaw = keys[0].colorMode == ColorMode::Managed && keys[1].colorMode == ColorMode::Raw;
    CHECK((rawThenManaged || managedThenRaw));

    auto fresh = std::make_shared<FakeSource>();
    fresh->setColorMode(ColorMode::Managed);
    BuiltPyramidSource second(fresh);
    second.setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress secondProgress;
    secondProgress.attach(second);
    second.start();
    REQUIRE(secondProgress.waitForCompletes(1));
    CHECK(fresh->calls().empty());
    CHECK(store->hits() == 1);
    requireSameResidentTiles(source, second, ColorMode::Managed);
}

TEST_CASE("managed without a fingerprint is not cached", "[infra][BuiltPyramidSource][cache]")
{
    auto store = std::make_shared<MemoryStore>();
    auto fake = std::make_shared<FakeSource>();
    fake->setColorMode(ColorMode::Managed);
    auto source = std::make_unique<BuiltPyramidSource>(fake);
    source->setLevelStore(store, cacheIdentity(""), cacheDescription());
    Progress progress;
    progress.attach(*source);
    source->start();
    REQUIRE(progress.waitForCompletes(1));
    source.reset();

    CHECK_FALSE(fake->calls().empty()); // built as before
    CHECK(store->loads() == 0);
    CHECK(store->saves() == 0);
}

TEST_CASE("destruction during a save returns promptly and leaves no entry", "[infra][BuiltPyramidSource][cache]")
{
    auto store = std::make_shared<MemoryStore>();
    store->blockSaves();
    auto fake = std::make_shared<FakeSource>();
    auto source = std::make_unique<BuiltPyramidSource>(fake);
    source->setLevelStore(store, cacheIdentity(), cacheDescription());
    source->start();
    REQUIRE(store->waitUntilInSave());

    const auto before = std::chrono::steady_clock::now();
    source.reset();
    const auto elapsed = std::chrono::steady_clock::now() - before;

    CHECK(elapsed < std::chrono::seconds(2));
    CHECK(store->cancelledSaves() == 1);
    CHECK(store->entryCount() == 0);
}

TEST_CASE("destruction during a load returns promptly and installs nothing", "[infra][BuiltPyramidSource][cache]")
{
    auto store = std::make_shared<MemoryStore>();
    {
        // Prime the store, so the blocked load below would be a hit.
        auto fake = std::make_shared<FakeSource>();
        BuiltPyramidSource primer(fake);
        primer.setLevelStore(store, cacheIdentity(), cacheDescription());
        Progress progress;
        progress.attach(primer);
        primer.start();
        REQUIRE(progress.waitForCompletes(1));
        REQUIRE(store->waitForCompletedSaves(1));
    }

    store->blockLoads();
    auto fake = std::make_shared<FakeSource>();
    auto source = std::make_unique<BuiltPyramidSource>(fake);
    source->setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progress;
    progress.attach(*source);
    source->start();
    REQUIRE(store->waitUntilInLoad());

    const auto before = std::chrono::steady_clock::now();
    source.reset();
    const auto elapsed = std::chrono::steady_clock::now() - before;

    CHECK(elapsed < std::chrono::seconds(2));
    CHECK(store->cancelledLoads() == 1);
    CHECK(store->hits() == 1); // the store answered, the source did not install it
    CHECK(progress.eventCount() == 0);
    CHECK(fake->calls().empty());
}

TEST_CASE("a failed save leaves the store's line as the only one at info", "[infra][BuiltPyramidSource][cache]")
{
    LogCapture log;
    auto store = std::make_shared<MemoryStore>();
    store->failSaves();
    auto fake = std::make_shared<FakeSource>();
    auto source = std::make_unique<BuiltPyramidSource>(fake);
    source->setLevelStore(store, cacheIdentity(), cacheDescription());
    Progress progress;
    progress.attach(*source);
    source->start();
    REQUIRE(progress.waitForCompletes(1));
    REQUIRE(store->waitForFailedSaves(1));
    source.reset(); // joins, so the source has logged whatever it logs

    CHECK(log.count("not saved") == 0);
}

namespace
{

// What a reader of one resident tile sees: whether it is ready, an error, and its bytes.
struct TileAnswer
{
    bool ready = false;
    bool error = false;
    std::vector<uint8_t> bytes;

    bool operator==(const TileAnswer& other) const
    {
        return ready == other.ready && error == other.error && bytes == other.bytes;
    }
};

std::vector<TileAnswer> residentTiles(BuiltPyramidSource& source)
{
    std::vector<TileAnswer> answers;
    for (const LevelInfo& level : source.levels()) {
        if (!level.built || !level.resident) {
            continue;
        }
        for (int row = 0; row < level.tilesY; ++row) {
            for (int col = 0; col < level.tilesX; ++col) {
                const TileKey key(level.level, col, row);
                TileAnswer answer;
                answer.ready = source.isTileReady(key);
                const TileData tile = source.readTile(key);
                answer.error = tile.isError();
                if (!tile.isError() && !tile.isPending()) {
                    answer.bytes = tile.buffer();
                }
                answers.push_back(std::move(answer));
            }
        }
    }
    return answers;
}

struct BuildResult
{
    std::vector<TileAnswer> tiles;
    std::vector<uint8_t> overview;
    bool overviewEmpty = true;
};

// Builds the fake to completion with the given reader count and reads every resident tile.
BuildResult buildWith(const std::shared_ptr<FakeSource>& fake, int readers, int chunkBytes)
{
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, chunkBytes, readers);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(progress.waitForCompletes(1));
    BuildResult result;
    result.tiles = residentTiles(source);
    const TileData overview = source.smallestBuiltLevel();
    result.overviewEmpty = overview.isEmpty();
    if (!overview.isEmpty()) {
        result.overview = overview.buffer();
    }
    return result;
}

bool sameTiles(const BuildResult& a, const BuildResult& b)
{
    if (a.tiles.size() != b.tiles.size()) {
        return false;
    }
    for (size_t i = 0; i < a.tiles.size(); ++i) {
        if (!(a.tiles[i] == b.tiles[i])) {
            return false;
        }
    }
    return true;
}

int errorTiles(const BuildResult& r)
{
    int errors = 0;
    for (const TileAnswer& answer : r.tiles) {
        errors += answer.error ? 1 : 0;
    }
    return errors;
}

} // namespace

TEST_CASE("the default reader count is half the cores, between one and four",
          "[infra][BuiltPyramidSource][parallel]")
{
    const int hc = static_cast<int>(std::thread::hardware_concurrency());
    CHECK(BuiltPyramidSource::defaultBuiltPassReaders() == std::min(4, std::max(1, hc / 2)));
}

TEST_CASE("four readers build the same bytes as one", "[infra][BuiltPyramidSource][parallel]")
{
    // 512-row bands, 300 000-byte chunks: two source tiles a chunk, 9 chunks a band, 54 in all.
    const BuildResult one = buildWith(std::make_shared<FakeSource>(), 1, 300'000);
    const BuildResult four = buildWith(std::make_shared<FakeSource>(), 4, 300'000);

    REQUIRE_FALSE(one.tiles.empty());
    CHECK(errorTiles(one) == 0);
    bool allReady = true;
    for (const TileAnswer& answer : one.tiles) {
        allReady = allReady && answer.ready;
    }
    CHECK(allReady);
    CHECK(sameTiles(one, four));
    REQUIRE_FALSE(one.overviewEmpty);
    REQUIRE_FALSE(four.overviewEmpty);
    CHECK(one.overview == four.overview);
}

TEST_CASE("a failed chunk fails the same region with four readers", "[infra][BuiltPyramidSource][parallel]")
{
    auto fakeOne = std::make_shared<FakeSource>();
    fakeOne->throwAt(512);
    auto fakeFour = std::make_shared<FakeSource>();
    fakeFour->throwAt(512);
    LogCapture log;
    const BuildResult one = buildWith(fakeOne, 1, 1'000'000);
    const int warningsOne = log.count("fake read failure");
    const BuildResult four = buildWith(fakeFour, 4, 1'000'000);

    CHECK(errorTiles(one) > 0);
    CHECK(sameTiles(one, four));
    CHECK(one.overviewEmpty);
    CHECK(four.overviewEmpty);
    CHECK(warningsOne == 1);
    CHECK(log.count("fake read failure") == 2); // one warning per build
}

TEST_CASE("a read throwing a non-standard exception fails its chunk with four readers",
          "[infra][BuiltPyramidSource][parallel]")
{
    auto fakeOne = std::make_shared<FakeSource>();
    fakeOne->throwNonStdAt(1024);
    auto fakeFour = std::make_shared<FakeSource>();
    fakeFour->throwNonStdAt(1024);
    const BuildResult one = buildWith(fakeOne, 1, 1'000'000);
    const BuildResult four = buildWith(fakeFour, 4, 1'000'000);

    CHECK(errorTiles(four) > 0);
    CHECK(sameTiles(one, four));
}

TEST_CASE("four readers overlap and one does not", "[infra][BuiltPyramidSource][parallel]")
{
    for (const int readers : {1, 4}) {
        auto fake = std::make_shared<FakeSource>();
        fake->setReadDelay(std::chrono::milliseconds(5));
        BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000, readers);
        Progress progress;
        progress.attach(source);
        source.start();
        REQUIRE(progress.waitForCompletes(1));
        CHECK(fake->calls().size() == 18);
        if (readers == 1) {
            CHECK(fake->maxInFlight() == 1);
        } else {
            CHECK(fake->maxInFlight() > 1);
            CHECK(fake->maxInFlight() <= 4);
        }
    }
}

TEST_CASE("the window bounds reads ahead", "[infra][BuiltPyramidSource][parallel]")
{
    auto fake = std::make_shared<FakeSource>();
    fake->holdChunkAt(0, 0);
    // 54 chunks: far more than the window of 2 x 4 readers.
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 300'000, 4);
    Progress progress;
    progress.attach(source);
    source.start();
    REQUIRE(fake->waitUntilBlocked());
    REQUIRE(fake->waitForCalls(8));
    // A ninth read could only be a reader running past the window; give one
    // the chance to show itself.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(fake->calls().size() == 8);
    CHECK(progress.eventCount() == 0); // nothing fed while chunk 0 is out

    fake->release();
    REQUIRE(progress.waitForCompletes(1));
    CHECK(fake->calls().size() == 54);
    CHECK_FALSE(source.smallestBuiltLevel().isEmpty());
}

TEST_CASE("destroying during blocked reads returns promptly", "[infra][BuiltPyramidSource][parallel]")
{
    auto fake = std::make_shared<FakeSource>();
    auto source = std::make_unique<BuiltPyramidSource>(fake, kBuiltLevelBudgetBytes, 300'000, 4);
    std::atomic<int> callbacks{0};
    source->addOnBuiltRowsReady([&callbacks](bool) { ++callbacks; });

    fake->blockReads();
    source->start();
    REQUIRE(fake->waitUntilBlocked(4)); // every reader inside readBlock

    const auto started = std::chrono::steady_clock::now();
    std::atomic<bool> destroyed{false};
    std::thread destroyer([&source, &destroyed] {
        source.reset();
        destroyed = true;
    });
    // The destructor should be in its join when the reads return; no latch can
    // observe that; releasing early lets readers start more reads before the cancel lands, which the bound below allows.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    fake->release();
    destroyer.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(destroyed.load());
    CHECK(elapsed < std::chrono::seconds(2));
    CHECK(callbacks.load() == 0);
    // The four reads in flight plus at most the window (2 x 4) claimed before the cancel landed.
    CHECK(fake->calls().size() <= 8);
}

TEST_CASE("unreliable while chunks are in flight is abandoned", "[infra][BuiltPyramidSource][parallel]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000, 4);
    Progress progress;
    progress.attach(source);

    fake->blockReads();
    source.start();
    REQUIRE(fake->waitUntilBlocked(4));
    fake->markUnreliable(0);
    fake->release();
    REQUIRE(progress.waitForCompletes(1));

    // The four reads in flight, none after, and none of them fed.
    CHECK(fake->calls().size() == 4);
    CHECK_FALSE(source.builtOverviewPending());
    CHECK(source.smallestBuiltLevel().isEmpty());
    bool allErrors = true;
    for (const TileAnswer& answer : residentTiles(source)) {
        allErrors = allErrors && answer.ready && answer.error;
    }
    CHECK(allErrors);
    std::lock_guard<std::mutex> lock(progress.mutex);
    CHECK(progress.completes == 1);
    CHECK(progress.events.size() == 1); // no band was ever fed
}

TEST_CASE("a slide with fewer chunks than readers completes", "[infra][BuiltPyramidSource][parallel]")
{
    // 2100x600: one /2 resident level, two bands of one chunk each. (A 600x400
    // slide plans no built levels at all, so it would test nothing.)
    auto probe = std::make_shared<FakeSource>(2100, 600);
    REQUIRE(BuiltPyramidSource(probe).hasBuiltLevels());

    const BuildResult one =
        buildWith(std::make_shared<FakeSource>(2100, 600), 1, BuiltPyramidSource::kDefaultBandChunkMaxBytes);
    auto fakeFour = std::make_shared<FakeSource>(2100, 600);
    const BuildResult four = buildWith(fakeFour, 4, BuiltPyramidSource::kDefaultBandChunkMaxBytes);

    CHECK(fakeFour->calls().size() == 2);
    CHECK(errorTiles(four) == 0);
    CHECK(sameTiles(one, four));
    REQUIRE_FALSE(four.overviewEmpty);
    CHECK(one.overview == four.overview);
}

TEST_CASE("a mode change while four readers are inside reads restarts cleanly",
          "[infra][BuiltPyramidSource][parallel]")
{
    auto fake = std::make_shared<FakeSource>();
    BuiltPyramidSource source(fake, kBuiltLevelBudgetBytes, 1'000'000, 4);
    Progress progress;
    progress.attach(source);

    fake->blockReads();
    source.start(); // Raw
    REQUIRE(fake->waitUntilBlocked(4));

    std::thread toggler([&source] { source.setColorMode(ColorMode::Managed); });
    REQUIRE(fake->waitForMode(ColorMode::Managed));
    fake->release();
    toggler.join(); // returns once every Raw reader is joined and the Managed build launched
    REQUIRE(progress.waitForCompletes(1));

    // The four Raw reads in flight, all before any Managed read, then the 18 Managed chunks.
    const auto calls = fake->calls();
    REQUIRE(calls.size() == 22);
    for (size_t i = 0; i < calls.size(); ++i) {
        CHECK(calls[i].mode == (i < 4 ? ColorMode::Raw : ColorMode::Managed));
    }
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        CHECK(progress.completes == 1);
        CHECK(progress.events.size() == 7); // the Managed build's 6 bands and its completion
    }
    CHECK(source.readTile(TileKey(1, 0, 0, 0, 0, ColorMode::Raw)).isPending());
    const TileData tile = source.readTile(TileKey(1, 0, 0, 0, 0, ColorMode::Managed));
    CHECK_FALSE(tile.isPending());
    CHECK_FALSE(tile.isError());
    CHECK_FALSE(source.smallestBuiltLevel().isEmpty());
}
