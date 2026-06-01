# Tile-Loading & Rendering Performance Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in performance logging that records, for each viewport change, the slide region to load, each tile's load time by coordinate, and the end-to-end time until the view is fully refined.

**Architecture:** A dedicated, off-by-default spdlog logger named `"perf"` (toggled by env var `SLIDEIO_PERF_LOG`). The scheduler times each `readTile` and reports per-tile timing through an extended tile-loaded callback. The controller logs the region-to-load. The widget aggregates per-cycle stats and logs the end-to-end refine time, anchored on the existing `updateRenderState` render-completeness transition.

**Tech Stack:** C++17, spdlog, Qt 6, Catch2 (tests), CMake + Conan.

**Build/test commands** (this machine — `conan` is not on the default PATH):
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build/Release -C Release --output-on-failure
```

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/infra/include/slideio/viewer/infra/PerfLog.h` (new) | Declares `perfLog()` accessor and `perfLogEnabled()` gate; forward-declares `spdlog::logger` to stay light |
| `src/infra/src/PerfLog.cpp` (new) | Resolves the registered `"perf"` logger; static no-op fallback so callers never branch on null |
| `tests/infra/PerfLogTest.cpp` (new) | Unit test: accessor never null; `perfLogEnabled()` follows the logger level |
| `tests/CMakeLists.txt` | Add `PerfLogTest.cpp` to the infra test target |
| `src/infra/CMakeLists.txt` | Add `PerfLog.cpp` to the infra library target |
| `src/main.cpp` | Register the `"perf"` logger; set its level from `SLIDEIO_PERF_LOG` |
| `src/infra/include/slideio/viewer/infra/TileLoadScheduler.h` | Extend `setOnTileLoaded` callback signature |
| `src/infra/src/TileLoadScheduler.cpp` | Time `readTile`, emit per-tile perf line, pass timing to callback |
| `src/ui/src/ViewportController.cpp` | Log region-to-load in `requestVisibleTiles()` |
| `src/ui/src/ViewportWidget.cpp` | Adapt callback; per-cycle stats; end-to-end + summary; paintGL CPU time |

---

## Task 1: PerfLog accessor (infra)

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/PerfLog.h`
- Create: `src/infra/src/PerfLog.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Test: `tests/infra/PerfLogTest.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/infra/PerfLogTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/PerfLog.h"

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

#include <memory>

using namespace slideio::viewer::infra;

TEST_CASE("perfLog returns a usable logger when none is registered", "[infra][PerfLog]")
{
    spdlog::drop("perf"); // ensure unregistered
    // Must not crash and must report disabled (fallback level is off).
    perfLog().trace("noop"); // no sink, no crash
    REQUIRE_FALSE(perfLogEnabled());
}

TEST_CASE("perfLogEnabled follows the registered logger level", "[infra][PerfLog]")
{
    spdlog::drop("perf");
    auto perf = std::make_shared<spdlog::logger>("perf"); // no sinks
    perf->set_level(spdlog::level::trace);
    spdlog::register_logger(perf);

    REQUIRE(perfLogEnabled());

    perf->set_level(spdlog::level::off);
    REQUIRE_FALSE(perfLogEnabled());

    spdlog::drop("perf"); // cleanup
}
```

- [ ] **Step 2: Wire the test into CMake**

In `tests/CMakeLists.txt`, change the infra test executable to include the new file:

```cmake
add_executable(slideio-viewer-infra-tests
    infra/LruTileCacheTest.cpp
    infra/PerfLogTest.cpp
)
```

- [ ] **Step 3: Run the test to verify it fails to build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: compile/link FAIL — `PerfLog.h` not found / `perfLog` undefined.

- [ ] **Step 4: Create the header**

Create `src/infra/include/slideio/viewer/infra/PerfLog.h`:

```cpp
#pragma once

namespace spdlog
{
class logger;
}

namespace slideio::viewer::infra
{

// Returns the process-wide "perf" logger used for tile-loading and rendering
// timing. Never null: when no "perf" logger is registered (e.g. unit tests or
// before main() sets one up), a static no-op logger at level::off is returned,
// so callers never have to branch on null.
spdlog::logger& perfLog();

// True when the perf logger would emit a trace-level record. Use to guard the
// construction of expensive log arguments (e.g. TileKey::toString()) so there
// is no cost when perf logging is disabled (the default).
bool perfLogEnabled();

} // namespace slideio::viewer::infra
```

