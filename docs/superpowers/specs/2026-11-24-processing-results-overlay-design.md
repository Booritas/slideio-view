# Processing Results Overlay — Design

Show the objects a slideio-processor run produced — tissue regions, nuclei,
cells, whatever a pipeline writes — on the slide in the viewer, straight from
the processor's workspace, at any number of objects.

Calls marked **(author's call)** were made while writing this and not
discussed with the project owner; everything else was decided with them.

## 1. Why

slideio-processor writes its results to a workspace: a directory whose
`workspace.store` is a SQLite database holding every object's outline, layer,
class label and measurements, with an R-tree over the objects' bounding boxes.
Today the only way to see those results in the viewer is the GeoJSON export,
and it does not work:

- The export marks every feature `"objectType": "detection"`, and the GeoJSON
  import skips detections by design (import design §2, §3).
- Tissue regions have holes (the vessel lumen, the gaps between fat cells), and
  the import skips polygons with holes: the viewer has no shape for them.
- A cell result has 10 000–100 000 objects. As annotations each would get a
  list row, a hit test per click, a measurement on selection and a place in the
  autosaved file — all built for tens or hundreds of hand-drawn shapes.

The owner expects the viewer to show processing results, cells included.
Results are not annotations: nobody edits them, they are not the
pathologist's, and they already live in a store built to answer "what is in
this rectangle" quickly. So the viewer reads the store directly and draws a
read-only **results overlay**, separate from annotations.

## 2. Goals, non-goals, anti-goals

**Goals**

- Open a processor workspace for the open slide and scene, read-only.
- Draw its objects over the slide — outlines, optional fill, holes as holes —
  fetching only what is in view, so a 100 000-cell result pans and zooms
  smoothly.
- **A whole-slide overview at any object count:** where a layer has too many
  objects in view to outline, an overview map shows where its objects are and
  which classes dominate where (§5.3a) — a 1 000 000-cell slide is visible
  fitted to the window, and outlines take over as the user zooms in.
- One switch and one colour per processor layer (tissue, nuclei, …).
- **Colour by class:** an object's class label picks its colour; a legend lists
  the classes with a switch to hide each.
- Click an object to see its layer, class and measurements, read-only.
- Remember, per slide and scene, which workspace was attached, and reattach it
  when the slide reopens.

**Non-goals (this round)**

- **Copying results into annotations.** Deferred by the owner. It needs
  annotations with holes (the separate "polygons with holes" design) or a
  stated outer-boundary-only rule, and a cap.
- **GeoJSON detection files (QuPath cell exports) as an overlay source.** The
  owner wants them eventually; the overlay's source interface (§6) is shaped so
  a second source can be added, but none is built now.
- **Live updates while a run writes.** A ReadOnly store sees the newest
  committed data on every query, so panning after a stage finishes shows the
  new objects; there is no push notification. The store's change signal is
  library-internal; a public one is a later library change.
- Filtering by measurement, counting inside a drawn region, heatmaps of a
  measurement (the overview map shows counts and classes only), raster
  results (the processor's rasters), editing anything in the workspace.
- **A processor-written overview** (a pyramid image of classes written during
  the run). Considered and set aside for now: the viewer builds the map itself
  (§5.3a); this is the fallback if attach-time scans prove too slow.
- Workspaces made for another slide: refused, not re-mapped (§5.1).

**Anti-goals** — it fails even if it works when:

- **The viewer ever writes to a workspace**, takes its writer lock, or blocks a
  processor run that is writing it.
- **The viewer loads a second SlideIO.** It links only the processor's data
  library, which has no SlideIO (§4).
- **Panning or zooming stalls** because a query runs on the UI thread, or
  because a zoomed-out view tries to draw 100 000 outlines.
- **An object is drawn somewhere it is not**: a coordinate or scene mix-up that
  puts results from scene 1 on scene 0, or from another slide on this one.
- **A result is mistaken for an annotation**: it must never appear in the
  annotation list, the counts, the annotation exports or the autosaved file.

**Constraint.** A workspace is derived from a patient slide; its store holds
the slide's absolute path. Displaying it adds no new data flow, but the
workspace path the viewer remembers (§5.6) is stored like the viewer's other
per-slide settings and falls under the same HIPAA Security Rule / GDPR
controls as the slides themselves.

## 3. Approaches considered

1. **Import detections as ordinary annotations and make annotations scale**
   (virtual list, spatial index, 100 000-object autosave). Every cell becomes
   editable, which results must not be, and it is the largest change. Dropped.
2. **A read-only overlay reading the workspace through the processor library —
   chosen.** The store already answers "objects in this rectangle" through its
   R-tree; holes are free with the even-odd fill the viewer already uses.
   - Linking the processor's **engine** would force the viewer and the
     processor to share one SlideIO build (one SlideIO per process). The owner
     rejected that.
   - The processor already ships a separate shared library,
     **`slideio-processor-data`** (core types, configuration, storage,
     reporting), and its build fails if any of its parts links SlideIO
     (`slideio-processor/src/CMakeLists.txt:32-41`). **The viewer links only
     that.** Chosen by the owner over the two alternatives below.
3. Read the SQLite file directly with the viewer's own code (no processor
   dependency, but the store's internal tables, at format version 2 with
   migrations, become a contract that breaks silently), or a helper process
   answering queries (a round trip per repaint, a process to manage). Dropped.

## 4. Prerequisite: a data-only build of slideio-processor

The processor's top-level `CMakeLists.txt` requires SlideIO unconditionally
(`find_package(slideio 2.11 REQUIRED)`). The viewer needs:

- A CMake option, `SLIDEIO_PROCESSOR_DATA_ONLY` (default OFF) **(author's
  call: name)**, that builds only `slideio-processor-data` and its object
  libraries (core, config, storage, reporting), does not look for SlideIO or
  OpenCV, and skips the engine, the tools and the engine's tests (the data
  library's own tests still build and run).
- Usable through `add_subdirectory`, as the Python bindings already use the
  whole library: the consumer sets the option, turns test seams off and links
  the `slideio-processor-data` target. No install rules or package
  configuration are needed **(corrected while planning: the earlier
  "installed CMake package" would have added install rules the library has
  never had, for no gain over the pattern the bindings already prove)**.
- The data library reads OpenCV for its version only
  (`BackendCapabilities`). In data-only mode the version is the conanfile's
  `opencv/<x.y.z>` pin, read at configure time, so the library reports what an
  engine built from the same commit would.
- Its dependencies in data-only mode: sqlite3 (with `enable_rtree`),
  rapidjson, libtiff, zlib, and Threads.
- A CI job building and testing the data-only configuration, so it cannot rot.

This lands in slideio-processor first, as its own small change, before any
viewer work that links it.

**In the viewer**: a `slideio-processor` submodule under
`extern/slideio-processor`, added with `add_subdirectory` in data-only mode
with test seams off; its dependencies (sqlite3 with R-tree, rapidjson,
libtiff, zlib) join the viewer's `conanfile.py` **(author's call)**. Its own
nested SlideIO submodule is never needed and is not initialised. The infra
layer links it
PRIVATE, so no processor type appears in a core or app public header; the
packaging scripts bundle the shared library. On Linux the data library already
hides its static dependencies' symbols (`--exclude-libs,ALL`); on Windows and
macOS the viewer's and the library's copies of shared dependencies (zlib,
libtiff) are separate DLLs/dylibs and must be checked for clashes at packaging
time (§11).

