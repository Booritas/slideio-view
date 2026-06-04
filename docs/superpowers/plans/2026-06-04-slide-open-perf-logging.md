# Slide-Open Performance Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in performance logging that records how long `::slideio::openSlide()` takes per call (5× per open) and the end-to-end wall-clock of the whole slide-open operation.

**Architecture:** A file-local `openSlideTimed()` helper in `SlideIOAdapter.cpp` wraps and times every `::slideio::openSlide()` call, emitting a `perf`-logger trace line tagged with the call context. `ViewportWidget::openSlide()` captures a start timestamp and logs the end-to-end duration once the slide is installed. All output is gated by the existing `perfLogEnabled()` guard, so it is free when perf logging is off.

**Tech Stack:** C++17, spdlog (`perf` logger), SlideIO, Qt 6, CMake + Conan.

**Build/test commands** (this machine — `conan` is not on the default PATH):
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build -C Release --output-on-failure
```
A successful build exits 0 and prints `-- Installing: .../slideio-viewer.exe`. If a `cmake_install.cmake` "cannot copy ... slideio-viewer.exe / Permission denied" error appears, a viewer process is locking the file — the compile itself is what matters (look for absence of `error C####` and other `CMake Error`s).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/infra/src/SlideIOAdapter.cpp` (modify) | Add `openSlideTimed()` helper; route all 3 `::slideio::openSlide()` call sites through it. |
| `src/ui/src/ViewportWidget.cpp` (modify) | Time the end-to-end open operation in `openSlide()`; log after the slide is installed. |

No new files, no test files (both changes wrap real slideio I/O with no pure logic to unit-test, consistent with the existing tile-read perf timing). Verified manually in Task 3.

---

## Task 1: Time each `::slideio::openSlide()` call (infra)

**Files:**
- Modify: `src/infra/src/SlideIOAdapter.cpp` (includes near top; add helper at the top of the `slideio::viewer::infra` namespace; 3 call-site edits at ~line 438, ~639, ~815)

No unit test: timing wraps real slideio I/O; there is no test fixture for a real slide in this repo. Verified by build + the manual run in Task 3.

- [ ] **Step 1: Add the PerfLog include**

In `src/infra/src/SlideIOAdapter.cpp`, after the `#include <spdlog/spdlog.h>` line (line 10), add:

```cpp
#include <spdlog/spdlog.h>

#include "slideio/viewer/infra/PerfLog.h"
```

- [ ] **Step 2: Add the `<chrono>` include**

In the standard-includes block (currently `<algorithm>`, `<cmath>`, ...), add `<chrono>` after `<algorithm>`:

```cpp
#include <algorithm>
#include <chrono>
#include <cmath>
```

- [ ] **Step 3: Add the `openSlideTimed` helper**

In `src/infra/src/SlideIOAdapter.cpp`, the line `namespace slideio::viewer::infra` opens the namespace (around line 428) followed by `{`. Immediately after that opening `{`, and before the `SlideIOAdapter::SlideIOAdapter(...)` constructor, insert:

```cpp
namespace slideio::viewer::infra
{

// Times a ::slideio::openSlide() call and, when perf logging is enabled, emits
// one trace line tagged with the call context (which open path triggered it).
// File-local: lives in the infra namespace so perfLog()/perfLogEnabled() resolve
// unqualified.
static std::shared_ptr<::slideio::Slide> openSlideTimed(const std::string& filePath,
                                                        const std::string& driverId,
                                                        const std::string& context)
{
    auto start = std::chrono::steady_clock::now();
    auto slide = ::slideio::openSlide(filePath, driverId);
    if (perfLogEnabled()) {
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        perfLog().trace("slideio::openSlide '{}' [{}] driver='{}': {:.1f}ms",
                        filePath, context, driverId, ms);
    }
    return slide;
}
```

> Note: place this AFTER the existing `namespace slideio::viewer::infra\n{` line. Do not add a second `namespace` line — reuse the existing one. The helper must appear before its first use (the scene constructor below it).

- [ ] **Step 4: Route the scene constructor through the helper**

In the scene constructor `SlideIOAdapter::SlideIOAdapter(const std::string& filePath, int sceneIndex, const std::string& driverId)`, replace:

```cpp
    m_slide = ::slideio::openSlide(filePath, driverId);
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    int numScenes = m_slide->getNumScenes();
```

with (only the first line changes; the null-check and following code stay):

```cpp
    m_slide = openSlideTimed(filePath, driverId, "scene " + std::to_string(sceneIndex));
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    int numScenes = m_slide->getNumScenes();
```

- [ ] **Step 5: Route the aux-image constructor through the helper**

In the aux-image constructor `SlideIOAdapter::SlideIOAdapter(const std::string& filePath, const std::string& auxImageName, const std::string& driverId)`, replace:

```cpp
    m_slide = ::slideio::openSlide(filePath, driverId);
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    m_scene = m_slide->getAuxImage(auxImageName);
```

with:

```cpp
    m_slide = openSlideTimed(filePath, driverId, "aux '" + auxImageName + "'");
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    m_scene = m_slide->getAuxImage(auxImageName);
```

- [ ] **Step 6: Route `enumerateScenes` through the helper**

In `SlideIOAdapter::enumerateScenes(...)`, replace:

```cpp
        auto slide = ::slideio::openSlide(filePath, driverId);
        if (!slide) {
            spdlog::error("SlideIOAdapter::enumerateScenes: failed to open slide '{}'", filePath);
            return {scenes, auxImages};
```

with:

```cpp
        auto slide = openSlideTimed(filePath, driverId, "enumerate");
        if (!slide) {
            spdlog::error("SlideIOAdapter::enumerateScenes: failed to open slide '{}'", filePath);
            return {scenes, auxImages};
```

- [ ] **Step 7: Build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: compiles cleanly (no `error C####`). `openSlideTimed` is used at all three sites.

- [ ] **Step 8: Run all tests**

Run:
```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 3/3 pass (`core-tests`, `infra-tests`, `ui-tests`) — unchanged by this task.

- [ ] **Step 9: Commit**

```bash
git add src/infra/src/SlideIOAdapter.cpp
git commit -m "Time each slideio::openSlide call via the perf logger"
```

---

## Task 2: Time the end-to-end open operation (ui)

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` (`openSlide()`, ~line 1764-1795)

No unit test: end-to-end timing spans real I/O on a worker thread. Verified by the manual run in Task 3. `PerfLog.h` and `<chrono>` are already included in this file (lines 17 and 19).

- [ ] **Step 1: Capture the start timestamp and thread it through to the install**

In `src/ui/src/ViewportWidget.cpp`, replace the body of `openSlide()` (from the opening brace through the `}).detach();` line) so the start time is captured and the end-to-end duration is logged once the slide is installed. Replace:

```cpp
void ViewportWidget::openSlide(const std::string& filePath, const std::string& driverId)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    m_impl->currentDriverId = driverId;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = slideDisplayName(filePath);
    emit loadingStarted(displayName);

    auto statusCallback = [this](QString msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            emit loadingStatusChanged(msg);
        }, Qt::QueuedConnection);
    };

    std::thread([this, opId, filePath, driverId, statusCallback]() {
        SceneOpenResult result = openSceneSync(filePath, 0, driverId, statusCallback);
        // Always enumerate scenes so the scene panel can populate, even if scene 0 worked.
        try {
            auto enumResult = infra::SlideIOAdapter::enumerateScenes(filePath, driverId);
            result.scenes = std::move(enumResult.first);
            result.auxImages = std::move(enumResult.second);
        } catch (const std::exception& ex) {
            spdlog::warn("openSlide: failed to enumerate scenes: {}", ex.what());
        }
        QMetaObject::invokeMethod(this,
            [this, opId, r = std::move(result)]() mutable {
                installSceneOpenResult(opId, std::move(r));
            }, Qt::QueuedConnection);
    }).detach();
}
```

with:

```cpp
void ViewportWidget::openSlide(const std::string& filePath, const std::string& driverId)
{
    const auto openStart = std::chrono::steady_clock::now();

    closeSlide();
    m_impl->currentFilePath = filePath;
    m_impl->currentDriverId = driverId;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = slideDisplayName(filePath);
    emit loadingStarted(displayName);

    auto statusCallback = [this](QString msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            emit loadingStatusChanged(msg);
        }, Qt::QueuedConnection);
    };

    std::thread([this, opId, openStart, filePath, driverId, statusCallback]() {
        SceneOpenResult result = openSceneSync(filePath, 0, driverId, statusCallback);
        // Always enumerate scenes so the scene panel can populate, even if scene 0 worked.
        try {
            auto enumResult = infra::SlideIOAdapter::enumerateScenes(filePath, driverId);
            result.scenes = std::move(enumResult.first);
            result.auxImages = std::move(enumResult.second);
        } catch (const std::exception& ex) {
            spdlog::warn("openSlide: failed to enumerate scenes: {}", ex.what());
        }
        QMetaObject::invokeMethod(this,
            [this, opId, openStart, filePath, r = std::move(result)]() mutable {
                installSceneOpenResult(opId, std::move(r));
                if (infra::perfLogEnabled()) {
                    double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - openStart).count();
                    infra::perfLog().trace("openSlide '{}' end-to-end: {:.1f}ms", filePath, ms);
                }
            }, Qt::QueuedConnection);
    }).detach();
}
```

> The end-to-end clock starts at `openSlide()` entry (capturing worker-spawn + queued-GUI-hop latency) and is read right after `installSceneOpenResult` returns — the point at which the slide is installed and `slideOpened` has been emitted. `filePath` is captured by copy in the inner lambda because `r` (the result) is moved into `installSceneOpenResult`.

- [ ] **Step 2: Build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: compiles cleanly.

- [ ] **Step 3: Run all tests**

Run:
```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 3/3 pass.

- [ ] **Step 4: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Log end-to-end slide-open time via the perf logger"
```

---

## Task 3: Manual verification

**Files:** none (runtime check). Use the installed build at
`build/install/release/bin/slideio-viewer.exe`. Log file:
`%LOCALAPPDATA%\SlideIO\SlideIO Viewer\logs\slideio-viewer.log`.

- [ ] **Step 1: Perf logging OFF (default)**

Launch the viewer with no env var and perf logging set to "Do not log" (Tools → Settings → Logs). Open a local slide. Inspect the log.
Expected: NO `slideio::openSlide` or `openSlide ... end-to-end` lines.

- [ ] **Step 2: Perf logging ON — local slide**

Enable perf logging (either `set SLIDEIO_PERF_LOG=1` before launching, or Settings → Logs → Performance logging → "Log performance data", Apply). Open a local slide. Inspect the log.
Expected:
- Several `slideio::openSlide '<path>' [scene 0] driver='...': <ms>ms` lines (one per pool adapter, ~4).
- One `slideio::openSlide '<path>' [enumerate] ...: <ms>ms` line.
- One `openSlide '<path>' end-to-end: <ms>ms` line.
- The first `[scene 0]` open is the largest (cold); later ones are smaller (warm).

- [ ] **Step 3: Perf logging ON — S3 slide**

With perf logging still on, open a slide from S3 (Open Slide from S3…). Inspect the log.
Expected: the per-open and end-to-end numbers are visibly larger than for the local file (remote I/O). Note the figures for the optimization work that motivated this.

- [ ] **Step 4: Sanity-check**

`end-to-end` should be ≥ the largest single `slideio::openSlide` time and roughly cover the sum of the cold open + coarse-level read + thumbnail + enumerate. Aux-image opens (if you open an associated image) log `[aux '<name>']`.

---

## Notes for the implementer

- **Namespace resolution:** `openSlideTimed` is placed inside `namespace slideio::viewer::infra`, so `perfLog()` / `perfLogEnabled()` resolve unqualified. In `ViewportWidget.cpp` (namespace `slideio::viewer::ui`) the calls are qualified `infra::perfLog()` / `infra::perfLogEnabled()`, matching the existing perf call sites in that file.
- **Cost when disabled:** two `steady_clock::now()` reads per open call and one pair for the end-to-end path — negligible against a slide open. All string formatting is behind `perfLogEnabled()`.
- **`std::to_string`** (used for the scene context) and `std::string` concatenation are already available in `SlideIOAdapter.cpp` (it uses `std::to_string` and `std::string` throughout).
- **`{}` format:** spdlog fmt-style placeholders (`{:.1f}` for milliseconds), consistent with existing perf lines.
```