- [ ] **Step 5: Create the implementation**

Create `src/infra/src/PerfLog.cpp`:

```cpp
#include "slideio/viewer/infra/PerfLog.h"

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

#include <memory>

namespace slideio::viewer::infra
{

spdlog::logger& perfLog()
{
    // Lazily-created no-op logger (no sinks, level off) used when the real
    // "perf" logger has not been registered.
    static std::shared_ptr<spdlog::logger> fallback = []
    {
        auto l = std::make_shared<spdlog::logger>("perf-null");
        l->set_level(spdlog::level::off);
        return l;
    }();

    if (auto registered = spdlog::get("perf"))
    {
        return *registered;
    }
    return *fallback;
}

bool perfLogEnabled()
{
    return perfLog().should_log(spdlog::level::trace);
}

} // namespace slideio::viewer::infra
```

- [ ] **Step 6: Add the source to the infra library**

In `src/infra/CMakeLists.txt`, add `src/PerfLog.cpp` to the `slideio-viewer-infra` source list. After the edit the top of the file reads:

```cmake
add_library(slideio-viewer-infra STATIC
    src/SlideIOAdapter.cpp
    src/SlideIOAdapterPool.cpp
    src/LruTileCache.cpp
    src/TileLoadScheduler.cpp
    src/Prefetcher.cpp
    src/PerfLog.cpp
```

- [ ] **Step 7: Build and run the tests to verify they pass**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build/Release -C Release -R infra-tests --output-on-failure
```
Expected: PASS — both PerfLog test cases pass, LruTileCache tests still pass.

- [ ] **Step 8: Commit**

```bash
git add src/infra/include/slideio/viewer/infra/PerfLog.h src/infra/src/PerfLog.cpp \
        src/infra/CMakeLists.txt tests/infra/PerfLogTest.cpp tests/CMakeLists.txt
git commit -m "Add perf logger accessor (off by default)"
```

---

## Task 2: Register the perf logger in main.cpp

**Files:**
- Modify: `src/main.cpp:28-37`

No unit test: this is process bootstrap, verified by the manual run in Task 6.

- [ ] **Step 1: Add `<cstdlib>` include**

At the top of `src/main.cpp`, add with the other standard includes (after the spdlog includes block):

```cpp
#include <cstdlib>
```

- [ ] **Step 2: Register the perf logger after the default logger is set**

In `src/main.cpp`, immediately after the existing line `spdlog::set_default_logger(logger);` (line 35) and before the `spdlog::info("SlideIO Viewer starting...")` line, insert:

```cpp
    // Dedicated perf logger for tile-loading / rendering timing. Shares the
    // same sinks as the main logger but is OFF by default — only emits when
    // SLIDEIO_PERF_LOG is set to a non-empty, non-"0" value. Keeps normal runs
    // free of the verbose per-tile/per-frame timing output.
    auto perfLogger = std::make_shared<spdlog::logger>("perf",
        spdlog::sinks_init_list{fileSink, consoleSink});
    const char* perfEnv = std::getenv("SLIDEIO_PERF_LOG");
    const bool perfOn = perfEnv && perfEnv[0] != '\0' && std::string(perfEnv) != "0";
    perfLogger->set_level(perfOn ? spdlog::level::trace : spdlog::level::off);
    perfLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(perfLogger);
    if (perfOn)
    {
        spdlog::info("Performance logging ENABLED (SLIDEIO_PERF_LOG set)");
    }
```

> Note: `<string>` and `<memory>` are already transitively available via the existing spdlog/Qt includes in `main.cpp`; if the build complains, add `#include <string>`.