### 4.1 Branching

Decided by the owner: the whole viewer side of this work — this document, the
`extern/slideio-processor` submodule, the build and packaging changes, and all
code — lives on the branch **`feat/processing-results`**, never on `main`,
until the feature is finished. A viewer release may have to ship from `main`
while this is in progress, so `main` must never gain the processor dependency
or a half-built Results tab.

- `main` is merged into the branch regularly (not rebased: the branch may be
  pushed and shared), so the final merge back is small.
- Fixes needed on both, found while working on the branch, are made on `main`
  first and merged in.
- The slideio-processor data-only build (§4) is a normal change in that
  repository, on its own branch and pull request; it does not wait for the
  viewer.
- The branch merges into `main` only when the feature is complete and
  reviewed.

## 5. Behaviour

### 5.1 Opening results

- **File → Open Processing Results…** opens a folder chooser. The chosen folder
  must contain `workspace.store`; otherwise a warning names the folder and
  nothing changes.
- The store is opened with `Store::open(path, OpenMode::ReadOnly, …)`: no lock,
  safe beside a writing run.
- **Slide check.** The store's provenance holds the slide's absolute path
  (`Provenance::slideId()`) and the scene index (`sceneIndex()`). The viewer
  compares them with the open slide and scene:
  - same file (compared as canonical paths) and same scene: attach;
  - same scene, different path: a warning with both paths and *Attach
    anyway* / *Cancel* — the slide may have moved since the run **(author's
    call)**;
  - different scene: refused, with a message naming the workspace's scene.
