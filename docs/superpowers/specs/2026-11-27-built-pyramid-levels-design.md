# Built pyramid levels for slides without a usable overview level

Status: design, 2026-11-27. Not implemented.

## 1. Why

A slide whose file has no reduced-resolution levels — a plain tiled TIFF read
by the GDAL driver is the case in hand, `PIT_01_00248_01.tiff`, one
63803×44997 level of 256×256 JPEG tiles — cannot be shown zoomed out at any
reasonable cost today. Every zoomed-out frame is made of full-resolution tiles.

What that costs, measured 2026-11-27 with the SlideIO GDAL fix described in
§2 in place (before it, the main view stayed empty, see §2):

- At the opening fit-to-window zoom (2.9%) the frame asks for **44 000
  tiles** (`paintGL[1]: 44000 visible tiles`, `enqueued 43744 tile requests`).
  After 40 s about half the view was drawn and the process held **~7 GB**.
- Textures are only released wholesale — by `clearAllTextures` on a slide
  close, a reopen or a Z/T change, and by `releaseTexturesOfOtherMode` on a
  colour-mode change; `deleteTexture` has no callers — so a zoomed-out pass
  of such a slide puts ~8.6 GB of 256×256 textures on a 4 GB GPU.
- The minimap says "Overview unavailable (slide has no downsampled level)",
  because `readCoarseLevelAndBuildThumbnail` refuses to read a level of more
  than `kMaxOverviewTiles` (4096) tiles, which is right: reading it inline
  would block the open for a minute or more.

A slide like this is the normal output of many converters and challenge
datasets (the grand-challenge PIT set is one), so this is a format the viewer
should open as comfortably as a pyramidal SVS.

## 2. Prerequisite already done (SlideIO)

Until 2026-11-27 SlideIO's GDAL driver decoded the whole page for every block
request and reported the page as its one tile. The viewer therefore built a
1×1 grid of one 63803×44997 "tile": one 48.7 s read, an 8.6 GB cache entry,
and a texture wider than `GL_MAX_TEXTURE_SIZE` (32768 on the test machine),
so the upload failed and the main view stayed empty while the minimap — a
separate 999×705 `readBlock` — worked.

`extern/slideio/src/slideio/drivers/gdal/gdalscene.{hpp,cpp}` now reads a
tiled TIFF page tile by tile through `TileComposer` and `TiffTools::readTile`
and reports the file's tile size; three tests in `test_gdal_driver.cpp` cover
it. Committed as `95ab634d` on SlideIO branch `feature/gdal-tiled-tiff-reads`
(pushed; pull request to follow). This design assumes it.

The GDAL scene still serialises reads (`supportsConcurrentReads()` is
false: one libtiff handle per file). One full pass over the 44 000 tiles of
`PIT_01_00248_01.tiff` costs roughly a minute on one thread
(256 tiles took 378 ms at open). Making the tiled GDAL path concurrent
through `ContextPool`, as SVS does, is a separate SlideIO change that would
divide that by the core count; it is a follow-up, not part of this design.

## 3. Goals, non-goals, anti-goals

Goals:

- A slide whose coarsest stored level is large gets **built levels**: extra,
  coarser levels the viewer produces itself, so a zoomed-out frame is tens of
  tiles, not tens of thousands.
- The minimap shows an overview for such a slide once one exists.
- The cost is paid **once per open, in the background**, reading the coarsest
  stored level exactly once. The slide is usable — panning, zooming in,
  annotating — from the moment it opens.
- Memory for built levels is bounded and independent of slide size beyond a
  fixed budget.

Non-goals:

- **Persisting built levels across sessions** (a pyramid cache on disk).
  Worth doing later; the in-memory build is its prerequisite and its test bed.
- Scenes with more than one Z slice or T frame. They keep today's behaviour.
- Scene-panel thumbnails (`generateSceneThumbnails` still skips such scenes).
- Bounding GPU texture memory in general (§9 records it as debt).
- Changing the display-range autodetect, which already samples 256 tiles in
  under half a second.

