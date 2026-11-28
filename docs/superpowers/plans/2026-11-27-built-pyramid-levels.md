# Built Pyramid Levels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give a slide whose coarsest stored level is large a set of viewer-built coarser levels, filled by one background pass, so zoomed-out frames are tens of tiles and the minimap gets an overview.

**Architecture:** Pure planning and box-averaging in core (`BuiltLevels`, `BuiltLevelAccumulator`); an infra decorator `BuiltPyramidSource : ISlideSource` that wraps `SlideIOAdapter`, owns the worker thread and serves built tiles; ui wires it in at open, keeps the open-time helpers on the coarsest *stored* level, stops requesting/skipping not-ready tiles, and feeds the minimap when the build completes.

**Tech Stack:** C++17, Qt 6 (ui/infra only), Catch2 v3, CMake. Core stays Qt-free.

**Spec:** `docs/superpowers/specs/2026-11-27-built-pyramid-levels-design.md`

## Global Constraints

- `kBuiltTopLevelMaxSide = 2048`; resident budget `256 * 1024 * 1024` bytes; built levels step by factor 2; sizes `ceil(C / 2^k)` per axis.
- Built levels only when `numZSlices == 1 && numTFrames == 1`; never for associated images.
- `SlideInfo::levels` is never changed — built levels appear only in `ISlideSource::levels()`.
- Every read the wrapper makes names its colour mode (mode-explicit `readBlock`); stored-level tiles forward to the adapter's `readTile`.
- A pending tile is never cached, never counted in `tilesSkipped`, never requested.
- Core links no Qt; no test constructs a widget or `QApplication`; Qt-linked test binaries run only under `ctest`.
- Build: `cmake --build build/build --config Release`; test: `ctest --test-dir build/build -C Release --output-on-failure`; only `core-tests` may be run directly.
- Commits land directly on `main`; do not push.

## Review Focus

1. A slide whose width or height is not a multiple of the factor or the tile size (the example is 63803×44997): the right and bottom built pixels average only real source pixels, and edge tiles are clipped like the adapter's. → Task 2 `"partial edge boxes average only real pixels"`, Task 4 `"edge tile of a resident level is clipped"`.
2. Closing or switching the slide while a band read is in flight: close returns promptly, no callback fires after destruction. → Task 4 `"destruction mid-build joins and stops callbacks"`.
3. A colour-mode toggle while tiles of the old mode are queued: no old-mode resident tile is ever served as the new mode, and none is cached as pending. → Task 4 `"a key in the other mode is pending"`, Task 3 `"scheduler does not cache a pending tile"`.
4. A band read that throws or returns an error tile (corrupt region): neighbouring rows still build, affected tiles render as error tiles, the build still completes. → Task 4 `"a failed band yields error tiles and the build completes"`.
5. 16-bit and float fluorescence data: averages keep the type, no overflow, minimap conversion uses the existing per-channel mix. → Task 2 `"every DataType with a size averages exactly"`.

---

### Task 1: Level plan and stored-level helpers (core)

**Files:**
- Modify: `src/core/include/slideio/viewer/core/Types.h` (`struct LevelInfo`, after `nativelyTiled`)
- Create: `src/core/include/slideio/viewer/core/BuiltLevels.h`, `src/core/src/BuiltLevels.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` (core-tests list)
- Test: `tests/core/BuiltLevelsTest.cpp`

**Interfaces:**
- Produces:
  - `LevelInfo` gains `bool built = false; bool resident = false; int builtFactor = 1;` (`builtFactor` = downsample relative to the source level *C*).
  - `constexpr int kBuiltTopLevelMaxSide = 2048;`
  - `constexpr size_t kBuiltLevelBudgetBytes = 256ull * 1024ull * 1024ull;`
  - `struct BuiltLevelPlan { int sourceLevel = -1; std::vector<LevelInfo> levels; };` — `levels` are only the appended ones, in order finest→coarsest, `level` indices continuing after the stored ones. Empty `levels` means nothing to build.
  - `BuiltLevelPlan planBuiltLevels(const std::vector<LevelInfo>& stored, const SlideInfo& info, size_t budgetBytes = kBuiltLevelBudgetBytes, int defaultTileSize = 256);`
  - `int coarsestStoredLevelIndex(const std::vector<LevelInfo>& levels);` — last index with `!built`, or -1.
  - `struct TileSlideRect { int levelX, levelY, tileW, tileH, slideX, slideY, slideW, slideH; bool valid; };`
  - `TileSlideRect tileSlideRect(const LevelInfo& level, int column, int row, int slideWidth, int slideHeight);` — the same arithmetic as `SlideIOAdapter::readTile` (`SlideIOAdapter.cpp:1203-1229`): clipped tile size, `round(level * 1/scale)`, clamped to the slide.
  - `std::vector<TileKey> requestableTiles(const std::vector<TileKey>& keys, const std::function<bool(const TileKey&)>& isReady);` — keeps order, drops not-ready keys.

