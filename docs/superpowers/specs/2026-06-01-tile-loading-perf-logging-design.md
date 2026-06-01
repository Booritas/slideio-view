# Tile-Loading & Rendering Performance Logging — Design

**Date:** 2026-06-01
**Status:** Approved

## Goal

Add instrumentation to the viewer's tile-loading and rendering pipeline to support
performance optimization. The log must answer three questions for any viewport change:

1. **What region must be loaded?** The image rectangle (in slide coordinates), the
   chosen pyramid level, and the set of tiles needed to paint the current viewport.
2. **How long does each tile take?** Per-tile load timing keyed by tile coordinates.
3. **How long until the view is sharp?** End-to-end time from a viewport change until
   every visible tile is loaded and fully painted, plus per-`paintGL` CPU time.

Because tile loading runs on background worker threads and is decoupled from painting,
the end-to-end metric is anchored on the user-perceived render-completeness transition,
not on a single `paintGL` call.

## Background — current pipeline

- `ViewportController::requestVisibleTiles()` computes the visible tile keys (via
  `CoordinateSystem::visibleTiles`), cancels prior requests, and enqueues the
  not-yet-cached tiles to the scheduler. This is where the region-to-load is known.
- `TileLoadScheduler::workerLoop()` (infra) pops requests on worker threads and calls
  `loan->readTile(key)`, inserting results into the cache. This is where each tile is
  actually read — the place to time individual loads. Cache hits are short-circuited
  before the read.
- The scheduler notifies the UI via `setOnTileLoaded(callback(key))`; the callback
  runs on the worker thread and marshals a repaint to the GUI thread via
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- `ViewportWidget::paintGL()` uploads pending textures and renders visible tiles. When
  tiles are still missing it calls `updateRenderState(false)` and schedules another
  repaint; when all visible tiles are on screen it calls `updateRenderState(true)`.
- `ViewportWidget::updateRenderState(bool)` is the single chokepoint for
  render-completeness transitions and emits `renderStateChanged` only on a flip.
- Logging uses spdlog: `main.cpp` installs a rotating-file sink + colored stderr sink
  on a default logger named `"viewer"` at `level::debug`, `flush_on(info)`.

## Design decisions (confirmed with user)

- **Render timing:** End-to-end (viewport change → fully refined), *plus* per-`paintGL`
  CPU time as a separate metric.
- **Verbosity control:** A dedicated `perf` spdlog logger, off by default, toggled on
  for optimization sessions. No spam during normal use.
- **Granularity:** One line per tile load (coordinates + timing) *and* a per-viewport
  aggregate summary.

## Components

### 1. `perf` logger and accessor — `src/infra/.../PerfLog.h` (+ `.cpp`)

A new header in **infra** (infra already links spdlog; both the scheduler in infra and
the controller/widget in ui can include it — ui depends on infra). Provides:

```cpp
namespace slideio::viewer::infra
{
    // Returns the registered "perf" logger. Never null: if not registered (e.g. unit
    // tests), lazily creates a no-op logger at level::off so call sites never branch
    // on null.
    spdlog::logger& perfLog();

    // True when perf logging is enabled (perf logger level <= trace). Use to guard
    // construction of expensive log arguments (e.g. toString()).
    bool perfLogEnabled();
}
```

- `perfLog()` looks up `spdlog::get("perf")`; if absent, returns a static fallback
  logger created with no sinks at `level::off`.
- `perfLogEnabled()` returns `perfLog().should_log(spdlog::level::trace)`.

**Registration — `main.cpp`:** after the default `viewer` logger is set up, create a
`perf` logger sharing the same sinks, register it with `spdlog::register_logger`, and
set its level from the environment:

```cpp
auto perf = std::make_shared<spdlog::logger>("perf",
    spdlog::sinks_init_list{fileSink, consoleSink});
const char* env = std::getenv("SLIDEIO_PERF_LOG");
perf->set_level((env && env[0] && env[0] != '0') ? spdlog::level::trace
                                                 : spdlog::level::off);
perf->flush_on(spdlog::level::trace);
spdlog::register_logger(perf);
```

Default off ⇒ zero behavior change unless `SLIDEIO_PERF_LOG=1` is set.

### 2. Region-to-load logging — `ViewportController::requestVisibleTiles()`

After computing `visibleKeys` and partitioning cached vs. need-loading, emit one line
(guarded by `perfLogEnabled()`):

- Viewport slide-coordinate rectangle: top-left `(x, y)` and size `(w, h)`, derived
  from the viewport's screen→slide mapping of the four screen corners (axis-aligned
  bounding box, clamped to slide bounds).
- Pyramid level chosen for this viewport and its scale.
- Tile column/row range: `[minCol..maxCol] x [minRow..maxRow]`.
- Counts: total visible tiles, already-cached, need-loading.

Example:
```
[perf] requestVisibleTiles: slideRect=(12000,8000 4096x3000) level=2 scale=0.25
       tiles=cols[3..6]xrows[2..4] visible=12 cached=5 toLoad=7
```

The cached count seeds the per-cycle summary's "cache hits" figure.

### 3. Per-tile load lines — `TileLoadScheduler::workerLoop()`

Wrap the `readTile` call with `std::chrono::steady_clock`:

```cpp
auto t0 = std::chrono::steady_clock::now();
core::TileData tileData = loan->readTile(request.key);
auto loadMs = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - t0).count();
```