- [ ] **Step 3: Build to verify it compiles**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Register perf logger, gated on SLIDEIO_PERF_LOG"
```

---

## Task 3: Time tile reads and extend the scheduler callback

**Files:**
- Modify: `src/infra/include/slideio/viewer/infra/TileLoadScheduler.h:49,64`
- Modify: `src/infra/src/TileLoadScheduler.cpp` (worker loop, ~line 173-189)

No new unit test: exercising `readTile` requires a real slide + adapter pool (no test fixture exists in this repo for that). Verified by build + the manual run in Task 6. The signature change is enforced by the compiler at the Task 5 call site.

- [ ] **Step 1: Change the callback type in the header**

In `src/infra/include/slideio/viewer/infra/TileLoadScheduler.h`, replace the `setOnTileLoaded` declaration (line 49):

```cpp
    void setOnTileLoaded(
        std::function<void(const core::TileKey&, double loadMs, size_t bytes, bool isError)> callback);
```

and the member field (line 64):

```cpp
    std::function<void(const core::TileKey&, double loadMs, size_t bytes, bool isError)> m_onTileLoaded;
```

- [ ] **Step 2: Update the setter definition**

In `src/infra/src/TileLoadScheduler.cpp`, replace the `setOnTileLoaded` definition (currently lines 124-127):

```cpp
void TileLoadScheduler::setOnTileLoaded(
    std::function<void(const core::TileKey&, double loadMs, size_t bytes, bool isError)> callback)
{
    m_onTileLoaded = std::move(callback);
}
```

- [ ] **Step 3: Add includes for timing and perf log**

At the top of `src/infra/src/TileLoadScheduler.cpp`, after the existing includes, add:

```cpp
#include "slideio/viewer/infra/PerfLog.h"

#include <chrono>
```

- [ ] **Step 4: Time the read, emit a per-tile line, and pass timing to the callback**

In `src/infra/src/TileLoadScheduler.cpp`, replace the read/insert/notify block in `workerLoop()` (currently lines 173-189, from the `// Read the tile` comment through the end of the `m_onTileLoaded` block):

```cpp
        // Read the tile, timing the read for perf logging.
        spdlog::trace("TileLoadScheduler: reading tile {}", request.key.toString());
        auto readStart = std::chrono::steady_clock::now();
        core::TileData tileData = loan->readTile(request.key);
        double loadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - readStart).count();

        size_t bytes = tileData.byteSize();
        bool isError = tileData.isError();

        if (perfLogEnabled())
        {
            perfLog().trace("readTile {}: {:.2f}ms {}B err={}",
                            request.key.toString(), loadMs, bytes, isError ? 1 : 0);
        }

        // Insert into cache (even error tiles, so we don't retry endlessly)
        auto tilePtr = std::make_shared<core::TileData>(std::move(tileData));
        m_tileCache->insert(request.key, tilePtr);

        // Notify callback
        if (m_onTileLoaded) {
            try {
                m_onTileLoaded(request.key, loadMs, bytes, isError);
            }
            catch (const std::exception& ex) {
                spdlog::warn("TileLoadScheduler: onTileLoaded callback threw: {}", ex.what());
            }
        }
```

> `byteSize()` and `isError()` are read *before* `tileData` is moved into `tilePtr`.

- [ ] **Step 5: Build (expect a failure at the UI call site)**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: infra compiles; the build FAILS in `ViewportWidget.cpp` because the existing `setOnTileLoaded` lambda takes only `(const core::TileKey&)`. That is fixed in Task 5. (If executing tasks strictly one-commit-at-a-time, proceed to Task 5 before committing; otherwise commit Tasks 3+5 together.)

- [ ] **Step 6: Commit (after Task 5 builds clean)**

```bash
git add src/infra/include/slideio/viewer/infra/TileLoadScheduler.h src/infra/src/TileLoadScheduler.cpp
git commit -m "Time tile reads and report timing via tile-loaded callback"
```

---

## Task 4: Log the region-to-load in the controller

**Files:**
- Modify: `src/ui/src/ViewportController.cpp` (`requestVisibleTiles()`, lines 109-129; add includes at top)

- [ ] **Step 1: Add includes**

At the top of `src/ui/src/ViewportController.cpp`, add (after the existing includes):

```cpp
#include "slideio/viewer/infra/PerfLog.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <limits>
```

- [ ] **Step 2: Emit the region line in `requestVisibleTiles()`**