Anti-goals — what would make this a failure even if it works:

- **A built level showing up as a level of the file.** The Slide Properties
  panel lists `SlideInfo::levels`; a pathologist or a QA reviewer reading it
  must see what the file contains, not what the viewer made.
- **Blocking the open.** A one-minute modal "building overview" is worse
  than today's behaviour for anyone who only wanted to look at one region at
  full resolution.
- **Built pixels that differ from what the file says at the same place**
  beyond the averaging a downsample is — a band written into the wrong rows,
  a channel order swapped. A zoomed-out view a reader trusts to find regions
  must not invent or move tissue.
- **A repaint storm.** A built tile that is not ready yet must not be counted
  as a skipped tile; `paintGL` re-arms a 16 ms repaint whenever any tile was
  skipped (`ViewportWidget.cpp:5066`), which over a one-minute build is a
  minute of 60 fps repaints.

## 4. Approaches

**A. Virtual levels, read through SlideIO.** Append coarser `LevelInfo`
entries and serve each tile of them as a slide rectangle and an output size,
resampled by SlideIO — the arithmetic `SlideIOAdapter::readTile` applies to
its own levels. No new state, a small change. But every built tile at downsample *d* costs *d²* full-resolution
tile decodes, every built level repeats the work of the levels below it, and
SlideIO's per-tile `INTER_LINEAR` resize aliases badly at large factors. The
fit-to-window frame of the example slide would still decode all 44 000 tiles
— on every open, and again for each level between.

**B. A background build into memory, plus virtual levels between.** One
background pass reads the coarsest stored level once, band by band, and
box-averages it into the coarsest built levels held in memory. Built levels
too large for the memory budget stay virtual (approach A) — they are only
ever seen when zoomed in far enough that few tiles are visible. The minimap
reads the smallest built level. Progressive: the built levels fill top to
bottom as bands complete.

**C. B, persisted to disk** as a pyramid file in a cache folder, so the
second open is instant. Strictly more than B; it needs B's builder and adds
a cache format, invalidation and a disk budget.

