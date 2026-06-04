# Perf Logging for Non-Trivial slideio Calls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Time the non-trivial `slideio` library calls (metadata reads, driver/scene/aux enumeration and construction) via the existing `perf` logger, so their cost is visible when perf logging is on.

**Architecture:** A file-local `timeSlideio()` template in `SlideIOAdapter.cpp` wraps a slideio call, returns its result, and emits a `perf` trace line (`slideio::<method>: <ms>ms`) when `perfLogEnabled()`. Ten call sites are routed through it. Trivial scalar getters, `openSlide` (already timed via `openSlideTimed`), and the tile reads (already timed by `TileLoadScheduler`) are left untouched.

**Tech Stack:** C++17, spdlog (`perf` logger), SlideIO, CMake + Conan.

**Build/test commands** (this machine — `conan` is not on the default PATH):
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build -C Release --output-on-failure
```
A successful build exits 0 and prints `-- Installing: .../slideio-viewer.exe`. A trailing `cmake_install.cmake` "cannot copy ... slideio-viewer.exe / Permission denied" means a viewer is running and locking the file — the compile is what matters (no `error C####`, no other `CMake Error`).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/infra/src/SlideIOAdapter.cpp` (modify) | Add the `timeSlideio()` template helper; route the 10 non-trivial slideio calls through it. |

No new files, no test files (the wrapper times real slideio I/O; no pure logic to unit-test, consistent with `openSlideTimed`). Verified manually in Task 2.

Includes already present in this file (from the open-timing work): `"slideio/viewer/infra/PerfLog.h"`, `<chrono>`, `<type_traits>`. No include changes needed.

---

## Task 1: Add `timeSlideio` helper and route the 10 call sites

**Files:**
- Modify: `src/infra/src/SlideIOAdapter.cpp` (add helper near `openSlideTimed`; 10 call-site edits)

No unit test: wraps real slideio I/O; verified by build + the manual run in Task 2.

- [ ] **Step 1: Add the `timeSlideio` template helper**

In `src/infra/src/SlideIOAdapter.cpp`, the `openSlideTimed` helper sits at the top of
`namespace slideio::viewer::infra`. Immediately AFTER `openSlideTimed`'s closing `}`
(and before the first `SlideIOAdapter::SlideIOAdapter(...)` constructor), insert:

```cpp
// Times any slideio call and, when perf logging is enabled, emits one trace line
// tagged with the method name. Returns fn()'s result unchanged; supports void.
// File-local: lives in the infra namespace so perfLog()/perfLogEnabled() resolve
// unqualified.
template <typename F>
static auto timeSlideio(const char* method, F&& fn) -> decltype(fn())
{
    using Ret = decltype(fn());
    auto start = std::chrono::steady_clock::now();
    if constexpr (std::is_void_v<Ret>) {
        fn();
        if (perfLogEnabled()) {
            double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            perfLog().trace("slideio::{}: {:.1f}ms", method, ms);
        }
    } else {
        Ret result = fn();
        if (perfLogEnabled()) {
            double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            perfLog().trace("slideio::{}: {:.1f}ms", method, ms);
        }
        return result;
    }
}
```

- [ ] **Step 2: Wrap `getScene` in the scene constructor**

Replace (in the scene constructor `SlideIOAdapter::SlideIOAdapter(filePath, sceneIndex, driverId)`):

```cpp
    m_scene = m_slide->getScene(sceneIndex);
```

with:

```cpp
    m_scene = timeSlideio("Slide::getScene", [&]{ return m_slide->getScene(sceneIndex); });
```

- [ ] **Step 3: Wrap both `getMetadata` lines (covers all 4 occurrences)**

The metadata block appears verbatim in BOTH the scene constructor and the aux-image
constructor (two identical copies). Use a replace-all so both copies are updated. Replace
(all occurrences):

```cpp
        m_slideInfo.slideMetadata = convertMetadata(m_slide->getMetadata());
        m_slideInfo.sceneMetadata = convertMetadata(m_scene->getMetadata());
```

with:

```cpp
        m_slideInfo.slideMetadata = convertMetadata(
            timeSlideio("Slide::getMetadata", [&]{ return m_slide->getMetadata(); }));
        m_slideInfo.sceneMetadata = convertMetadata(
            timeSlideio("Scene::getMetadata", [&]{ return m_scene->getMetadata(); }));
```

> There are two identical copies of this two-line block (scene ctor ~641-642, aux ctor
> ~818-819). A replace-all updates both; if editing one at a time, apply the same change
> to each copy. Both remain inside their existing `try { ... } catch` blocks.

- [ ] **Step 4: Wrap `getAuxImage` in the aux-image constructor**

Replace (in `SlideIOAdapter::SlideIOAdapter(filePath, auxImageName, driverId)`):

```cpp
    m_scene = m_slide->getAuxImage(auxImageName);
```

with:

```cpp
    m_scene = timeSlideio("Slide::getAuxImage", [&]{ return m_slide->getAuxImage(auxImageName); });
```

- [ ] **Step 5: Wrap `getScene` in `enumerateScenes`**

Replace (inside the scene-enumeration loop in `SlideIOAdapter::enumerateScenes`):

```cpp
                auto scene = slide->getScene(i);
```

with:

```cpp
                auto scene = timeSlideio("Slide::getScene", [&]{ return slide->getScene(i); });
```

- [ ] **Step 6: Wrap `getAuxImageNames` in `enumerateScenes`**

Replace:

```cpp
        auto auxNames = slide->getAuxImageNames();
```

with:

```cpp
        auto auxNames = timeSlideio("Slide::getAuxImageNames", [&]{ return slide->getAuxImageNames(); });
```

- [ ] **Step 7: Wrap `getAuxImage` in `enumerateScenes`**

Replace (inside the aux-enumeration loop):

```cpp
                auto auxScene = slide->getAuxImage(auxName);
```

with:

```cpp
                auto auxScene = timeSlideio("Slide::getAuxImage", [&]{ return slide->getAuxImage(auxName); });
```

- [ ] **Step 8: Wrap `getDriverIDs` in `availableDriverIds`**

Replace (in `SlideIOAdapter::availableDriverIds`):

```cpp
        return ::slideio::getDriverIDs();
```

with:

```cpp
        return timeSlideio("getDriverIDs", []{ return ::slideio::getDriverIDs(); });
```

> This lambda captures nothing (`[]`) because `::slideio::getDriverIDs` is a free function.

- [ ] **Step 9: Build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: clean compile (no `error C####`). All wrapped calls compile (the template
deduces each return type).

- [ ] **Step 10: Run all tests**

Run:
```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 3/3 pass (`core-tests`, `infra-tests`, `ui-tests`) — unchanged by this task.

- [ ] **Step 11: Confirm the wrapped set is complete**

Run:
```bash
grep -nE "getScene\(|getMetadata\(|getAuxImage\(|getAuxImageNames\(|getDriverIDs\(" src/infra/src/SlideIOAdapter.cpp
```
Expected: every match (other than the ones inside `timeSlideio(...)` lambdas) is now
wrapped — i.e. each `getScene`/`getMetadata`/`getAuxImage`/`getAuxImageNames`/`getDriverIDs`
call appears as `timeSlideio(..., [&]{ return ...; })` (or `[]` for getDriverIDs). The
trivial getters (`getRect`, `getNumChannels`, etc.) are intentionally NOT wrapped.

- [ ] **Step 12: Commit**

```bash
git add src/infra/src/SlideIOAdapter.cpp
git commit -m "Time non-trivial slideio calls via the perf logger"
```

---

## Task 2: Manual verification

**Files:** none (runtime check). Use the installed build at
`build/install/release/bin/slideio-viewer.exe`. Log file:
`%LOCALAPPDATA%\SlideIO\SlideIO Viewer\logs\slideio-viewer.log`.

- [ ] **Step 1: Perf logging OFF (default)**

Launch with no env var and perf logging set to "Do not log" (Tools → Settings → Logs).
Open a local slide. Inspect the log.
Expected: NO `slideio::Slide::getMetadata` / `slideio::Scene::getMetadata` /
`slideio::Slide::getScene` / `slideio::getDriverIDs` lines.

- [ ] **Step 2: Perf logging ON — local slide**

Enable perf logging (`set SLIDEIO_PERF_LOG=1` before launch, or Settings → Logs →
Performance logging → "Log performance data", Apply). Open a local slide. Inspect the log.
Expected, alongside the existing `slideio::openSlide` and `openSlide ... end-to-end` lines:
- `slideio::Slide::getScene: <ms>ms`
- `slideio::Slide::getMetadata: <ms>ms` and `slideio::Scene::getMetadata: <ms>ms`
- `slideio::Slide::getAuxImageNames: <ms>ms` and `slideio::Slide::getAuxImage: <ms>ms`
  (from `enumerateScenes`, if the slide has aux images)
- `slideio::getDriverIDs: <ms>ms` (emitted when the driver list is queried, e.g. opening
  the file dialog or at the relevant code path).

- [ ] **Step 3: Sanity-check**

Metadata parse times should generally exceed the (un-logged) trivial getters; remote
(S3) slides should show larger figures than local files. No trivial-getter lines
(`getRect`, `getNumChannels`, etc.) should appear.

---

## Notes for the implementer

- **No new includes:** `PerfLog.h`, `<chrono>`, and `<type_traits>` are already included
  in `SlideIOAdapter.cpp` (added with the open-timing work). Verify they are present
  before assuming; if `<type_traits>` were missing, add it in the std-includes block.
- **Helper placement:** `timeSlideio` must be defined before its first use (the scene
  constructor's `getScene`, ~line 475). Placing it right after `openSlideTimed` at the
  top of `namespace slideio::viewer::infra` satisfies this.
- **Why a template (not std::function):** `timeSlideio` is header-free, zero-allocation,
  and deduces each call's return type; `if constexpr (std::is_void_v<...>)` keeps it
  usable for any future void call without special-casing at the call site.
- **Lambda capture:** `[&]` for member-scene/slide calls (synchronous, no escape);
  `[]` for the free `::slideio::getDriverIDs`.
- **DRY vs `openSlideTimed`:** leave `openSlideTimed` as-is (it logs richer context —
  file path, driver, open context); `timeSlideio` is the generic wrapper for the rest.
- **`{}` format:** `{:.1f}` for milliseconds, matching existing perf lines.
```