- [ ] **Step 1: Write the failing tests** in `tests/core/BuiltLevelsTest.cpp`, tag `[core][BuiltLevels]`:
  - `"no built levels when the coarsest stored level is at most 2048 px"` — stored 2048×1500 → `plan.levels.empty()`; 2049×100 → not empty.
  - `"no built levels for a Z-stack or a T-series"` — `info.numZSlices = 3` → empty; `numTFrames = 2` → empty.
  - `"the example slide gets five levels, three resident"` — stored one level 63803×44997, tile 256, natively tiled, 3 ch `Byte`: `levels.size() == 5`; sizes `{31902×22499, 15951×11250, 7976×5625, 3988×2813, 1994×1407}`; `builtFactor` `{2,4,8,16,32}`; `resident` `{false,false,true,true,true}`; `level` indices `{1..5}`; `sourceLevel == 0`; tile size 256; `built` all true; `scale == width / 63803.0` within 1e-12.
  - `"budget boundary"` — choose a stored level so the /2 level's bytes plus coarser ones equal the budget exactly → /2 resident; budget −1 → /2 virtual.
  - `"nothing resident means nothing built"` — 40 channels `UInt16` at 4096×4096 → empty plan.
  - `"tile size follows a natively tiled source, else 256"` — stored tile 512 natively tiled → 512; `nativelyTiled = false` with tile 256 → 256.
  - `"coarsestStoredLevelIndex skips built levels"` — `{stored, stored, built, built}` → 1; all stored → last; empty → -1.
  - `"tileSlideRect matches the adapter's arithmetic"` — level 1994×1407 scale 1994/63803, tile (7,5) of 256: `tileW == 1994-1792 == 202`, `tileH == 1407-1280 == 127`, `slideX == round(1792*63803/1994.0)`, clamped `slideW`/`slideH` end at 63803/44997; a column past the grid → `valid == false`.
  - `"requestableTiles drops not-ready keys and keeps order"`.

- [ ] **Step 2: Run** `cmake --build build/build --config Release --target slideio-viewer-core-tests` — expected: compile error, `BuiltLevels.h` not found.

- [ ] **Step 3: Implement** `BuiltLevels.{h,cpp}` and the `LevelInfo` fields. Plan: *C* = `coarsestStoredLevelIndex(stored)`; stop if `max(C.w, C.h) <= 2048` or Z/T > 1; generate k = 1.. until `max(w,h) <= 2048`; mark resident from the coarsest upward while cumulative `w*h*channels*dataTypeSize(dt)` ≤ budget; if none resident, return empty. Tile size: `C.nativelyTiled ? C.tileWidth/Height : defaultTileSize`. Fill `tilesX/tilesY` as `SlideIOAdapter.cpp:760-765` does; `nativelyTiled = true` (built tiles are served tile by tile).

- [ ] **Step 4: Run** `build/build/tests/Release/slideio-viewer-core-tests.exe "[BuiltLevels]"` — expected: all pass. Then the full core suite passes.

- [ ] **Step 5: Commit** — `git add` the five files; message `Plan built pyramid levels for slides without a usable overview level`.

---

### Task 2: Band accumulator (core)

**Files:**
- Create: `src/core/include/slideio/viewer/core/BuiltLevelAccumulator.h`, `src/core/src/BuiltLevelAccumulator.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/BuiltLevelAccumulatorTest.cpp`

