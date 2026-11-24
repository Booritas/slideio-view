# Processing Results Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a slideio-processor workspace's objects over the slide as a read-only overlay — outlines with holes, colour by class, an overview map where there are too many to outline, click for measurements — reading the store directly through `slideio-processor-data`.

**Architecture:** core holds Qt-free types and policy (`ResultsSource` interface, hit test, palette, `OverviewGrid`, attachment decision, settings interface). infra implements `ResultsSource` over the processor store (`ProcessorWorkspaceSource`, the only code that sees processor headers) and the QSettings store. app's `ResultsOverlayService` owns a query thread and a scan thread and posts results to the UI thread through an injected poster. ui paints, routes clicks, and adds the Results tab and menu items.

**Tech Stack:** C++17, Qt 6.7, CMake, Conan 2, Catch2, slideio-processor-data (submodule, data-only).

**Spec:** `docs/superpowers/specs/2026-11-24-processing-results-overlay-design.md` (read it whole; section numbers below refer to it).

**Branch:** all work on `feat/processing-results` (spec §4.1); never commit to `main`.

**Prerequisite:** slideio-processor plan `docs/superpowers/plans/2026-11-24-data-only-build.md` done and its branch pushed, so the submodule can pin a commit with `SLIDEIO_PROCESSOR_DATA_ONLY`.

## Global Constraints