In `src/ui/src/ViewportController.cpp`, replace the body of `requestVisibleTiles()` (lines 109-129) with:

```cpp
void ViewportController::requestVisibleTiles()
{
    if (!m_coordSystem || !m_scheduler) {
        return;
    }

    m_scheduler->cancelAll();

    auto visibleKeys = visibleTileKeys();

    std::vector<infra::TileRequest> visibleRequests;
    visibleRequests.reserve(visibleKeys.size());
    for (const auto& key : visibleKeys) {
        if (!m_cache || !m_cache->lookup(key)) {
            visibleRequests.push_back({key, infra::TilePriority::Visible});
        }
    }

    if (infra::perfLogEnabled() && !visibleKeys.empty()) {
        // Slide-coordinate bounding box of the current viewport (clamped to the
        // slide) = the image region that must be loaded to paint the view.
        double cx[4], cy[4];
        m_viewport.screenToSlide(0.0, 0.0, cx[0], cy[0]);
        m_viewport.screenToSlide(static_cast<double>(m_viewport.screenWidth()), 0.0, cx[1], cy[1]);
        m_viewport.screenToSlide(0.0, static_cast<double>(m_viewport.screenHeight()), cx[2], cy[2]);
        m_viewport.screenToSlide(static_cast<double>(m_viewport.screenWidth()),
                                 static_cast<double>(m_viewport.screenHeight()), cx[3], cy[3]);
        double minX = cx[0], maxX = cx[0], minY = cy[0], maxY = cy[0];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, cx[i]); maxX = std::max(maxX, cx[i]);
            minY = std::min(minY, cy[i]); maxY = std::max(maxY, cy[i]);
        }
        minX = std::max(0.0, minX);
        minY = std::max(0.0, minY);
        maxX = std::min(static_cast<double>(m_slideWidth), maxX);
        maxY = std::min(static_cast<double>(m_slideHeight), maxY);

        int level = visibleKeys.front().level();
        double scale = m_pyramid ? m_pyramid->levelInfo(level).scale : 0.0;

        int minCol = std::numeric_limits<int>::max(), maxCol = std::numeric_limits<int>::min();
        int minRow = std::numeric_limits<int>::max(), maxRow = std::numeric_limits<int>::min();
        for (const auto& key : visibleKeys) {
            minCol = std::min(minCol, key.column()); maxCol = std::max(maxCol, key.column());
            minRow = std::min(minRow, key.row()); maxRow = std::max(maxRow, key.row());
        }

        const size_t toLoad = visibleRequests.size();
        const size_t cached = visibleKeys.size() - toLoad;
        infra::perfLog().trace(
            "requestVisibleTiles: slideRect=({:.0f},{:.0f} {:.0f}x{:.0f}) level={} scale={:.4f} "
            "tiles=cols[{}..{}]xrows[{}..{}] visible={} cached={} toLoad={}",
            minX, minY, maxX - minX, maxY - minY, level, scale,
            minCol, maxCol, minRow, maxRow, visibleKeys.size(), cached, toLoad);
    }

    if (!visibleRequests.empty()) {
        m_scheduler->requestTiles(visibleRequests);
    }
}
```

- [ ] **Step 3: Build to verify it compiles**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: `ViewportController.cpp` compiles. (The overall build may still fail at the `ViewportWidget` callback site until Task 5 — that is expected.)

- [ ] **Step 4: Commit (after Task 5 builds clean)**

```bash
git add src/ui/src/ViewportController.cpp
git commit -m "Log viewport region-to-load for perf timing"
```

---

## Task 5: Per-cycle summary, end-to-end timing, and paintGL CPU time

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` — Impl struct (~line 1196), callback wiring (lines 2088-2094), `paintGL()` (lines 2365, 2429-2442, 2757-2774), `updateRenderState()` (lines 2777-2783); add includes at top.

- [ ] **Step 1: Add includes**

At the top of `src/ui/src/ViewportWidget.cpp`, add (with the other includes):

```cpp
#include "slideio/viewer/infra/PerfLog.h"