- One workspace attached per scene at a time; opening another replaces it.
  **File → Close Processing Results** detaches. Closing the slide detaches.
- A store that fails to open (corrupt, a newer format the linked library cannot
  read, unreadable) is a warning with the library's message; nothing attaches.

### 5.2 What is drawn

- Every **non-dropped layer with at least one object**, except the pixel
  layer, is a results layer. Semantic and primitive layers alike.
- Objects are drawn in level-0 slide coordinates — the store's — through the
  same slide-to-screen transform annotations use, with `Qt::OddEvenFill`, each
  ring (outer and holes) a sub-path. Outline 1.5 screen px, fill off by default
  with an opacity per layer when on **(author's call)**.
- Results draw **under** annotations and **over** the slide tiles, and stay out
  of the zoom-snapshot texture as annotations do.
- **Colour:** an object with a class label is drawn in its class colour (§5.3);
  one without, in its layer's colour. Layer colours come from a fixed palette in
  layer order and can be changed in the Results tab.
- **Too many to outline.** When a layer has more than a draw budget of objects
  in view (default 20 000 **(author's call)**), its outlines are not drawn;
  its overview map (§5.3a) is drawn instead, and the Results tab says, beside
  the layer, "12 345 in view — overview; zoom in for outlines". Until the map
  is built, it says "building overview… 43 %" and nothing is drawn for that
  layer. The
  budget counts **every** object of the layer in view, hidden classes included:
  one query per layer (`layer().intersecting(rect).limit(budget + 1)`), with
  class hiding applied after it. `ObjectQuery` has no multi-label or exclusion
  filter, and hiding a class is not a way to beat the budget **(author's
  call)**. Other
  layers draw normally, so a tissue layer stays visible over a slide whose
  nuclei do not. A detail line is not "the coarsest adequate level": the budget
  is a count, never a zoom threshold, so a sparse result draws at any zoom.
- Outlines are thinned for drawing only: consecutive points closer than half a
  screen pixel are skipped. Hit tests use the full geometry.

### 5.3 Colour by class

- An object's classes are its **effective label set**: the producing stage's
  label plus every label other stages asserted on it (`AnnotationStore::
  labelCount`/`labelAt`, named through `VocabularyRegistry`) — the same set
  the processor's CSV and GeoJSON exports write by default
  (`LabelPolicy::EffectiveSet`). A classification stage typically *asserts*
  labels on objects it did not produce, so the producer's label alone would
  show its classes as Unclassified **(author's call)**.
- An object is coloured by the **first label of that set**, in
  `AnnotationStore` order — what the processor's GeoJSON export writes as the
  "classification" **(author's call)**.
- An object is hidden only when **every** class it carries is hidden
  **(author's call)**.
- Colours come from the viewer's colourblind-safe palette (FR-ANN-15: blue,
  orange, yellow, purple, cyan, …), handed out in legend order and cycling.
  The user can change any class's colour in the legend; the choice is
  remembered **per class name** (compared with `core::caseFoldKey`), for every
  slide, so "tumour" keeps its colour everywhere once chosen **(corrected while
  planning: the viewer's categories carry no colour — each annotation has its
  own — so the earlier "take the matching category's colour" had nothing to
  match)**.
- The **legend** (in the Results tab, under each layer) lists the layer's
  classes with their colours, their object counts in the whole layer
  (`ObjectStore::count` with a label filter, which matches the effective set)
  and a switch to hide each, plus an "Unclassified" row for objects with no
  label. An object with two classes counts under both, so the rows can add up
  to more than the layer total; the legend says so in a tooltip.
- **Colour by class** is a per-layer switch, on by default; off, the whole
  layer is drawn in its layer colour.

### 5.3a The overview map

Agreed with the owner: at the zoom where a whole slide fits the window, a cell
is smaller than a screen pixel, so outlines are noise; the pattern is what is
useful. The viewer builds the map itself, once, in the background; nothing is
asked of the processor run.

- **Which layers.** Every results layer whose total object count exceeds the
  draw budget, so a layer that can always be outlined never costs a scan.
- **When.** Built on attach and on Reload, in the background, one layer at a
  time, largest first; outlines and everything else work meanwhile. Detaching
  or Reload cancels a scan in progress. The map is held in memory only, not
  cached on disk **(author's call: a disk cache needs an invalidation rule
  against a store a run may still be writing; it is the first thing to add if
  attach-time scans prove too slow)**.
- **The grid.** Squares of a fixed level-0 size chosen so the longer scene side
  has at most 1024 squares (≈ 98 level-0 px on a 100 000 px slide)
  **(author's call)**. Each square holds a count per class, counted at the
  object's bounding-box centre, with the same effective label set as §5.3: an
  object counts once under each class it carries, and under "Unclassified"
  when it carries none. The 15 most frequent classes of the layer get their own
  counts; the rest are merged into "Other classes" **(author's call: bounds
  memory at 1024 × 1024 × 17 × 4 bytes ≈ 70 MB in the worst case, typically a
  fifth of that)**.
- **Drawing.** Each non-empty square is filled in the colour of its most
  frequent **visible** class — hiding a class in the legend removes it from the
  map without a rescan — with an opacity that rises with the square's visible
  count, scaled to the layer's 99th-percentile square count so one dense
  cluster does not wash out the rest **(author's call)**. With colour by class
  off, the layer colour is used. The map is drawn in the same pass and order as
  outlines, and only for layers over the budget in the current view; a layer
  under it draws outlines, so the switch between the two happens per layer as
  the user zooms.
- **Clicking** the map selects nothing; the Results tab's hint tells the user
  to zoom in to select.

### 5.4 Clicking a result

- With the selection tool, a click that hits no annotation tests the visible
  results layers, top layer first, and selects the first object whose outer
  ring contains the point and whose holes do not (even-odd over all rings).
  Annotations always win a click over results **(author's call)**.
- The selected result is outlined in the selection colour, and its details show
  in the Results tab: layer, classes, producing stage name, then each
  measurement as name, value and unit — the object's own measurements in the
  store's order, then those other stages asserted on it
  (`AnnotationStore::assertedMeasurementAt`), each marked with the asserting
  stage's name. Read-only.
- Esc or a click on empty slide clears it. Hiding its layer or class clears it.

### 5.5 The Results tab

A new dock tab beside Layers **(author's call: not inside the Layers tab, whose
active/lock/re-layer behaviour does not apply to results)**:

```
Results: /data/run-42/ws        [Reload] [Close]
  slide: …/S1303802.svs, scene 0

  [x] tissue        ■ green   fill [ ] 30%     2 objects
  [x] nuclei        ■ blue    fill [ ] 30%     48 211 objects
      [x] colour by class
          [x] ■ tumour         20 004
          [x] ■ lymphocyte     11 950
          [ ] ■ stroma         16 257
          [x] ■ Unclassified        0
      12 345 in view — overview; zoom in for outlines

  Selected: nuclei · tumour · stage "classify"
    area          41.0  px²
    area.um2      10.2  µm²
    intensity.0.mean  143.7
```

- **Reload** re-reads the layer list, classes and counts (a run may have added
  layers or classes) and redraws.
- Layer and class switches, colours, fill and opacity are view settings.

### 5.6 Remembering the attachment

Per (slide, scene), the viewer remembers the attached workspace path and the
view settings of §5.5, in QSettings keyed by the viewer's `SlideId` **(author's
call: not in the annotation file, whose format version is guarded)**. On
reopening the slide it reattaches silently when the slide check of §5.1 passes
outright; otherwise it does not attach and the status bar says why.

*Attach anyway* is remembered with the slide path it was given for: reopening
the same slide from that same path reattaches silently; from any other path
the warning of §5.1 comes back. `SlideId` is computed from the slide's content,
so it follows a moved file, and without this a user who said *Attach anyway*
would be asked again on every reopen **(author's call)**.

## 6. Code shape

- **core** (Qt-free, no processor types): plain data and policy.
  - `ResultObject` — id (as text), layer index, class indices (the effective
    set, in order; empty for unclassified), rings
    (each a point list plus hole flag) in level-0 coordinates, bounding box.
  - `ResultsSource` — the interface the overlay reads: layers (name, object
    count), classes per layer (name, count), `query(rect, layers, budget)`
    returning objects or "over budget" with the count, `details(id)` returning
    the object's measurements as (name, value, unit text). A later GeoJSON
    detection source implements the same interface.
  - `resultHitTest(object, point)` — even-odd over all rings.
  - `ResultsPalette`: palette colours in legend order, with per-class-name overrides.
  - `thinForDrawing(ring, toleranceSlideUnits)`.
  - `OverviewGrid` — the §5.3a grid: built from (centre, classes) pairs,
    answers the colour and opacity of each square for a set of visible
    classes. `ResultsSource` gains `scan(layer, callback, progress, cancel)`,
    visiting every object of a layer as its bounding box and classes only.
- **infra:** `ProcessorWorkspaceSource : ResultsSource` — the only code that
  includes processor headers. Opens the store read-only, maps `ObjectRecord`,
  `Polygon` and `MeasurementSet` to core types, runs queries
  (`ObjectQuery().layer(l).intersecting(rect).limit(budget + 1)`).
- **app:** `ResultsOverlayService` — owns the attached source, the view
  settings and a query worker: one background thread that runs the latest
  requested query (dropping superseded requests) and posts results back to the
  UI thread, as `TileLoadScheduler` posts tiles. Queries cover the view grown
  by half a view each side, so small pans need no new query. Store access is
  serialised on that thread (a `Store` is used from one thread at a time).
  A second background thread builds overview grids, through **its own**
  read-only `Store` opened on the same file, so a scan never holds up the
  queries that pans need; it hands each finished grid to the UI thread.
- **ui:** the painting in `ViewportWidget` (a `paintResults` pass before
  `paintAnnotations`), the click routing, the Results tab, the menu items.

## 7. Errors and edge cases

- A query that throws (store file deleted, disk error) detaches the workspace
  with a warning naming the path; the viewer keeps running.
- A layer dropped by a later run disappears on Reload; a selected object that
  no longer exists clears the selection.
- A stage that is stale or failed still shows its objects (they are what the
  store holds); the Results tab marks the layer "stale" or "failed" from
  `StageStateReader` **(author's call)**.
- An empty workspace (no layers with objects) attaches and says "no results".
- Objects partly outside the scene are drawn as stored.

## 8. Tests

- **Library (slideio-processor):** the data-only configuration builds without
  SlideIO and its tests pass; a consumer project finds the installed package
  and links it.
- **Viewer core:** hit test with holes (inside a hole is a miss), class colour
  matching and hashing, thinning keeps endpoints and never drops a ring below 3
  points, over-budget reporting. `OverviewGrid`: counts land in the square of
  the bounding-box centre; a two-class object counts under both; the 16th and
  later classes merge into "Other classes"; the dominant colour skips hidden
  classes; opacity scaling uses the 99th percentile.
- **Viewer infra:** a fixture store built in the test with the data library's
  own writer (`Store::open` ReadWrite, `objects().writerForStage(...)`):
  objects with and without holes and labels in two layers. Tests: rectangle
  queries return exactly the intersecting objects; the budget limit; label and
  measurement mapping; slide/scene provenance read; a ReadOnly open succeeds
  while a ReadWrite store is open on the same file.
- **Viewer app:** the query worker drops superseded requests and delivers the
  latest; detaching mid-query delivers nothing; a scan reports progress, is
  cancelled by detach and Reload, and a cancelled scan delivers no grid.
- No suite creates a `QApplication`, so drawing, clicking and the tab are
  manual checks (§9).

## 9. Manual checks — group AN

Against a workspace from `examples/tissue_detection.py` (2 tissue regions with
hundreds of holes) and a nuclei workspace of at least 50 000 objects:

1. Attach; tissue outlines with holes match the slide (holes unfilled with fill
   on).
2. Zoomed out on the nuclei workspace: after "building overview…", the
   nuclei show as the overview map, coloured by dominant class, while tissue
   still draws as outlines; zooming in switches nuclei to outlines; panning
   stays smooth during and after the scan. Repeat with a 1 000 000-object
   workspace (a synthetic one is fine) and record the scan time.
3. Click a nucleus: details show; click inside a tissue hole: nothing selected.
4. Classes coloured from the palette; changing a class colour sticks on
   another slide with the same class; hiding a class hides it.
5. Attach while a processor run is writing the workspace: the run is not
   blocked; after a stage completes, Reload shows the new layer.
6. Wrong scene refused; moved slide warns and attaches on *Attach anyway*.
7. Close and reopen the slide: reattached with the same settings.
8. Results never appear in the annotation list, counts, exports or the
   annotation file.

## 10. Left to the builder

- The exact palette entries beyond FR-ANN-15's five, and the Results tab's widgets.
- How the worker's posted results reach the widget (signal or callback), and
  whether the grown query rectangle is half a view or a tuned figure.
- The draw budget's exact value if 20 000 proves too slow or too timid on the
  test workspaces — it is one constant, and §9 check 2 decides it.
- How details format numbers and units.

## 11. Risks

- **Dependency clashes on Windows/macOS packaging** (two zlib or libtiff
  copies). Checked when packaging; the fallback is building the data library
  against the viewer's Conan versions.
- **Query cost at the budget edge**: 20 000 objects with full geometry per
  query, plus an `AnnotationStore` label lookup per object for the effective
  set. If a query takes longer than about 100 ms the overlay lags pans; the
  budget or the grown rectangle shrinks.
- **Overview scan time.** A scan reads every object through the library's
  cursor, which decodes full geometry and needs an `AnnotationStore` lookup
  per object for labels, though the map uses only the bounding box and the
  classes. At a million objects that may take tens of seconds. The fixes, in
  order: a disk cache of the grid (§5.3a), then a library query that returns
  bounding boxes and labels without geometry, then the processor writing the
  map during the run (the alternative the owner set aside).
- **The store format moves on**: a viewer linked to an older data library
  refuses a newer store with a clear message (§5.1), and updating the submodule
  pin is the fix.