**Interfaces:**
- Consumes: `LevelInfo` (Task 1), `TileData`, `DataType`, `dataTypeSize`.
- Produces:
  - `enum class BuiltRegionState { Ready, Pending, Failed };`
  - `class BuiltLevelAccumulator` with:
    - `BuiltLevelAccumulator(int sourceWidth, int sourceHeight, int numChannels, DataType dataType, std::vector<int> residentFactors);` — factors ascending powers of 2 (e.g. `{8,16,32}`).
    - `int finestFactor() const;`
    - `void addChunk(int x, int y, int w, int h, const uint8_t* pixels);` — interleaved, row stride `w * numChannels * dataTypeSize`. Bands arrive in order: `y` is the current band's top; `h` is a multiple of `finestFactor()` except for the last band; chunks of one band arrive left to right and together cover `[0, sourceWidth)`.
    - `void failChunk(int x, int y, int w, int h);` — counts as coverage, marks the band failed.
    - `BuiltRegionState regionState(int residentIndex, int y0, int y1) const;` — rows `[y0,y1)` of resident level `residentIndex` (0 = finest).
    - `TileData extract(int residentIndex, int x, int y, int w, int h) const;` — copy of a region of that level.
    - `int levelWidth(int residentIndex) const; int levelHeight(int residentIndex) const;`
    - `bool complete() const;`

- [ ] **Step 1: Write the failing tests**, tag `[core][BuiltLevelAccumulator]`:
  - `"factor-2 box average of a 5x3 byte image"` — 1 channel, factors `{2}`, source rows `{0,2,4,6,8 / 10,12,14,16,18 / 20,21,22,23,24}` added as one chunk of height 3: level 3×2 equals `{(0+2+10+12)/4=6, (4+6+14+16)/4=10, (8+18)/2=13 / (20+21)/2=20.5→21, (22+23)/2=22.5→23, 24}`.
  - `"partial edge boxes average only real pixels"` — covered by the above's right column and bottom row; add a 3×3, factor-4 case → one pixel, the mean of nine.
  - `"rounding is half away from zero"` — `Int16` values `{-1,-2}` in one box → `-2`; `{1,2}` → `2`.
  - `"every DataType with a size averages exactly"` — for each of `Byte, Int8, UInt16, Int16, UInt32, Int32, Int64, UInt64, Float32, Float64` (and `Float16` through `float`): a 2×2 box of known values gives the exact mean; `UInt64` near max does not overflow; `UInt16` with 3 channels keeps channels separate.
  - `"chunks across the width equal one full-width band"` — 10×4, 3 ch, factor 2, delivered as chunks `[0,3) [3,8) [8,10)` versus one chunk → identical `extract` of the whole level.
  - `"a coarser level is the 2x2 average of the finer one"` — factors `{2,4}` on 8×8: `extract(1,…)` equals averaging `extract(0,…)` by 2.
  - `"regionState follows completed bands"` — 16×16, factors `{2,4}`, bands of height 4: after band 0, level 0 rows `[0,2)` Ready, `[0,3)` Pending; level 1 rows `[0,1)` Ready; after all bands `complete()`.
  - `"a failed band marks its rows failed and leaves neighbours ready"` — 3 bands, the middle one `failChunk`: level 0 rows of band 0 and 2 Ready, band 1 Failed; level 1 rows that mix band 1 Failed; `complete()` true.

- [ ] **Step 2: Run** the core test target build — expected: compile error.

- [ ] **Step 3: Implement.** Accumulate the finest level row by row from each completed band into a `std::vector<double>` sum plus per-pixel count only for the rows of that band, then write rounded values (integer types: `std::round`, which rounds half away from zero, then clamp to the type's range; floats: plain mean). Coarser levels: when two finer rows are complete (or the finer level's last row), average the 2×2 (or 2×1/1×2/1×1 at edges) finer pixels into one coarser row. Track per level the number of completed rows and a list of failed row ranges; `regionState` returns Failed if any failed range intersects, else Ready if `y1 <= completedRows`, else Pending.

- [ ] **Step 4: Run** `slideio-viewer-core-tests.exe "[BuiltLevelAccumulator]"` then the whole core suite — expected: pass.

- [ ] **Step 5: Commit** — `Accumulate source bands into built pyramid levels`.

---

### Task 3: Pending tiles, mode-explicit reads, readiness on the source interface

**Files:**
- Modify: `src/core/include/slideio/viewer/core/TileData.h`
- Modify: `src/core/include/slideio/viewer/core/ISlideSource.h`
- Modify: `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h`, `src/infra/src/SlideIOAdapter.cpp` (`readBlock`, `:1431`)
- Modify: `src/infra/src/TileLoadScheduler.cpp` (`:174-187`)
- Test: `tests/infra/TileLoadSchedulerTest.cpp`, `tests/infra/ColorManagedAdapterTest.cpp`

