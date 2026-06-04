# Settings → Performance Tab: Image-Reading Thread Pool — Design

**Date:** 2026-06-04
**Status:** Approved (design)

## Goal

Add a second tab, **Performance**, to the Settings dialog that lets the user set the
size of the image-reading thread pool. One value drives both the `SlideIOAdapterPool`
size (concurrent slideio readers) and the `TileLoadScheduler` worker-thread count, so it
genuinely controls read concurrency end-to-end. The value persists via `QSettings` and
takes effect on the next opened slide.

## Non-Goals

- No live resize of an already-open slide (the pool is created at open time; a new value
  applies to the next open / scene switch).
- No separate scheduler-worker setting — the worker count tracks the pool size.
- No change to the existing Logs tab behavior.

## Background

- `ViewportWidget` opens slides on worker threads. Two helpers in an anonymous namespace,
  `openSceneSync` and `openAuxImageSync`, each construct a `SlideIOAdapterPool` with a
  hardcoded size of `4` (`ViewportWidget.cpp:1067` and `:1120`). Each adapter is one
  concurrent slideio reader, so the pool size bounds read concurrency.
- `installSceneOpenResult` (a `ViewportWidget` member, ui namespace) creates the
  `TileLoadScheduler` from the pool with the default `numWorkers` (0 → `max(2, hw-2)`).
  Workers acquire an adapter per read, so workers beyond the pool size block on
  `acquire()` — the pool size is the real limiter.
- The Settings dialog (`SettingsDialog`) hosts a `QTabWidget` with one tab (`LogsTab`);
  OK/Apply call the tab's `apply()`, Cancel discards. `QSettings` (org "SlideIO" / app
  "SlideIO Viewer") is already configured. The `LogSettings` module is the established
  pattern for a unit-tested settings helper (pure logic + QSettings glue + spdlog).

## Components

### 1. `ReadingSettings` (new) — `src/ui/{include/slideio/viewer/ui,src}/ReadingSettings.{h,cpp}`

Mirrors `LogSettings`: a Qt-light helper with a pure, unit-tested core.

- `int clampThreadPoolSize(int n)` — pure; clamps to `[kMinThreadPoolSize, kMaxThreadPoolSize]`.
- `int readThreadPoolSize()` — reads QSettings key `reading/threadPoolSize`, returns
  `clampThreadPoolSize(stored)`, or `kDefaultThreadPoolSize` when unset.
- `void saveThreadPoolSize(int n)` — writes `clampThreadPoolSize(n)` to the key.
- Constants (in the header): `kDefaultThreadPoolSize = 4`, `kMinThreadPoolSize = 1`,
  `kMaxThreadPoolSize = 32`.

The header declares only these (no Qt types in the signatures), so the pure function is
testable by compiling `ReadingSettings.cpp` standalone against `Qt6::Core` (for QSettings
in the glue) — exactly as `LogSettings` is tested.

### 2. `PerformanceTab` (new) — `src/ui/{include/slideio/viewer/ui,src}/PerformanceTab.{h,cpp}`

`class PerformanceTab : public QWidget` (PIMPL, like `LogsTab`), `Q_OBJECT`, deleted copy,
public `void apply();`.

- Construction: an "Image reading" `QGroupBox` containing a `QFormLayout` row
  "Reading threads:" → `QSpinBox` with range `[kMinThreadPoolSize, kMaxThreadPoolSize]`,
  current value from `readThreadPoolSize()`. Below it, a wrapped note `QLabel`:
  "Applies to the next opened slide." Outer `QVBoxLayout` adds the group, the note, and a
  stretch.
- `apply()` → `saveThreadPoolSize(spinBox->value())`. (Persist only; nothing live to
  reconfigure.)

### 3. `SettingsDialog` wiring — `src/ui/src/SettingsDialog.cpp`

- Add `PerformanceTab* performanceTab` to `Impl`; construct it and
  `tabs->addTab(performanceTab, "Performance")` after the Logs tab.