- core stays Qt-free and processor-free; no processor type in any core, app or ui header. infra links `slideio-processor-data` PRIVATE.
- The viewer never opens a store other than `OpenMode::ReadOnly`, never creates files in a workspace (§2 anti-goals).
- Results never enter `AnnotationModel`, the annotation list, counts, exports or the annotation file.
- No store access on the UI thread except attach/reload metadata reads; queries, details and scans run on the service's threads (§6).
- Draw budget `20 000` objects per layer in view (`kResultsDrawBudget`). Overview grid: at most `1024` squares on the scene's longer side, `15` tracked classes plus "Other classes" and "Unclassified" (§5.3a).
- Outline width `1.5` screen px; fill off by default, default fill opacity `0.30` (§5.2).
- Text, exactly: `"<n> in view — overview; zoom in for outlines"` and `"building overview… <p> %"` (n with a thin-space thousands separator as the list panel formats counts, p an integer).
- Palette: FR-ANN-15's colourblind-safe colours first (blue, orange, yellow, purple, cyan), then further distinct colours; legend order, cycling (§5.3).
- Paths may appear in messages and logs (owner's decision for the processor; same here). Workspace paths in QSettings fall under HIPAA/GDPR controls like the viewer's other per-slide settings (§2 constraint).
- Every task ends with all four test suites passing (`ctest --test-dir build/<config> -C <Config>`).
- Commits are human-reviewed before push; push and merge only on the owner's instruction.

## Review Focus

1. **A path to the store file or to a folder without `workspace.store`** chosen in Open Processing Results: must warn naming the folder, never crash or create anything — Task 5 test `OpenRefusesFolderWithoutStore`.
2. **Slide paths that differ only by case or separator on Windows** (`C:\Slides\a.svs` vs `c:/slides/A.svs`): must count as the same slide on Windows and different elsewhere — Task 4 test `SameSlidePathFoldsCaseOnWindows`.
3. **A processor run writing the workspace while the viewer reads it**: the read-only open must succeed beside a writer and later queries see newly committed objects — Task 5 test `ReadOnlySeesObjectsCommittedAfterOpen`.
4. **Detaching or closing the slide while a query or scan is in flight**: nothing posted after detach reaches the UI; the destructor joins both threads — Task 6 tests `DetachDropsInFlightResults` and `DestructorJoinsWorkers`.
5. **Objects with holes and with several outer rings**: a point inside a hole is a miss; a point in either outer ring is a hit — Task 2 tests `HoleIsAMiss` and `SecondOuterRingIsAHit`.

---

### Task 1: Link slideio-processor-data into the viewer

**Files:**
- Modify: `.gitmodules`; add submodule `extern/slideio-processor`
- Modify: `conanfile.py`, `CMakeLists.txt`, `src/infra/CMakeLists.txt`, `tests/CMakeLists.txt`
- Modify: `build-template.sh` (submodule init), `.github/workflows/build-validation.yml`
- Create: `tests/infra/ProcessorDataLinkTest.cpp`

**Interfaces:**
- Produces: target `slideio-processor-data` available to infra; the DLL/so beside the app and on the test PATH; installed with the app.

- [ ] **Step 1: Add the submodule, pinned**

`git submodule add https://github.com/Booritas/slideio-processor.git extern/slideio-processor`, checked out at the head of slideio-processor `feat/data-only-build` (or its merge commit on `main` if merged by then). Do not initialise its nested submodules. In `build-template.sh`, initialise `extern/slideio` recursively as today and `extern/slideio-processor` **non-recursively**. In CI (`actions/checkout` with `submodules: recursive`) replace by `submodules: false` plus a step running the same two init commands.

- [ ] **Step 2: Conan dependencies**

`conanfile.py`: add `sqlite3/3.44.2` (option `enable_rtree = True`), `rapidjson/cci.20250205`, `libtiff/4.7.1` with the processor's libtiff options (`zlib=True`, `jpeg=False`, `jbig lzma zstd webp libdeflate cxx = False`), and `zlib/1.3.2` — the versions `extern/slideio-processor/conanfile.py` pins. If Qt's graph forces another zlib, use Conan's `override=True` on the processor's version and say so in the commit message.

- [ ] **Step 3: CMake**

Top-level `CMakeLists.txt`, after the `find_package` block:
```cmake
set(SLIDEIO_PROCESSOR_DATA_ONLY ON CACHE BOOL "" FORCE)
set(SLIDEIO_PROCESSOR_TEST_SEAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/slideio-processor EXCLUDE_FROM_ALL)
```
`src/infra/CMakeLists.txt`: `PRIVATE slideio-processor-data`. Copy `$<TARGET_FILE:slideio-processor-data>` next to `slideio-viewer` with a POST_BUILD `copy_if_different` (Windows and Linux; macOS through the bundle's Frameworks as the install does). Install it with the app: `install(TARGETS slideio-processor-data RUNTIME DESTINATION ${_install_bin} LIBRARY DESTINATION ${_install_lib})` (Apple: into the bundle's Frameworks). `tests/CMakeLists.txt`: prepend `$<TARGET_FILE_DIR:slideio-processor-data>` to the `PATH` modification of infra-tests, app-tests and ui-tests, and add `infra/ProcessorDataLinkTest.cpp` to infra-tests (link `slideio-processor-data` PRIVATE to the test target as well).

CI: add `feat/processing-results` to the `push` branches of `build-validation.yml`.

- [ ] **Step 4: Write the link test**

```cpp
TEST_CASE("ProcessorDataLinkTest: a missing store fails to open read-only")
{
    const auto dir = <a fresh temp directory>;
    REQUIRE_THROWS(slideio::processor::Store::open((dir / "workspace.store").string().c_str(),
                                                    slideio::processor::OpenMode::ReadOnly,
                                                    slideio::processor::Configuration()));
}
```

- [ ] **Step 5: Build and test**

Run: `./build.sh` then `ctest --test-dir build/Release -C Release --output-on-failure`
Expected: all four suites pass, including the new test; `slideio-processor-data` DLL/so sits next to `slideio-viewer`.

- [ ] **Step 6: Commit**

```bash
git add .gitmodules extern/slideio-processor conanfile.py CMakeLists.txt src/infra/CMakeLists.txt tests/CMakeLists.txt tests/infra/ProcessorDataLinkTest.cpp build-template.sh .github/workflows/build-validation.yml
git commit -m "build: link slideio-processor-data (data-only submodule)"
```

### Task 2: Core result types, hit test, thinning and palette

**Files:**
- Create: `src/core/include/slideio/viewer/core/ResultsSource.h`, `ResultGeometry.h`, `ResultsPalette.h`
- Create: `src/core/src/ResultGeometry.cpp`, `src/core/src/ResultsPalette.cpp`
- Test: `tests/core/ResultGeometryTest.cpp`, `tests/core/ResultsPaletteTest.cpp`

**Interfaces:**
- Produces (namespace `slideio::viewer::core`), in `ResultsSource.h`:
```cpp
struct ResultRing { std::vector<PointF> points; bool hole = false; };
struct ResultObject {
    std::string id;                 // the processor's ObjectId text
    int layer = -1;                 // index into ResultsSource::layers()
    std::vector<int> classes;       // indices into that layer's classes, effective set order; empty = unclassified
    std::vector<ResultRing> rings;  // level-0 coordinates
    Rect<double> bounds;
};
struct ResultClassInfo { std::string name; int64_t objectCount = 0; };
struct ResultLayerInfo {
    std::string name; int64_t objectCount = 0;
    std::vector<ResultClassInfo> classes;   // legend order: descending objectCount, then name
    std::string stageState;                 // "", "stale" or "failed" (§7)
};
struct ResultsProvenance { std::string slidePath; int scene = 0; };
struct ResultMeasurement { std::string name; double value = 0.0; std::string unit; std::string assertedBy; };
struct ResultDetails {
    std::string layer; std::vector<std::string> classes; std::string stage;
    std::vector<ResultMeasurement> measurements;  // own first (store order), then asserted
};
struct ResultQueryAnswer { bool overBudget = false; int64_t inView = 0; std::vector<ResultObject> objects; };
class ResultsError : public std::runtime_error { using std::runtime_error::runtime_error; };
class ResultsSource {
public:
    virtual ~ResultsSource() = default;
    virtual ResultsProvenance provenance() const = 0;
    virtual std::vector<ResultLayerInfo> layers() const = 0;
    // Objects of `layer` intersecting `rect`; overBudget with inView set when more than `budget`.
    virtual ResultQueryAnswer query(int layer, const Rect<double>& rect, int64_t budget) = 0;
    virtual std::optional<ResultDetails> details(const std::string& objectId) = 0;
    // Visits every object of `layer` as (bounds, classes); `progress(fraction)` returning false cancels.
    virtual void scan(int layer, const std::function<void(const Rect<double>&, const std::vector<int>&)>& visit,
                      const std::function<bool(double)>& progress) = 0;
};
constexpr int64_t kResultsDrawBudget = 20000;
```
  `ResultGeometry.h`: `bool resultContains(const ResultObject& object, PointF point);` (even-odd over all rings) and `std::vector<PointF> thinForDrawing(const std::vector<PointF>& ring, double toleranceSlideUnits);` (skips a point closer than the tolerance to the last kept one; keeps first and last; never returns fewer than 3 points for a ring of 3 or more).
  `ResultsPalette.h`: `const std::vector<Color>& resultsPalette();` and `Color resultClassColour(const std::string& className, int legendIndex, const std::map<std::string, Color>& overridesByFoldedName);` (override looked up by `caseFoldKey(className)`, else `resultsPalette()[legendIndex % size]`), `Color resultLayerColour(int layerIndex);`.

- [ ] **Step 1: Write the failing tests**

`ResultGeometryTest.cpp`:
- `HoleIsAMiss`: square 0..100 with hole 40..60 → `(50,50)` false, `(10,10)` true, `(150,50)` false.
- `SecondOuterRingIsAHit`: outer rings 0..10 and 20..30 → `(25,25)` true, `(15,15)` false.
- `ThinKeepsEndsAndMinimum`: 1000 collinear points 0.01 apart, tolerance 1 → first and last kept, size ≥ 3 (when the input is closed and ≥ 3); a 3-point ring with tolerance 1000 stays 3 points.

`ResultsPaletteTest.cpp`:
- First five palette entries are FR-ANN-15's blue, orange (`0xE6,0x7E,0x22`), yellow, purple, cyan, in that order; the palette has at least 10 distinct colours.
- `resultClassColour("Tumour", 0, {{caseFoldKey("tumour"), red}}) == red`; without override, legend index `size+1` gives entry 1.

- [ ] **Step 2: Run them to see them fail** — `ctest -R core-tests` fails to build (headers missing).

- [ ] **Step 3: Implement**, adding the two `.cpp` files to `src/core/CMakeLists.txt` and the tests to `tests/CMakeLists.txt`.

- [ ] **Step 4: Run** `ctest --test-dir build/Release -C Release -R core-tests` — PASS.

- [ ] **Step 5: Commit** `feat(core): result types, hit test, thinning, palette`

### Task 3: Core OverviewGrid

**Files:**
- Create: `src/core/include/slideio/viewer/core/OverviewGrid.h`, `src/core/src/OverviewGrid.cpp`
- Test: `tests/core/OverviewGridTest.cpp`

**Interfaces:**
- Consumes: Task 2's `Rect<double>`, class indices.
- Produces:
```cpp
class OverviewGrid {
public:
    static constexpr int kMaxSquaresLongSide = 1024;
    static constexpr int kTrackedClasses = 15;
    static constexpr int kOtherClasses = -2;
    static constexpr int kUnclassified = -1;
    // sceneRect: level-0 scene rectangle; classCounts: the layer's per-class object counts (legend order).
    OverviewGrid(const Rect<double>& sceneRect, const std::vector<int64_t>& classCounts);
    void add(const Rect<double>& bounds, const std::vector<int>& classes);  // at the bounds' centre
    void finish();                                     // computes the 99th-percentile square count
    double squareSize() const; int columns() const; int rows() const;
    struct Cell { int column; int row; int dominant; double opacity; };  // dominant: class index, kOtherClasses or kUnclassified
    std::vector<Cell> cells(const Rect<double>& view, const std::vector<bool>& classVisible, bool unclassifiedVisible) const;
};
```
  Tracked classes are the `kTrackedClasses` largest by `classCounts` (ties: lower index); the rest count into `kOtherClasses`, which is visible when any of its member classes is visible. Opacity of a cell: `0.15 + 0.60 * min(1, visibleCount / p99)` where `visibleCount` counts objects of visible classes in the square (an object with two visible classes counts once) **(plan's choice of the curve the spec leaves open)**.

- [ ] **Step 1: Write the failing tests** — `SquareSizeCapsLongSide` (scene 100 000 × 50 000 → `columns() <= 1024`, `squareSize() >= 100000/1024`); `CountsAtCentre` (a box straddling two squares lands in the square of its centre); `TwoClassObjectCountsUnderBoth` (dominant reflects both counts; visible count counts it once); `SixteenthClassMergesIntoOther`; `DominantSkipsHiddenClasses`; `OpacityUsesP99` (one square of 1000 among 100 squares of 10: the 10s get opacity > 0.7); `CellsOutsideViewAreNotReturned`.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Per square: a `uint32_t` count per bucket (each tracked class, Other classes, Unclassified) and a `uint32_t` object count. An object adds 1 to every bucket it carries (Unclassified when it carries none) and 1 to the object count. `visibleCount = min(objectCount, sum of visible buckets)` — exact when no object carries two classes, and never above the square's objects otherwise **(plan's rule; visibility changes after the scan, so per-object visibility cannot be stored)**. Test `TwoClassObjectCountsUnderBoth` pins the single-object case (count 1).
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(core): overview grid`

### Task 4: Attachment policy and remembered settings

**Files:**
- Create: `src/core/include/slideio/viewer/core/ResultsAttachment.h`, `src/core/src/ResultsAttachment.cpp`
- Create: `src/infra/include/slideio/viewer/infra/QSettingsResultsSettingsStore.h`, `src/infra/src/QSettingsResultsSettingsStore.cpp`
- Test: `tests/core/ResultsAttachmentTest.cpp`, `tests/infra/QSettingsResultsSettingsStoreTest.cpp`

**Interfaces:**
- Produces (core):
```cpp
bool sameSlidePath(const std::string& a, const std::string& b);  // lexically normalised absolute paths; case-folded on Windows only
enum class AttachmentDecision { Attach, WarnSlideMoved, RefuseScene };
AttachmentDecision decideAttachment(const ResultsProvenance& workspace, const std::string& openSlidePath, int openScene,
                                    const std::string& attachAnywaySlidePath);  // "" when none remembered
struct ResultsLayerView {
    std::string layerName; bool visible = true; std::optional<Color> colour; bool fill = false;
    double fillOpacity = 0.30; bool colourByClass = true; std::vector<std::string> hiddenClasses;  // class names
};
struct ResultsAttachmentSettings {
    std::string slideId; int scene = 0; std::string workspacePath; std::string attachAnywaySlidePath;
    std::vector<ResultsLayerView> layers;
};
class IResultsSettingsStore {
public:
    virtual ~IResultsSettingsStore() = default;
    virtual std::optional<ResultsAttachmentSettings> find(const std::string& slideId, int scene) const = 0;
    virtual bool set(const ResultsAttachmentSettings& entry) = 0;
    virtual void remove(const std::string& slideId, int scene) = 0;
    virtual std::map<std::string, Color> classColours() const = 0;          // keyed by caseFoldKey
    virtual void setClassColour(const std::string& className, Color colour) = 0;
};
```
  `decideAttachment`: different scene → `RefuseScene`; same scene and `sameSlidePath(workspace.slidePath, openSlidePath)` → `Attach`; same scene and `sameSlidePath(attachAnywaySlidePath, openSlidePath)` → `Attach`; else `WarnSlideMoved`.
- Produces (infra): `QSettingsResultsSettingsStore` (default ctor = application settings; `explicit (const std::string& iniFilePath)` for tests), keys `results/attachments/<slideId>.s<scene>/…` and `results/classColours/<caseFoldKey>`, as `QSettingsCalibrationOverrideStore` lays its keys out.

- [ ] **Step 1: Failing tests** — core: `SameSlidePathFoldsCaseOnWindows` (`#ifdef _WIN32` true for `C:\Slides\a.svs` vs `c:/slides/A.svs`, else false), `SameSlidePathNormalisesDots` (`/a/b/../c.svs` == `/a/c.svs`), each `decideAttachment` branch including "remembered override for another path still warns". infra: round-trip of a full `ResultsAttachmentSettings` with two layers, a colour, hidden classes with commas and slashes in their names; `remove`; class colours keyed case-insensitively (`setClassColour("Tumour")`, read back under `caseFoldKey("TUMOUR")`).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (`std::filesystem::path(...).lexically_normal()`, made absolute; no filesystem access needed by the rule).
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat: results attachment policy and settings store`

### Task 5: ProcessorWorkspaceSource

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/ProcessorWorkspaceSource.h`, `src/infra/src/ProcessorWorkspaceSource.cpp`
- Create: `tests/infra/support/ProcessorFixture.h`, `tests/infra/support/ProcessorFixture.cpp` (test-only, links the data library)
- Test: `tests/infra/ProcessorWorkspaceSourceTest.cpp`

**Interfaces:**
- Consumes: Task 2's `ResultsSource` and types.
- Produces: `class ProcessorWorkspaceSource : public core::ResultsSource` with `static std::unique_ptr<ProcessorWorkspaceSource> open(const std::string& workspaceDirectory);` — throws `core::ResultsError` whose message names the folder when `<dir>/workspace.store` is missing ("no workspace.store in <dir>") or when the store fails to open (the library's message appended). The header includes no processor header (pimpl).
- Mapping rules: results layers = non-dropped layers except the pixel layer with `objectCountInLayer > 0`, in registry order; classes = the vocabulary labels found on the layer's objects (effective set), with counts from `count(ObjectQuery().layer(l).label(c))`; `stageState` from the layer's writer stages (`"failed"` if any failed, else `"stale"` if any stale); `query` runs `count` with `layer().intersecting(rect)` first and returns `overBudget` without fetching when above `budget`; rectangles convert to `Rect64` by `floor` of the origin and `ceil` of the far corner; units: `Pixels "px"`, `Micrometres "µm"`, `SquarePixels "px²"`, `SquareMicrometres "µm²"`, `Intensity ""`, `Dimensionless ""`; ids are `ObjectId::toString` text and `details` parses them back (unknown id → `std::nullopt`).
- `ProcessorFixture` (tests): `ProcessorFixture::create(dir)` builds a store with slide path `/slides/fixture.svs`, scene 1, two semantic layers `tissue` and `nuclei` declared through `LayerRegistry`, stages declared through `StageStateWriter::declare` and set Complete, objects written through `objects().writerForStage(...)`: one tissue square 0..1000 with a hole 400..600; 30 nuclei 10×10 on a grid inside 0..1000, 10 labelled `tumour` as the row label, 10 labelled `stroma` through an `AnnotationStore` assertion by a second stage, 10 unlabelled; each nucleus with an `area` measurement and one asserted `intensity.mean`.

- [ ] **Step 1: Failing tests** — `OpenRefusesFolderWithoutStore` (message contains the folder), `ProvenanceIsRead` (`/slides/fixture.svs`, 1), `LayersAndClasses` (two layers in order; nuclei classes `tumour` 10, `stroma` 10 — the asserted one counted), `QueryReturnsIntersectingWithHoles` (rect 0..50 returns the tissue object with 2 rings, one a hole), `QueryOverBudget` (budget 5 on nuclei over the whole rect → `overBudget`, `inView == 30`, no objects), `ClassesIncludeAssertedLabels` (a stroma nucleus has the stroma class index), `DetailsIncludeAssertedMeasurements` (own `area` with unit `px²` first, then `intensity.mean` with `assertedBy` the second stage's name), `ScanVisitsEveryObjectAndCancels` (30 visits; returning false from progress at the first call stops early), `ReadOnlySeesObjectsCommittedAfterOpen` (fixture's store kept open ReadWrite, viewer source opened, one more nucleus written and committed, a new query sees 31).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Store access is single-threaded per instance; the service opens a second instance for scans (Task 6).
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(infra): processor workspace results source`

### Task 6: ResultsOverlayService

**Files:**
- Create: `src/app/include/slideio/viewer/app/ResultsOverlayService.h`, `src/app/src/ResultsOverlayService.cpp`
- Test: `tests/app/ResultsOverlayServiceTest.cpp` (with a `FakeResultsSource` in the test file)

**Interfaces:**
- Consumes: Tasks 2–4.
- Produces:
```cpp
class ResultsOverlayService {
public:
    using Poster = std::function<void(std::function<void()>)>;                 // runs a task on the UI thread
    using SourceFactory = std::function<std::unique_ptr<core::ResultsSource>()>; // each call opens a new source
    explicit ResultsOverlayService(Poster post);
    ~ResultsOverlayService();                                    // detaches, joins both threads
    void attach(SourceFactory factory, const core::Rect<double>& sceneRect);  // reads layers on the calling thread; throws core::ResultsError
    void detach();
    void reload();                                               // re-reads layers, restarts scans, re-queries
    bool attached() const;
    const std::vector<core::ResultLayerInfo>& layers() const;
    void requestView(const core::Rect<double>& visibleSlideRect); // latest request wins
    enum class LayerMode { Hidden, Outlines, Overview, BuildingOverview };
    struct LayerState {
        LayerMode mode = LayerMode::Hidden; int64_t inView = 0; double scanFraction = 0.0;
        std::vector<core::ResultObject> objects; std::shared_ptr<const core::OverviewGrid> grid;
    };
    const LayerState& layerState(int layer) const;
    void setLayerView(int layer, const core::ResultsLayerView& view);  // visibility, colour, fill, colour-by-class, hidden classes
    const core::ResultsLayerView& layerView(int layer) const;
    bool objectVisible(const core::ResultObject& object) const;   // any class visible, or unclassified visible
    std::optional<std::string> selectAt(core::PointF slidePoint);  // top (highest index) visible Outlines layer first
    void clearSelection();
    const std::optional<std::string>& selectedId() const;
    const std::optional<core::ResultDetails>& selectedDetails() const;  // arrives later, posted
    void setOnChanged(std::function<void()> callback);             // called on the UI thread after each posted update
    void setOnError(std::function<void(const std::string&)> callback);  // a source error on a worker: posted, then the service detaches (§7)
};
```
  Selection is cleared when its layer or all its classes are hidden, on detach, and on reload when the object is gone.
  Query thread: for each visible layer, `query(layer, view grown by half its width/height each side, kResultsDrawBudget)`; skip the query when the new view lies inside the last grown rectangle and no setting changed. Layers whose total `objectCount` exceeds the budget get a grid built on the scan thread through a second source from the factory, largest layer first; `BuildingOverview` until posted. A layer over budget in view uses `Overview` when its grid exists, else `BuildingOverview`. Every posted task carries an attach generation number and is dropped if `detach()`/`reload()` ran since. `selectAt` hit-tests the layer's current `objects` with `resultContains` and requests details on the query thread.

- [ ] **Step 1: Failing tests** (the poster appends to a queue the test drains; the fake source records calls and can block a query on a latch): `OutlinesUnderBudget`, `OverviewWhenOverBudgetAfterScan` (BuildingOverview, then Overview after draining), `LatestViewWins` (three requests while the fake blocks → only the first and the last run), `SmallPanReusesGrownRect`, `HiddenClassHidesObjectOnlyWhenAllItsClassesHidden`, `SelectTopLayerFirst`, `DetailsArrivePosted`, `DetachDropsInFlightResults`, `ReloadRestartsScan`, `SourceErrorDetachesAndReports` (the fake throws `core::ResultsError("gone")` from `query` → `onError("gone")` once, `attached()` false), `HidingClassClearsSelection`, `DestructorJoinsWorkers` (destroy while a query blocks; release the latch from another thread; no task runs after).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (two `std::thread`s with a mutex and condition variable each; no Qt in the service; add to `src/app/CMakeLists.txt`).
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(app): results overlay service`

### Task 7: Drawing and clicking in the viewport

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, `src/ui/src/ViewportWidget.cpp`
- Create: `src/ui/include/slideio/viewer/ui/ResultsPaintPolicy.h`, `src/ui/src/ResultsPaintPolicy.cpp`
- Test: `tests/ui/ResultsPaintPolicyTest.cpp`

**Interfaces:**
- Consumes: Task 6's service, Task 2's palette and thinning, Task 3's cells.
- Produces: `void ViewportWidget::setResultsService(app::ResultsOverlayService* service);` — the widget calls `requestView(viewport.visibleSlideRect())` whenever it emits `viewportChanged` and on attach; paints in a `paintResults(QPainter&)` pass placed immediately before `paintAnnotations` in the same `QPainter` block of `paintGL` (results under annotations, outside the snapshot capture); the overlay-needed test (`overlayNeedsPainting`) also returns true when results are attached.
  `ResultsPaintPolicy.h` (Qt-free): `core::Color objectColour(const core::ResultObject&, const core::ResultsLayerView&, const std::vector<core::Color>& classColours, core::Color layerColour);` (first class's colour when colour-by-class is on and the object has a class, else the layer colour) and `core::Color overviewColour(int dominant, ...)` (Other classes: grey `0x9E,0x9E,0x9E`; Unclassified: the layer colour).
- Clicking: in the release branch where `annotationAt` returns empty (today `clearSelection()` at `ViewportWidget.cpp` near the `releaseSelectsAnnotation` call), call `selectAt` on the service; a result selection clears the annotation selection and vice versa; Esc clears it. The selected result is outlined in the annotation selection colour, 2.5 px.

- [ ] **Step 1: Failing tests** for `ResultsPaintPolicy` (class colour vs layer colour, colour-by-class off, Other and Unclassified colours).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** the policy, then the widget changes: each ring is a sub-path of one `QPainterPath` with `Qt::OddEvenFill`, points thinned with `thinForDrawing` at half a screen pixel converted with `screenToleranceToSlide`; overview cells as filled rectangles in `overviewColour` at the cell's opacity.
- [ ] **Step 4: Run** all suites — PASS.
- [ ] **Step 5: Commit** `feat(ui): draw and select processing results`

### Task 8: Results tab, menu, attach flow and documentation

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/ResultsPanel.h`, `src/ui/src/ResultsPanel.cpp`, `src/ui/include/slideio/viewer/ui/ResultsPanelText.h`, `src/ui/src/ResultsPanelText.cpp`
- Modify: `src/ui/src/MainWindow.cpp`, `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/ui/ResultsPanelTextTest.cpp`
- Modify: `CLAUDE.md` (a short "Processing results" paragraph under the architecture, and the manual check group AN of spec §9 listed where the other groups are indexed)

**Interfaces:**
- Consumes: everything above; `infra::ProcessorWorkspaceSource::open`, `infra::QSettingsResultsSettingsStore`.
- Produces: `ResultsPanelText.h`: `std::string inViewText(int64_t n);` → `"12 345 in view — overview; zoom in for outlines"`; `std::string buildingText(double fraction);` → `"building overview… 43 %"`; `std::string measurementLine(const core::ResultMeasurement&);` → `"area  41.0 px²"`, with `" (classify)"` appended when `assertedBy` is set; `std::string stageStateBadge(const std::string&)` → `""`, `"stale"`, `"failed"`. A workspace with no results layers attaches and the tab says `"no results"` (§7); `onError` shows a warning with the message and the tab returns to its detached state.
- MainWindow: actions **Open Processing Results…** and **Close Processing Results** in the File menu after *Import Annotations…*, enabled only with a slide open (Close only when attached). Open: `QFileDialog::getExistingDirectory`; build a factory `[dir]{ return infra::ProcessorWorkspaceSource::open(dir); }`; open once to read provenance; `decideAttachment` with the remembered `attachAnywaySlidePath` → `RefuseScene`: warning naming the workspace's scene; `WarnSlideMoved`: question box with both paths, *Attach anyway* / *Cancel* (on *Attach anyway*, remember the open slide's path); `Attach`: attach. `core::ResultsError` → warning with its message. On slide open: `find(slideId, scene)`; reattach silently when `decideAttachment` says `Attach`, else a status-bar message saying why. Settings saved on every view change. Slide close detaches. The Results tab is a dock tab next to Layers, laid out as spec §5.5: per layer a checkbox, colour button, fill checkbox and opacity, object count, state badge; under it "colour by class", the legend rows (checkbox, colour button, name, count) with a tooltip saying a two-class object counts in both rows; the in-view or building line; Reload and Close buttons; the selected object's details.

- [ ] **Step 1: Failing tests** for `ResultsPanelText` (the four functions, thousands separator as the list panel uses, 0 % and 100 %).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** the text helpers, the panel and the MainWindow wiring; the poster is `[this](auto task){ QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection); }`; `setOnChanged` updates the panel and calls `viewportWidget->update()`.
- [ ] **Step 4: Run** all suites — PASS; then build and launch the app, attach the workspace from `slideio-processor-python/examples/tissue_detection.py` on `JP2K-33003-1.svs`, and confirm the two tissue regions draw with their holes.
- [ ] **Step 5: Update `CLAUDE.md`** and list spec §9's checks as group AN for the owner to run.
- [ ] **Step 6: Commit** `feat(ui): Results tab and Open Processing Results`