**Interfaces:**
- Produces:
  - `TileData::createPending()` and `bool TileData::isPending() const` (a pending tile is also `isEmpty()`, not `isError()`).
  - `ISlideSource`:
    - `virtual TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight, int targetWidth, int targetHeight, int zIndex, int tFrame, ColorMode mode);` — default implementation calls the existing overload (ignores `mode`), so fakes need not change.
    - `virtual bool isTileReady(const TileKey&) const { return true; }`
    - `virtual void addOnBuiltRowsReady(std::function<void(bool complete)>) {}` — called on a worker thread after each band and once with `true` at the end.
    - `virtual bool builtOverviewPending() const { return false; }` — true while a build is running or not yet started for a slide that has resident levels.
  - `SlideIOAdapter` overrides the mode-explicit `readBlock`, reading through `sceneForMode(mode)` instead of `activeScene()`; the existing overload calls it with `colorMode()`.

- [ ] **Step 1: Write the failing tests:**
  - In `TileLoadSchedulerTest.cpp`: `"scheduler does not cache a pending tile"` — a `RecordingSlideSource` variant whose `readTile` returns `TileData::createPending()`; after the request drains, `cache.lookup(key) == nullptr` and the loaded callback was not called.
  - In `ColorManagedAdapterTest.cpp`: `"a mode-explicit block read follows the mode it is given, not the adapter's"` — same image and skip as the test at `:178`; with the adapter in Raw, `readBlock(0,0,w,h,256,256,0,0,Managed)` equals the same call made with the adapter in Managed, and differs from the Raw-mode call (use `countDifferingBytes`).

- [ ] **Step 2: Run** `cmake --build build/build --config Release` — expected: compile errors for `createPending` and the new overload.

- [ ] **Step 3: Implement** the four interface additions, the adapter override, and in `TileLoadScheduler::workerLoop` skip insert and callback when `tileData.isPending()`.

- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release --output-on-failure` — expected: all four suites pass (the adapter test skips without the corpus; set `SLIDEIO_VIEWER_TEST_IMAGES` to run it).

- [ ] **Step 5: Commit** — `Pending tiles and mode-explicit block reads`.

---

### Task 4: BuiltPyramidSource (infra)

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/BuiltPyramidSource.h`, `src/infra/src/BuiltPyramidSource.cpp`
- Modify: `src/infra/CMakeLists.txt`, `tests/CMakeLists.txt` (infra-tests list)
- Test: `tests/infra/BuiltPyramidSourceTest.cpp`

**Interfaces:**
- Consumes: Task 1 `planBuiltLevels`, `tileSlideRect`, `coarsestStoredLevelIndex`; Task 2 accumulator; Task 3 interface.
- Produces:
  - `class BuiltPyramidSource : public core::ISlideSource`
    - `explicit BuiltPyramidSource(std::shared_ptr<core::ISlideSource> inner, size_t budgetBytes = core::kBuiltLevelBudgetBytes, int bandChunkMaxBytes = 64 * 1024 * 1024);` — plans; does **not** start the thread.
    - `void start();` — starts the worker in the current `colorMode()` if the plan has resident levels; idempotent. Separate from the constructor so `openSceneSync` can run the open-time pass first and register listeners before bands flow.
    - `bool hasBuiltLevels() const;`
    - `core::TileData smallestBuiltLevel() const;` — the coarsest resident level's whole raster once `complete`, else empty.
    - All `ISlideSource` methods. `slideInfo()` = inner's, unchanged. `levels()` = inner's + plan. `readTile`: stored → `inner->readTile`; virtual → `tileSlideRect` then `inner->readBlock(..., key.zIndex(), key.tFrame(), key.colorMode())` (error tile if `!valid`); resident → pending if `key.colorMode() != buildMode` or region Pending, error tile if Failed, else `extract`. `isTileReady`: false only for resident keys that would be pending. `setColorMode(m)`: forwards to inner; if `m` differs from the build's mode and the source has resident levels, stop + join the worker, discard the accumulator, start a new build in `m`. `addOnLevelMarkedUnreliable`, `colorMode`, `activeColorProfileInfo` forward.
    - Destructor: set the cancel flag, join.
  - Band reads: rows of height `finestFactor * C.tileHeight` (last band shorter), chunked across the width so a chunk is ≤ `bandChunkMaxBytes`; each chunk is `inner->readBlock(x, y, w, h, w, h, 0, 0, buildMode)` in *C*'s slide coordinates (for *C* = level 0 they equal level coordinates; otherwise scale by `1/C.scale`, output size = *C*-level size). An error or empty result → `failChunk`. The cancel flag is checked before every chunk. Listener calls happen outside any lock the readers take.
  - One `spdlog::info` at start (`BuiltPyramidSource: building N levels from level C (WxH) in mode M`) and at completion with elapsed ms; at cancel, `debug`.