Emit one line per read (guarded): tile key string (level/col/row/z/t — the
coordinates), `loadMs`, byte size of the tile data, and error flag. Example:
```
[perf] readTile L2(5,3) z0 t0: 8.42ms 786432B err=0
```

Cache-hit short-circuits remain unlogged here (counted in the summary instead).

### 4. Extended tile-loaded callback

Change the scheduler callback signature so the UI can aggregate timings:

```cpp
// TileLoadScheduler.h
void setOnTileLoaded(std::function<void(const core::TileKey&,
                                        double loadMs,
                                        size_t bytes,
                                        bool isError)> callback);
```

The worker passes the measured `loadMs`, `tilePtr->byteSize()`, and the tile's
`isError()` state. The single existing call site in `ViewportWidget` is updated.

### 5. Per-cycle summary + end-to-end timing — `ViewportWidget`

Add to `ViewportWidget::Impl`:
- A small POD of per-cycle stats: `int loadedCount; double sumMs, maxMs; size_t bytes;`
  plus a `std::mutex` (the callback runs on a worker thread).
- A `steady_clock::time_point cycleStart;` and a `bool cycleActive;`.

Wiring:
- **Callback** (`setOnTileLoaded`): under the stats mutex, accumulate
  `loadedCount`, `sumMs += loadMs`, `maxMs = max(...)`, `bytes += ...`. Then do the
  existing pending-upload push + queued `update()`.
- **`updateRenderState(false)`** (refinement begins — `true→false` flip): capture
  `cycleStart = steady_clock::now()`, reset the stats POD, set `cycleActive = true`.
- **`updateRenderState(true)`** (fully refined — `false→true` flip): if `cycleActive`,
  compute `endToEndMs = now - cycleStart`, read the stats under the mutex, and log the
  summary; set `cycleActive = false`.

Because the transition logic already lives in `updateRenderState`, restarts (viewport
moves again mid-refine) reset the clock automatically.

Summary example:
```
[perf] viewport refined in 142.7ms: loaded=7 cacheHits=5 avg=11.3ms max=28.1ms
       totalRead=5.4MB
```

(`cacheHits` carried from the most recent `requestVisibleTiles` partition.)

### 6. Per-`paintGL` CPU time — `ViewportWidget::paintGL()`

At the top of `paintGL`, capture `steady_clock::now()`; near the end, log the elapsed
wall-clock to the `perf` logger. Reuse the existing throttle (`paintCount <= 5 ||
paintCount % 100 == 0`) so it does not flood, and note `tilesRendered/tilesSkipped`.
Example:
```
[perf] paintGL[143] cpu=2.1ms rendered=12 skipped=0
```

## Files touched

| File | Change |
|------|--------|
| `src/infra/include/slideio/viewer/infra/PerfLog.h` (new) | `perfLog()` / `perfLogEnabled()` accessors |
| `src/infra/src/PerfLog.cpp` (new) | accessor impl + static fallback logger |
| `src/main.cpp` | register `perf` logger, read `SLIDEIO_PERF_LOG` env var |
| `src/infra/include/.../TileLoadScheduler.h` | extended callback signature |
| `src/infra/src/TileLoadScheduler.cpp` | time `readTile`, per-tile lines, pass timing to callback |
| `src/ui/src/ViewportController.cpp` | region-to-load log in `requestVisibleTiles()` |
| `src/ui/src/ViewportWidget.cpp` | per-cycle stats, end-to-end + summary log, paintGL CPU time, new callback signature |
| `src/infra/CMakeLists.txt` | add `PerfLog.cpp` to the infra target |

## Error handling & edge cases

- **Logger absent (tests, headless):** `perfLog()` returns a static no-op `level::off`
  logger; nothing crashes and nothing is written.
- **Thread safety:** per-cycle stats are mutated from worker threads (callback) and read
  from the GUI thread (`updateRenderState`); guarded by a dedicated mutex.
- **Restarts mid-refine:** handled by the existing `true↔false` transition logic; each
  new refinement resets `cycleStart` and stats.
- **Zero-tile viewports / no slide open:** `updateRenderState(true)` is called with no
  active cycle (`cycleActive == false`) ⇒ no summary line, no spurious timing.
- **Error tiles:** still logged per-tile with `err=1` and counted in `loaded`, matching
  the existing "cache even error tiles" behavior.
- **Performance when disabled:** all emission is gated by `perfLogEnabled()` /
  `should_log`; the only unconditional cost is two `steady_clock::now()` reads per tile
  and per paint, which is negligible.

## Testing

- **Unit (infra):** `PerfLog` accessor returns a usable logger and never null; toggling
  the registered logger's level flips `perfLogEnabled()`. Scheduler still loads tiles
  and invokes the (extended) callback with a non-negative `loadMs` and correct
  `isError` for an out-of-range key.
- **Manual:** run with `SLIDEIO_PERF_LOG=1`, open a slide, pan/zoom, and confirm the log
  file contains region lines, per-tile lines, per-cycle summaries, and paintGL CPU
  lines; run without the env var and confirm none appear.

## Out of scope (YAGNI)

- Runtime UI toggle / menu item for perf logging (env var is sufficient for an
  optimization session).
- CSV/structured export or in-app perf overlay.
- Timing of GPU upload/draw beyond the single paintGL CPU measurement.