**Chosen: B.** A pays the full-slide cost again on every zoom-out and
produces aliased overviews; C is B plus a feature nobody has asked for yet,
and B is its first half. The project owner skips the design question round
(see the owner's standing preference); B is this document's call.

## 5. The design

### 5.1 Which scenes get built levels (core, pure)

`core::planBuiltLevels(const std::vector<LevelInfo>& stored, const SlideInfo&,
size_t memoryBudgetBytes)` returns the built levels to append, or none.

- Applies only when `numZSlices == 1 && numTFrames == 1`.
- Let *C* be the coarsest stored level. If its longer side is at most
  `kBuiltTopLevelMaxSide` (2048 px), nothing is built: the existing path
  already handles it, and a 2048-px level is at most 64 tiles of 256.
- Otherwise built levels are appended at successive **factors of 2** below
  *C* until the longer side is at most `kBuiltTopLevelMaxSide`. Factor 2, not
  4, because the viewer minifies with `GL_LINEAR` and no mipmaps, so a 4×
  step between levels would show 4:1 minification aliasing across half of
  every zoom range. Sizes are `ceil(C / 2^k)` per axis.
- Each built level is marked **resident** (held in memory, filled by the
  builder) or **virtual** (read through SlideIO on demand, approach A).
  Levels are made resident from the coarsest upward while their cumulative
  bytes (`w × h × channels × dataTypeSize`) stay within the budget. Budget:
  256 MB, the same figure as `kMaxWholeLevelReadBytes`, and for the same
  reason. For the example slide: /2 and /4 virtual; /8 (134 MB), /16, /32
  resident (≈180 MB together).
- If not even the coarsest built level fits the budget, nothing is resident
  and nothing is built — every built level would be virtual, which is
  approach A and its cost. A 2048×2048 level is 256 MB only past 64 bytes a
  pixel, so this is a many-channel fluorescence scene (e.g. 40 channels of
  `UInt16` is 320 MB); such a scene keeps today's behaviour.

Built levels use the stored grid's tile size when it is natively tiled,
otherwise `kDefaultTileSize`. `LevelInfo` gains `bool built = false` and
`bool resident = false`; `scale` and the tile counts are computed as for any
level, so `TilePyramid` and `CoordinateSystem` need no change — they see
more levels and `bestLevelForScale` picks among them.

**The open-time code keeps reading the file's own levels.** Today several
places take "the coarsest level" as `levels().back()` or the pyramid's last
level: `readCoarseLevelAndBuildThumbnail` (`ViewportWidget.cpp:767`, sampled
and cached for the display-range autodetect), `hasAffordableOverview`
(:592), `buildCoarseLevelThumbnailFallback` (:619) and, through the second,
`generateSceneThumbnails` (:2501). With built levels appended, that level is
a built one whose tiles are all not ready, so each of them must instead take
the coarsest **stored** level — a pure `core::coarsestStoredLevelIndex(levels)`,
the last level with `built == false`. The display range is therefore sampled
from the file exactly as today, and is not refined when the build finishes
(changing a range under the user mid-session is a separate decision).
`openSceneSync` still builds the `TilePyramid` from all levels. Associated
images (`openAuxImageSync`) never get built levels.

### 5.2 Accumulating bands (core, pure)

`core::BuiltLevelAccumulator` owns the resident levels' rasters. Its input is
a **band**: full-resolution pixels of level *C* for a rectangle `[x, x+w) ×
[y, y+h)`, interleaved channels, the scene's `DataType`. Its job is to
box-average each band into the finest resident level and halve that into
each coarser one.

- The finest resident level is at factor *F* (8 in the example). Bands are
  delivered in rows of height that is a multiple of *F*, except the last, so
  each output row of the finest level is complete when its band is.
- Averaging is a plain box over the *F×F* source pixels that exist —
  partial boxes at the right and bottom edges average only real pixels, they
  do not pad with zero. Integer types accumulate in a wider integer (`double`
  for `Int64`/`UInt64`, whose sums can overflow 64 bits, and for floats) and
  round half away from zero; every `core::DataType` with a size —
  `Byte`, `Int8`, `UInt16`, `Int16`, `UInt32`, `Int32`, `Int64`, `UInt64`,
  `Float16`, `Float32`, `Float64` — is covered. `Float16` is averaged through
  `float`.
- Coarser resident levels are 2×2 box averages of the next finer resident
  level, computed as soon as the two source rows they need are complete.
- It reports, after each band, the **completed row extent of every resident
  level** (`completedRows(level)`), which is what decides whether a tile is
  ready (§5.4).
- A band that could not be read is reported as failed; the rows it covers
  are marked failed in every resident level, and tiles touching them are
  served as error tiles (the existing "permanent read failure" rendering).

Everything here is plain C++ over byte buffers: no Qt, no OpenCV, so
core-tests reach every line.

### 5.3 The builder (infra)

`infra::BuiltPyramidSource` implements `core::ISlideSource` by wrapping
another `ISlideSource` — the `SlideIOAdapter` the viewport already opens.

- `levels()` returns the stored levels followed by the planned built ones.
  `slideInfo()` returns the wrapped `SlideInfo` **unchanged** — its `levels`
  stay the file's levels, which is what keeps them out of Slide Properties
  (anti-goal 1), the panel's only source (`SlidePropertiesPanel.cpp:217`).
  The other readers of `levels()` are the pyramid and the open-time helpers
  listed in §5.1, which take the coarsest stored level.
- **Reads name their colour mode.** `ISlideSource::readBlock` reads through
  the adapter's *live* mode (`SlideIOAdapter.cpp:1438`, `activeScene()`),
  while every tile read selects its scene from the key's mode
  (`SlideIOAdapter.cpp:1176-1180`) so that a read in flight across a toggle
  is filed under the rendition it actually is. Built pixels need the same
  guarantee, so `ISlideSource` gains a `readBlock` overload taking a
  `core::ColorMode`, which `SlideIOAdapter` serves through `sceneForMode`.
  The existing overload stays for the open-time callers that want the live
  mode. Every read the wrapper makes uses the new overload.
- **Virtual built levels are served by the wrapper, not the adapter.**
  `SlideIOAdapter::readTile` refuses any level index it did not create
  (`SlideIOAdapter.cpp:1190`), and its unreliable-level fallback searches its
  own levels by scale, which built levels must never join. So the wrapper
  computes a virtual tile's slide rectangle and output size from its
  `LevelInfo` (the same arithmetic `readTile` uses for any level) and makes
  one mode-explicit `readBlock`, with the key's Z, T and mode. Stored levels
  forward to the adapter's `readTile`, unchanged, keeping its fallback.
- On construction, if any level is resident, it starts **one worker
  thread** that reads level *C* in bands through the mode-explicit
  `readBlock` — full width (split into chunks when a full-width band would
  exceed 64 MB), band height = *F* × the level's tile height, at *C*'s own
  resolution — and feeds them to the accumulator.
- Each `readBlock` is one SlideIO call, so on a serialised scene the
  interactive tile workers wait at most one band chunk behind it (tens of
  milliseconds), not behind the whole build.
- **Cancellation:** an `std::atomic<bool>` checked between chunks; the
  destructor sets it and joins. `ViewportWidget` destroys the source on
  slide close, scene switch and colour-profile reopen, which already route
  through `closeSlide()`.
- **Colour mode:** a build belongs to one mode, like every tile key. It
  starts in the mode active at open. A colour-mode change cancels the build,
  frees its rasters and starts a build in the new mode; tiles of the old
  mode already in the tile cache stay there under their own keys, exactly as
  a toggle leaves every other tile today (`ViewportWidget.cpp:4482`: "No
  cache clear and no scheduler cancellation"). Holding one pyramid per mode
  instead would double the resident budget to 512 MB; transforming built
  pixels in the viewer would duplicate SlideIO's ICC path. Cost of the choice:
  each toggle on a large pyramid-less slide repeats the minute-long pass, and
  zoomed-out frames in the new mode fill in as it goes.
- Progress is reported through a listener, registered like
  `addOnLevelMarkedUnreliable`: `addOnBuiltRowsReady(std::function<void(int
  level, int rowsCompleted)>)`, called on the worker thread after each band,
  and once more with a "complete" flag at the end.
- `readTile(key)` for a resident level, in the build's mode, slices the tile
  out of the accumulator if every row it covers is complete, returns an error
  tile if any row it covers failed, and otherwise returns a **pending**
  result (§5.4). A resident-level key in the *other* mode (a read issued
  before a toggle) is pending too, and is never served from the wrong
  rendition. Both `readBlock` overloads forward unchanged.
- A ready resident tile goes through the scheduler into the ordinary LRU
  tile cache like any tile — a copy of 256×256 pixels, cheap to slice again
  if evicted. The cache's own budget therefore covers those copies; the
  resident rasters are the only memory added (≤ 256 MB).

### 5.4 Not-ready tiles

Today a `TileData` is a tile, an empty tile or an error tile, and
`TileLoadScheduler` caches whatever `readTile` returns, "even error tiles, so
we don't retry endlessly" (`TileLoadScheduler.cpp:177`). A not-ready resident
tile must be neither cached nor counted as skipped.

- `TileData` gains a third state, **pending** (`TileData::createPending()`,
  `isPending()`). The scheduler does not insert a pending result into the
  cache and does not call `onTileLoaded`.
- The source answers `isTileReady(key)` (true for every stored and virtual
  level). `ViewportController::requestVisibleTiles` does not request a key
  that is not ready, and `paintGL` does not count one as `tilesSkipped`, in
  **both** tile loops — the brightfield one and the fluorescence one with
  its own skip handling (`ViewportWidget.cpp:4854-4887` and `:5035-5050`).
- A not-ready area shows the background. There is **no** coarser fallback
  for it: the coarse-fallback pass (`ViewportWidget.cpp:4816-4851`) draws
  only tiles already uploaded or cached, nothing requests coarser levels,
  and every resident level fills top to bottom at the same pace, so a
  coarser built level is never ready where a finer one is not. The fill is
  the progress indicator.
- The `addOnBuiltRowsReady` callback is marshalled to the UI thread with a
  queued invocation and calls `controller->requestVisibleTiles()` and
  `update()`, so newly ready tiles are requested and a frame is drawn per
  completed band (a few per second), not 60 per second. Without the request,
  an `update()` alone would never draw a finished tile.
- The scheduler still refuses to cache a pending result, as a safety net for
  any other requester (the prefetcher, a read issued before a toggle).

### 5.5 The overview and the minimap (ui)

- `readCoarseLevelAndBuildThumbnail` is unchanged for slides without built
  levels. For a slide with resident built levels it skips the whole-slide
  `readBlock` (as it already does when the overview is unaffordable) and
  reports `overviewAvailable = false` at open.
- When the builder reports completion, `ViewportWidget` renders the
  smallest built level to the minimap's thumbnail through the same pixel
  conversion the open-time `readBlock` result goes through, so display range
  and colour mode apply identically. That conversion is inline code inside
  `readCoarseLevelAndBuildThumbnail` today (around `ViewportWidget.cpp:1240-1300`)
  and has to be extracted into a function both callers share. It then emits a
  signal that `MainWindow` connects to `minimapWidget->setOverviewAvailable(true)`
  and the new image. Until then the minimap keeps its "Overview unavailable"
  state, with its text changed to say it is being built for a slide that has
  a build in progress: "Building overview…".
- Showing a partially built minimap is deliberately not done: a minimap is
  for finding where to go, and half of one hides the other half.

### 5.6 What a user sees, for the example slide

```
open                 main view: coarsest built level empty; first bands fill
                     top-to-bottom within ~1 s, whole slide in ~1 min
                     minimap: "Building overview…"
zoom in to 100%      level 0 tiles, read on demand exactly as today
zoom out to ~25%     /4 virtual level: each tile = 16 full-res decodes,
                     ~70 visible tiles — a second or two, cached after
build completes      minimap shows the slide; zoomed-out frames are ~60 tiles
```

## 6. Constraints

- Core stays Qt-free; the plan and the accumulator are core, the thread and
  the listener are infra, signals and minimap are ui. Strict downward
  dependency as everywhere else.
- No test constructs a widget or a `QApplication`. Everything decidable is
  in the pure functions of §5.1–5.2; the decorator in §5.3 is tested in
  infra-tests over a fake `ISlideSource`.
- The SlideIO tiled-GDAL change of §2 must land first and the submodule pin
  must move with it, or the builder reads one 8.6 GB band.
- `cmake --install` is silently skipped while a viewer runs; manual checks
  must confirm the installed binary is the new one (CLAUDE.md, Build and
  Test).

## 7. Tests

core-tests:

- `planBuiltLevels`: nothing for a 2048-px or smaller coarsest level; nothing
  for Z>1 or T>1; the example slide's five levels with /8, /16, /32
  resident; sizes are ceilings; budget boundary exactly at and one byte over;
  tile size follows the stored grid.
- `BuiltLevelAccumulator`: exact box averages on a hand-computed 5×3 input
  with factor 2 (partial edge boxes average real pixels only); round-half-away-from-zero
  on integers; every `DataType` with a size; bands delivered as several chunks
  across the width produce the same raster as one band; a coarser level
  equals averaging the finer one; `completedRows` after each band; a failed
  band marks its rows failed and leaves neighbours good.
- `coarsestStoredLevelIndex`: the last stored level with built levels
  appended, and with none.

infra-tests (`BuiltPyramidSource` over a fake source that serves a synthetic
gradient, records the colour mode of every read and counts calls):

- `levels()` appends the plan; `slideInfo().levels` is the wrapped list.
- Reads level *C* exactly once in total, in bands, top to bottom, every band
  through the mode-explicit `readBlock` in the build's mode — never the
  live-mode overload, even when the fake's live mode differs.
- A resident tile before its rows complete is pending and not ready; after,
  ready and equal to the box average of the fake's pixels; across a failed
  band, an error tile.
- A resident key in the other mode is pending and never served.
- A virtual level's tile is one mode-explicit `readBlock` with the slide
  rectangle, output size, Z, T and mode of its key; a stored level's
  `readTile` forwards to the wrapped `readTile`.
- A mode change cancels the build, frees it and rebuilds in the new mode.
- Destruction mid-build returns promptly and leaves no thread behind.
- The ready listener fires once per band and once at completion.
- `TileLoadScheduler` does not cache a pending result and does not call
  `onTileLoaded` for it.
- `SlideIOAdapter`'s mode-explicit `readBlock` reads the scene of the mode
  it is given (corpus-free: a GDAL slide written by the test, as SlideIO's
  own tiled-TIFF tests do).

ui-tests: `ViewportController::requestVisibleTiles` is unreachable without a
widget only if it needs one; if it does, the not-ready filter is extracted
into a pure function over keys and a readiness predicate and tested there.

## 8. Manual checks (group AN)

To be run against the installed build:

- AN1 Open `PIT_01_00248_01.tiff`: the window is responsive at once; the
  main view starts filling within 2 s; no step of the open blocks input.
- AN2 The whole slide is drawn zoomed out within ~90 s on the test machine;
  log shows the band pass reading level 0 once.
- AN3 Minimap reads "Building overview…", then shows the slide when the
  build completes, and navigating with it works.
- AN4 Zoom to 100% mid-build: full-resolution tiles appear as fast as with
  no build running (within the band-chunk wait).
- AN5 Process memory during and after the build stays below the tile-cache
  budget plus 256 MB plus a margin; not ~7 GB.
- AN6 Slide Properties lists one level for this file.
- AN7 Close the slide mid-build: it closes at once, the log shows the
  builder stopped, and opening another slide is unaffected.
- AN8 Toggle colour management mid-build and after: the build restarts in
  the new mode (log), zoomed-out tiles fill in again in that mode's colours,
  and no tile of one mode is shown in the other.
- AN9 `PIT_01_02548_01.tiff` (11963×6985): gets three built levels, all
  resident, and its overview within a few seconds; the display range is the
  same as before this change.
- AN12 Open the large slide in a fluorescence-style view if one is
  available (or any single-level fluorescence TIFF): not-ready areas stay
  background without a repaint storm (log shows no 16 ms repaint loop).
- AN10 A pyramidal SVS: no built levels, behaviour identical to today
  (log shows no build).
- AN11 Annotate and measure on the large slide while it builds: nothing
  about annotations is affected.

## 9. Debt this does not fix

- **GPU textures are never evicted one by one** (`ViewportWidget::Impl::textures`
  grows until a slide close, a reopen, a Z/T change or a colour-mode change
  releases them wholesale; `deleteTexture` has no callers). Built levels make the zoomed-out case tens of textures, but
  a long full-resolution pan across any large slide still accumulates them.
  To be recorded in `docs/technical-debt.md`.
- The GDAL scene serialises reads (§2).
- No persistence: every open of the same slide rebuilds (approach C).

## 10. Left to the builder

- Names of the new types and files, within the conventions.
- Band chunk width and whether the band height is *F* or a multiple of *F*
  tiles — whatever keeps one `readBlock` near 10–50 ms on the example slide.
- How `isTileReady` reaches the controller and `paintGL` (they hold the
  source today only through `ViewportWidget::Impl`), as long as §5.4's rules
  hold: not requested, not cached, not skipped, request-and-repaint per band.
- Whether the "Building overview…" text needs a new `MinimapWidget` state or
  reuses the unavailable state with a different string.