- [ ] **Step 1: Write the failing tests** in `BuiltPyramidSourceTest.cpp`, tag `[infra][BuiltPyramidSource]`, over a `FakeSource : ISlideSource` with one level of 4100×3000 (tile 256, 1 ch `Byte`), pixel `(x*7 + y*3) & 0xFF`, recording every `readBlock` call (rect, size, mode, which overload) and able to fail a given band:
  - `"levels appends the plan and slideInfo keeps the stored levels"`.
  - `"reads the source level once, in bands, top to bottom, in the build's mode"` — after the build completes: the union of chunk rects is exactly 4100×3000 with no overlap, `y` non-decreasing, every call used the mode overload with the mode at `start()`, never the live-mode overload, even after `inner` was switched to the other mode before `start()`.
  - `"a resident tile is pending before its band and exact after"` — with a fake that blocks in `readBlock` on a latch: before release `readTile` of a resident key is pending and `isTileReady` false; after completion it equals the box average computed in the test from the fake's formula.
  - `"edge tile of a resident level is clipped"` — last column/row tile has the clipped width/height of `tileSlideRect`.
  - `"a key in the other mode is pending"`.
  - `"a virtual tile is one mode-explicit block read with its key's rect, size and mode"` — construct with `budgetBytes = 1'000'000` so the /2 level (2050×1500, 3.08 MB) is virtual and the /4 level (1025×750) resident; with the default budget both are resident.
  - `"a stored tile forwards to the inner readTile"`.
  - `"a failed band yields error tiles and the build completes"`.
  - `"a mode change rebuilds in the new mode"` — after completion in Raw, `setColorMode(Managed)`: a resident Managed key becomes ready only after a second full pass whose reads all carry Managed.
  - `"destruction mid-build joins and stops callbacks"` — destroy while the fake is blocked (release the latch from another thread after 50 ms); destructor returns; a listener counter does not change afterwards.
  - `"the ready listener fires once per band and once with complete"`.
  - `"no thread starts for a slide without resident levels"` — stored 2000×1000 → `hasBuiltLevels()` false, `start()` makes no `readBlock` call.

- [ ] **Step 2: Run** the build — expected: compile error, header missing.

- [ ] **Step 3: Implement** `BuiltPyramidSource`. Guard the accumulator with a `std::shared_mutex` (writer: worker after each chunk; readers: `readTile`/`isTileReady`/`smallestBuiltLevel`). Worker owns a `std::thread`; cancel is `std::atomic<bool>`.

- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release -R infra-tests --output-on-failure` — expected: pass.

- [ ] **Step 5: Commit** — `Build coarser pyramid levels in the background`.

---

### Task 5: Wire the source into the viewport

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` — `openSceneSync` (`:1313-1342`), `hasAffordableOverview` (`:589`), `buildCoarseLevelThumbnailFallback` (`:607`), `readCoarseLevelAndBuildThumbnail` (`:760-1156`), both paint loops (`:4854-4887`, `:5035-5050`), the slide-install path (`:2903`)
- Modify: `src/ui/src/ViewportController.cpp` (`requestVisibleTiles`, `:130`), `src/ui/include/slideio/viewer/ui/ViewportController.h`
- Test: covered by Tasks 1–4 pure tests; this task adds none beyond running all suites (the widget is unreachable from tests).

**Interfaces:**
- Consumes: `BuiltPyramidSource` (Task 4), `coarsestStoredLevelIndex`, `requestableTiles` (Task 1), `isTileReady` / `addOnBuiltRowsReady` (Task 3).
- Produces: `ViewportController::setTileReadyPredicate(std::function<bool(const core::TileKey&)>)`; `ViewportWidget` signal `void builtOverviewReady(const QImage& thumbnail);` (used by Task 6).