- OK (`accepted`) and Apply both call `m_impl->logsTab->apply()` **and**
  `m_impl->performanceTab->apply()`. Cancel unchanged (discards).

### 4. Consume the setting — `src/ui/src/ViewportWidget.cpp`

- Add `#include "slideio/viewer/ui/ReadingSettings.h"`.
- Replace the hardcoded `4` at both pool-construction sites with
  `slideio::viewer::ui::readThreadPoolSize()` (fully qualified — these helpers are in the
  file's anonymous namespace, not the ui namespace):
  - `:1067` → `std::make_shared<infra::SlideIOAdapterPool>(filePath, sceneIndex, slideio::viewer::ui::readThreadPoolSize(), driverId)`
  - `:1120` → `std::make_shared<infra::SlideIOAdapterPool>(filePath, auxImageName, slideio::viewer::ui::readThreadPoolSize(), driverId)`
- At scheduler creation (`installSceneOpenResult`, ~2107), pass the pool size as
  `numWorkers` so the worker count tracks the pool:
  `std::make_shared<infra::TileLoadScheduler>(m_impl->adapterPool, m_impl->tileCache, m_impl->adapterPool->poolSize())`.
  (`SlideIOAdapterPool::poolSize()` already exists.)

### 5. Build / CMake

- Add `src/PerformanceTab.cpp` and `src/ReadingSettings.cpp` to the `slideio-viewer-ui`
  source list (`src/ui/CMakeLists.txt`). AUTOMOC picks up the new `Q_OBJECT` header via
  the existing `include/*.h` glob.
- Add `ReadingSettingsTest.cpp` + `${CMAKE_SOURCE_DIR}/src/ui/src/ReadingSettings.cpp` to
  the `slideio-viewer-ui-tests` target (`tests/CMakeLists.txt`), alongside the existing
  `LogSettings` test sources.

## Data Flow

```
PerformanceTab spin (1..32)
  └─ apply() → saveThreadPoolSize() → QSettings["reading/threadPoolSize"]
                                          │
   next slide open (worker thread)        ▼
   openSceneSync / openAuxImageSync → readThreadPoolSize() = N
        └─ SlideIOAdapterPool(size = N)
   installSceneOpenResult (GUI thread)
        └─ TileLoadScheduler(pool, cache, numWorkers = pool.poolSize() = N)
```

## Error Handling

- Out-of-range or corrupt stored values are clamped by `clampThreadPoolSize` on both read
  and write, so the pool size is always in `[1, 32]`. A `QSpinBox` already constrains UI
  input to the range.
- No new failure modes in the open path: the pool constructor already validates
  `poolSize > 0` (throws otherwise), and the clamped value is always ≥ 1.

## Testing

- **Unit test** (`tests/ui/ReadingSettingsTest.cpp`, Catch2, in `ui-tests`):
  `clampThreadPoolSize` — below min → 1, above max → 32, in-range unchanged, boundaries
  (1 and 32) preserved.
- **Manual verification** (Tools → Settings → Performance):
  - Set Reading threads = 1, OK, open a slide → the log's `SlideIOAdapterPool: creating
    pool of 1 adapters` and `TileLoadScheduler: starting 1 worker threads` confirm it.
  - Set = 8, open another slide → pool of 8 / 8 workers. The already-open slide is
    unaffected until reopened.
  - Reopen the dialog → the spin box shows the persisted value across restarts.

## Notes for the implementer

- **Namespace:** the pool sites are in the file-scope anonymous namespace, so the
  `readThreadPoolSize()` call must be fully qualified `slideio::viewer::ui::…`. The
  scheduler site is a `ViewportWidget` member (ui namespace) and uses
  `m_impl->adapterPool->poolSize()`, not `readThreadPoolSize()`, so both the pool and the
  scheduler derive from the same number without re-reading QSettings on the GUI thread.
- **Pattern fidelity:** follow `LogSettings`/`LogsTab` for module shape, PIMPL, CMake, and
  the standalone-compiled unit test (link `Qt6::Core` only).
- **`QSpinBox` ownership:** parented to the tab/group like the existing `LogsTab` widgets.
```
