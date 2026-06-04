# Slide-Open Performance Logging — Design

**Date:** 2026-06-04
**Status:** Approved (design)

## Goal

Add opt-in performance logging that answers "how long does it take to open a slide",
at two granularities:

1. **Raw library open** — the duration of each `::slideio::openSlide()` call.
2. **End-to-end open operation** — the total wall-clock from `ViewportWidget::openSlide()`
   until the slide is installed and ready to display.

## Non-Goals

- No new UI. Reuses the existing `perf` logger and its on/off control
  (`SLIDEIO_PERF_LOG` env var or Settings → Logs → Performance logging).
- No timing of scene-thumbnail generation (a separate, later background step).
- No timing of tile loading / viewport refinement (already covered by the existing
  "viewport refined" perf metric).

## Background

Opening a slide (`ViewportWidget::openSlide`) runs on a detached worker thread and:
1. Creates a `SlideIOAdapterPool` of 4 adapters — each `SlideIOAdapter` constructor
   calls `::slideio::openSlide(filePath, driverId)` (4 opens).
2. Acquires an adapter, reads a coarse pyramid level, and builds a thumbnail.
3. Calls `SlideIOAdapter::enumerateScenes`, which opens the slide once more (1 open).
4. Posts `installSceneOpenResult` to the GUI thread, which installs the pool/cache/
   pyramid, creates the scheduler/controller, sets `slideOpen = true`, and emits
   `slideOpened` — the point at which the slide is ready.

So `::slideio::openSlide()` is invoked 5× per open. The first call is cold; later
calls on the same file are typically warm.

The `perf` logger (spdlog logger named `"perf"`, OFF by default) and its guards
`slideio::viewer::infra::perfLog()` / `perfLogEnabled()` already exist and are used
for per-tile timing. This feature reuses them, so all new output is suppressed at zero
cost unless perf logging is enabled.

## Component 1 — Raw `::slideio::openSlide()` timing (infra)

**File:** `src/infra/src/SlideIOAdapter.cpp`

Add a free helper in the existing anonymous namespace:

```cpp
std::shared_ptr<::slideio::Slide> openSlideTimed(const std::string& filePath,
                                                 const std::string& driverId,
                                                 const std::string& context);
```

It times the `::slideio::openSlide(filePath, driverId)` call with
`std::chrono::steady_clock` and, only when `perfLogEnabled()`, emits one trace line via
`perfLog()`:

```
slideio::openSlide '<filePath>' [<context>] driver='<driverId>': <ms>ms
```

It returns the slide handle unchanged; callers keep their existing null-check/throw.

Replace the three direct `::slideio::openSlide(...)` call sites with `openSlideTimed`:
- Scene constructor (`SlideIOAdapter::SlideIOAdapter(filePath, sceneIndex, driverId)`):
  context `"scene " + std::to_string(sceneIndex)`.
- Aux-image constructor: context `"aux '" + auxImageName + "'"`.
- `enumerateScenes`: context `"enumerate"`.

New includes in `SlideIOAdapter.cpp`: `"slideio/viewer/infra/PerfLog.h"` and `<chrono>`.
(`<string>` and `<memory>` are already available; `PerfLog` is in the same `infra`
library.)

## Component 2 — End-to-end open timing (ui)

**File:** `src/ui/src/ViewportWidget.cpp` (`openSlide()`)

- Capture `const auto openStart = std::chrono::steady_clock::now();` as the first
  statement of `openSlide()`.
- Capture `openStart` by value in the worker `std::thread` lambda, and again in the
  GUI-thread `QMetaObject::invokeMethod` lambda that calls `installSceneOpenResult`.
- Immediately after `installSceneOpenResult(opId, std::move(r))` returns, when
  `perfLogEnabled()`, log:

```
openSlide '<filePath>' end-to-end: <ms>ms
```

Measuring from `openSlide()` entry includes worker-thread spawn latency and the queued
GUI hop, which are part of the user-perceived open time. Logging after
`installSceneOpenResult` is correct whether the open succeeded or failed (a failed open
returns early inside `installSceneOpenResult`); the elapsed still reflects the time
spent. spdlog already logs success/failure separately, so the end-to-end line does not
repeat that.

No new includes: `"slideio/viewer/infra/PerfLog.h"` and `<chrono>` are already included
in `ViewportWidget.cpp` (added for the tile-loading perf work).

## Data Flow

```
openSlide() [capture openStart]
  └─ worker thread (captures openStart)
       ├─ openSceneSync → SlideIOAdapterPool(4) → 4x SlideIOAdapter ctor
       │                                              └─ openSlideTimed("scene 0")  → perf line
       │     └─ coarse-level read + thumbnail
       ├─ enumerateScenes → openSlideTimed("enumerate")                            → perf line
       └─ invokeMethod (captures openStart)
            └─ installSceneOpenResult(...)   [slide installed, slideOpened emitted]
               └─ log "openSlide ... end-to-end: <ms>ms"                            → perf line
```

## Error Handling

- `openSlideTimed` does not swallow exceptions: if `::slideio::openSlide` throws, the
  exception propagates exactly as today (the helper does not wrap the call in try/catch).
  When it returns null, the existing caller-side null-check throws as before.
- The end-to-end timing logs regardless of open success; on failure the value reflects
  time-to-failure, which is still useful.

## Testing

- **No unit test.** Both timings wrap real slideio I/O; there is no pure logic to test
  (consistent with the existing tile-read perf timing, which is also manually verified).
- **Manual verification** (with perf logging enabled via `SLIDEIO_PERF_LOG=1` or
  Settings → Logs → Performance logging):
  - Open a local slide. The log shows multiple
    `slideio::openSlide '<path>' [scene 0] ...: <ms>ms` lines (one per pool adapter),
    a `[enumerate]` line, and one `openSlide '<path>' end-to-end: <ms>ms` line.
  - The first open of a file is cold (largest); subsequent opens of the same file in
    the same pool are warmer.
  - Open an S3 (remote) slide: per-open and end-to-end numbers are visibly larger than
    for a local file.
  - With perf logging OFF (default), none of these lines appear.

## Notes for the implementer

- **Layering:** `PerfLog.h` is in `infra`; `SlideIOAdapter` (infra) and `ViewportWidget`
  (ui, which depends on infra) may both include it.
- **Cost when disabled:** each `openSlideTimed` call always reads `steady_clock` twice
  (negligible vs a slide open) and branches on `perfLogEnabled()` before formatting. The
  end-to-end path adds one `steady_clock` read at `openSlide()` entry and one before the
  guarded log. All formatting is skipped when perf logging is off.
- **`{}` format:** spdlog fmt-style placeholders (`{:.1f}` for milliseconds), consistent
  with existing perf call sites.
```