- [ ] **Step 1: Open path.** In `openSceneSync` (not `openAuxImageSync`) wrap the adapter: `auto built = std::make_shared<infra::BuiltPyramidSource>(adapter); r.slideSource = built;`. Build the `TilePyramid` from `r.slideSource->levels()` (now including built levels). In `hasAffordableOverview`, `buildCoarseLevelThumbnailFallback` and `readCoarseLevelAndBuildThumbnail`, replace "last level" with `core::coarsestStoredLevelIndex(levels)`. When the source `hasBuiltLevels()`, `readCoarseLevelAndBuildThumbnail` skips the whole-slide `readBlock` and sets `overviewAvailable = false`. `start()` the source on the UI thread in the install path (`:2903`), after registering the listener in Step 3.

- [ ] **Step 2: Requests and skips.** `requestVisibleTiles` passes `visibleTileKeys()` through `core::requestableTiles(keys, m_isTileReady)` when a predicate is set. In both paint loops, a key with `!slideSource->isTileReady(key)` is `continue`d without touching `tilesSkipped`; the prefetcher's request list is filtered the same way where it is built.

- [ ] **Step 3: Progress.** Register `addOnBuiltRowsReady` with a lambda that `QMetaObject::invokeMethod(this, [...]{ controller->requestVisibleTiles(); update(); if (complete) emitBuiltOverview(); }, Qt::QueuedConnection)`, capturing a `QPointer<ViewportWidget>` and the source's identity so a callback from a previous slide is dropped.

- [ ] **Step 4: Build and run all suites** — `cmake --build build/build --config Release` then `ctest ...` — expected: green.

- [ ] **Step 5: Smoke run.** Kill any running viewer (ask first if it is the owner's), `cmake --install build/build --config Release`, confirm the installed exe timestamp matches the build, open `d:\Projects\slideio\images\images\tiff\PIT_01_00248_01.tiff`; log shows `building 5 levels from level 0` and `paintGL` visible tile counts in the tens at fit zoom once filled.

- [ ] **Step 6: Commit** — `Serve built levels in the viewport`.

---

### Task 6: Minimap overview from the built levels

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` (extract the conversion at `:1158-1266` into a free function), `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/MinimapWidget.cpp`, `src/ui/include/slideio/viewer/ui/MinimapWidget.h`
- Modify: `src/ui/src/MainWindow.cpp` (`:1310-1323`)

**Interfaces:**
- Consumes: `BuiltPyramidSource::smallestBuiltLevel()`, `builtOverviewPending()`, Task 5 signal.
- Produces:
  - `QImage renderOverviewImage(const core::TileData& block, const core::SlideInfo& info);` (file-local in `ViewportWidget.cpp`) — the existing per-channel mix / RGB / grayscale conversion, used by both the open-time block and the built level, so they cannot drift.
  - `MinimapWidget::setOverviewBuilding(bool)` — while true and no thumbnail, the panel text reads exactly `Building overview…`.

- [ ] **Step 1: Extract** `renderOverviewImage` with no behaviour change; build; run all suites; open a pyramidal SVS and confirm its minimap is unchanged (screenshot compare by eye).

- [ ] **Step 2: Emit** `builtOverviewReady(renderOverviewImage(source->smallestBuiltLevel(), slideInfo))` on completion; `MainWindow` connects it to `minimapWidget->setThumbnail(img); minimapWidget->setOverviewAvailable(true); minimapWidget->setOverviewBuilding(false);`, and in the `slideOpened` handler calls `setOverviewBuilding(slideSource->builtOverviewPending())` (expose through a `ViewportWidget::builtOverviewPending()` getter).

- [ ] **Step 3: Build, run all suites, install, open the example slide** — minimap shows `Building overview…`, then the slide.

- [ ] **Step 4: Commit** — `Minimap overview from built levels`.

---

### Task 7: Manual checks, documentation

**Files:**
- Modify: `docs/superpowers/specs/2026-11-27-built-pyramid-levels-design.md` (§8 results)
- Modify: `CLAUDE.md` (a Key Design Decisions entry: built levels, where they live, the three not-ready rules, stored-level helpers)
- Modify: `docs/technical-debt.md` (GPU textures released only wholesale)

- [ ] **Step 1:** Install (viewer not running; confirm the installed binary is the new one) and run AN1–AN12 from the spec's §8; record observed results, pass or fail, in §8.
- [ ] **Step 2:** Add the CLAUDE.md entry and the technical-debt entry.
- [ ] **Step 3: Commit** — `Record group AN and document built pyramid levels`.