#include <chrono>
```

(`<mutex>` is already used by the existing `pendingUploadsMutex`; `spdlog` is already included.)

- [ ] **Step 2: Add per-cycle stat fields to Impl**

In `src/ui/src/ViewportWidget.cpp`, in the `Impl` struct just after the `bool lastRenderComplete = true;` line (line 1199), add:

```cpp
    // --- Perf: per-refinement-cycle tile-load stats (worker thread writes,
    // GUI thread reads on render-complete) ---
    std::mutex perfStatsMutex;
    int perfLoadedCount = 0;
    double perfSumMs = 0.0;
    double perfMaxMs = 0.0;
    size_t perfBytes = 0;
    std::chrono::steady_clock::time_point perfCycleStart{};
    bool perfCycleActive = false;
```

- [ ] **Step 3: Accumulate timing in the tile-loaded callback**

In `src/ui/src/ViewportWidget.cpp`, replace the `setOnTileLoaded` call (lines 2088-2094) with the new signature and accumulation:

```cpp
    m_impl->scheduler->setOnTileLoaded(
        [this](const core::TileKey& key, double loadMs, size_t bytes, bool /*isError*/) {
        {
            std::lock_guard<std::mutex> stats(m_impl->perfStatsMutex);
            m_impl->perfLoadedCount += 1;
            m_impl->perfSumMs += loadMs;
            m_impl->perfMaxMs = std::max(m_impl->perfMaxMs, loadMs);
            m_impl->perfBytes += bytes;
        }
        {
            std::lock_guard<std::mutex> lock(m_impl->pendingUploadsMutex);
            m_impl->pendingUploads.push_back(key);
        }
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    });
```

- [ ] **Step 4: Capture paintGL CPU start**

In `src/ui/src/ViewportWidget.cpp`, at the very start of `paintGL()` (just after line 2365 `{` and the early-return guard at 2367-2369 is fine to leave before this), add right after the `glViewport` setup or at the top of the function body:

```cpp
    auto paintStart = std::chrono::steady_clock::now();
```

Place it as the first statement inside `paintGL()` (line 2366), before the `if (!m_impl->gl ...)` guard, so it is always defined where used. (The early returns at lines 2368/2384/2446 do not log paint CPU time — those are no-op/empty frames.)

- [ ] **Step 5: Log paintGL CPU time at the throttled summary points**

In `src/ui/src/ViewportWidget.cpp`, replace the throttled summary block near the end of `paintGL()` (lines 2757-2759):

```cpp
    if (paintCount <= 5 || paintCount % 100 == 0) {
        double paintMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - paintStart).count();
        spdlog::info("paintGL[{}]: rendered={} skipped={}", paintCount, tilesRendered, tilesSkipped);
        if (infra::perfLogEnabled()) {
            infra::perfLog().trace("paintGL[{}] cpu={:.2f}ms rendered={} skipped={}",
                                   paintCount, paintMs, tilesRendered, tilesSkipped);
        }
    }
```

- [ ] **Step 6: Start/stop the end-to-end cycle in `updateRenderState`**

In `src/ui/src/ViewportWidget.cpp`, replace `updateRenderState()` (lines 2777-2783) with:

```cpp
void ViewportWidget::updateRenderState(bool fullyLoaded)
{
    if (fullyLoaded != m_impl->lastRenderComplete) {
        m_impl->lastRenderComplete = fullyLoaded;

        if (!fullyLoaded) {
            // Refinement begins: start the end-to-end clock and reset per-cycle
            // load stats.
            std::lock_guard<std::mutex> stats(m_impl->perfStatsMutex);
            m_impl->perfCycleStart = std::chrono::steady_clock::now();
            m_impl->perfLoadedCount = 0;
            m_impl->perfSumMs = 0.0;
            m_impl->perfMaxMs = 0.0;
            m_impl->perfBytes = 0;
            m_impl->perfCycleActive = true;
        } else if (m_impl->perfCycleActive) {
            // View fully refined: log end-to-end time + per-cycle load summary.
            double endToEndMs = 0.0;
            int loaded = 0;
            double sumMs = 0.0, maxMs = 0.0;
            size_t bytes = 0;
            {
                std::lock_guard<std::mutex> stats(m_impl->perfStatsMutex);
                endToEndMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - m_impl->perfCycleStart).count();
                loaded = m_impl->perfLoadedCount;
                sumMs = m_impl->perfSumMs;
                maxMs = m_impl->perfMaxMs;
                bytes = m_impl->perfBytes;
                m_impl->perfCycleActive = false;
            }
            if (infra::perfLogEnabled()) {
                double avgMs = loaded > 0 ? sumMs / loaded : 0.0;
                infra::perfLog().trace(
                    "viewport refined in {:.1f}ms: loaded={} avgLoad={:.2f}ms maxLoad={:.2f}ms "
                    "totalRead={:.2f}MB",
                    endToEndMs, loaded, avgMs, maxMs,
                    static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
        }

        emit renderStateChanged(fullyLoaded);
    }
}
```

> Note: the summary reports `loaded` (tiles actually read this cycle). The cached-tile count is already logged per request in Task 4's region line, so it is not duplicated here.

- [ ] **Step 7: Build to verify the whole project compiles**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: full build succeeds (this resolves the Task 3 callback-signature break).

- [ ] **Step 8: Run all tests**

Run:
```bash
ctest --test-dir build/build/Release -C Release --output-on-failure
```
Expected: all tests PASS.

- [ ] **Step 9: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Log end-to-end refine time, per-cycle tile-load summary, and paintGL CPU time"
```

If Tasks 3 and 4 were not committed (because the build was broken until now), commit them in their own commits first, in order:

```bash
git add src/infra/include/slideio/viewer/infra/TileLoadScheduler.h src/infra/src/TileLoadScheduler.cpp
git commit -m "Time tile reads and report timing via tile-loaded callback"
git add src/ui/src/ViewportController.cpp
git commit -m "Log viewport region-to-load for perf timing"
git add src/ui/src/ViewportWidget.cpp
git commit -m "Log end-to-end refine time, per-cycle tile-load summary, and paintGL CPU time"
```

---

## Task 6: Manual verification

**Files:** none (runtime check).

- [ ] **Step 1: Run with perf logging OFF (default)**

Launch the viewer normally (no env var), open a slide, pan and zoom. Open the log file (Help/File menu → "Open log file", per `MainWindow.cpp:119`).
Expected: NO `[perf]` / `requestVisibleTiles` / `readTile` / `viewport refined` lines. Normal `paintGL[...]` info lines still appear.

- [ ] **Step 2: Run with perf logging ON**

From a shell:
```bash
export SLIDEIO_PERF_LOG=1
# launch the built viewer executable from build/build/Release
```
Open a slide, pan and zoom. Inspect the log file.
Expected to see, per viewport change:
- `Performance logging ENABLED` once at startup.
- `requestVisibleTiles: slideRect=(...) level=... scale=... tiles=cols[..]xrows[..] visible=N cached=C toLoad=T`
- One `readTile L<lvl>(<col>,<row>) ...: X.XXms NB err=0` line per loaded tile.
- `paintGL[...] cpu=...ms rendered=... skipped=...` at the throttled cadence.
- `viewport refined in ...ms: loaded=N avgLoad=...ms maxLoad=...ms totalRead=...MB` once the view stops refining.

- [ ] **Step 3: Confirm timings are sane**

`avgLoad` × `loaded` should be in the same ballpark as `endToEnd` divided by worker count; a remote (S3) slide should show visibly larger per-tile `readTile` times than a local file. Note anomalies for the optimization work that motivated this feature.

---

## Notes for the implementer

- **Layering:** `PerfLog.h` lives in **infra**; both infra (`TileLoadScheduler`) and ui (`ViewportController`, `ViewportWidget`) already depend on infra, so the include is legal in all three. The core library is untouched and stays Qt-/spdlog-free.
- **Cost when disabled:** every emission is wrapped in `perfLogEnabled()`. The only unconditional additions are two `steady_clock::now()` reads per tile and per logged paint, plus the per-tile callback accumulation under a short-held mutex — all negligible.
- **`{}` format args:** spdlog uses fmt-style `{}` / `{:.2f}` placeholders, consistent with existing call sites in this file.
