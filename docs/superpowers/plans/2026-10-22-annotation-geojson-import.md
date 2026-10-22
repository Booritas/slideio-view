# GeoJSON Import of Annotations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `File > Import Annotations from GeoJSON...` reads a QuPath or slideio GeoJSON file, shows one summary of what will happen, and on confirmation adds the annotations to the current scene as one undoable, autosaved step.

**Architecture:** A pure reader in core (`geoJsonToAnnotations`, beside the writer in `AnnotationGeoJson.{h,cpp}`) turns file text into `core::Annotation`s plus skip counts and provenance. The app layer gains bulk insert/remove on `AnnotationModel`, an `AnnotationImportCommand`, and `planImport` (mints UUIDs, drops ids already in the scene). The ui gains a pure summary builder and the `MainWindow` action, gated with the drawing tools in `applyAnnotationAvailability`.

**Tech Stack:** C++17, nlohmann_json (core, PRIVATE), Qt 6 Core (app) and Widgets (ui), Catch2 v3.

**Spec:** `docs/superpowers/specs/2026-10-22-annotation-geojson-import-design.md`

## Global Constraints

- Core stays Qt-free; nlohmann appears in no core public header. Core never mints ids: an annotation needing one is returned with an **empty id**.
- Import **adds**, never replaces. Coordinates are level-0 scene pixels, untransformed.
- Accepted top level: `FeatureCollection`, a single `Feature`, or a JSON array of Features. A UTF-8 BOM is skipped. Anything else is a read failure that changes nothing.
- Annotation features: `properties.objectType` (or legacy `properties.object_type`) == `"annotation"`, or absent. Other values are counted per value and skipped. `childObjects` are counted (recursively) and skipped.
- Geometry rules, colour precedence, property sources and fallbacks: exactly spec §3 ("Geometry mapping", "Shapes this viewer exported", "Properties"). Type names are the serializer's: `rectangle`, `ellipse`, `polygon`, `polyline`, `point`, `arrow`.
- Vertex minimums are the serializer's `kMinPolygonVertices` (3, counted after dropping a closing repeat) and `kMinPolylineVertices` (2). **Positions are counted, not distinct positions**, although spec §3 says "distinct": the serializer counts stored vertices, and a degenerate polygon the viewer saved and exported must import back (spec §2 goal: its own exports return exactly).
- An id already in the scene, or repeated within the file, is skipped and counted "already in this scene".
- Every feature is accounted for: `featuresRead == featuresImported + every per-feature skip count`.
- Gating: the action lives in `applyAnnotationAvailability` beside the six tool actions, never paired with `closeAction`. The commit re-checks `annotationPersistence->isActive()`.
- Dialog filter `GeoJSON (*.geojson *.json);;All files (*)`, opened in `annotationSettings->workspaceDirectory()`.
- Never run a Qt-linked test exe directly — only through `ctest` (`core-tests` is the exception).

## Review Focus

1. **Re-importing the viewer's own export onto the scene it came from** — expected: nothing added, every feature counted "already in this scene"; a duplicated clinical count is the worst outcome. Pinned in Task 3 (`planImport`) and Task 1 (round trip keeps ids).
2. **A QuPath detection export of tens of thousands of objects** — expected: all counted as `detection`/`cell` and skipped quickly, nothing imported. Pinned in Task 1 (20 000 detections, count only, no timing assertion).
3. **Numbers in unexpected JSON types** — a coordinate written as a string, `null`, a boolean, a nested array; an `id` as a number; `name` as a number — expected: invalid geometry counted, or the field ignored, never an exception escaping the reader. Pinned in Task 1.
4. **Invalid UTF-8 or truncated JSON** — expected: `error` set, no annotations returned, nothing thrown. Pinned in Task 1.
5. **Undo after an import followed by a further edit** — expected: undo removes the edit first, then the whole import in one step, and the model never holds duplicates. Pinned in Task 2.

---

### Task 1: Core reader

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeoJson.h`, `src/core/src/AnnotationGeoJson.cpp`
- Modify: `src/core/include/slideio/viewer/core/AnnotationSerialization.h`, `src/core/src/AnnotationSerialization.cpp` — two more public forwarders, bodies unchanged: `[[nodiscard]] bool parseAnnotationColor(const std::string& text, Color& out);` (to `parseColor`) and `[[nodiscard]] bool isRepresentableStyle(const AnnotationProperties& properties);` (to the private `isRepresentable(const AnnotationProperties&)`).
- Test: `tests/core/AnnotationGeoJsonImportTest.cpp` (new; add to `slideio-viewer-core-tests` in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `annotationsToGeoJson`, `GeoJsonExportContext` (existing, for round-trip tests); `core::parseIso8601Utc`, `core::isUnclassifiedCategory`, `kMinPolygonVertices`, `kMinPolylineVertices`.
- Produces (in `AnnotationGeoJson.h`):

```cpp
struct GeoJsonImportProvenance
{
    bool present = false;    // the collection carried a "slideio" object
    std::string slide;       // as written; may be empty
    std::string slideId;     // may be empty
    int scene = -1;          // -1 when absent
};

struct GeoJsonImportResult
{
    std::vector<Annotation> annotations; // file order; empty id = mint one
    int featuresRead = 0;                // top-level features
    int featuresImported = 0;            // features that produced >= 1 annotation
    std::map<std::string, int> skippedObjectTypes; // e.g. {"detection", 112}
    int withHoles = 0;
    int unsupportedGeometry = 0;         // GeometryCollection, null, unknown type
    int invalidGeometry = 0;
    int nestedObjects = 0;               // childObjects, counted recursively
    GeoJsonImportProvenance provenance;
    std::string error;                   // non-empty: file unreadable, everything else empty
};

[[nodiscard]] GeoJsonImportResult geoJsonToAnnotations(
    std::string_view text, std::chrono::system_clock::time_point importedAt);
```

- [ ] **Step 1: Write the failing tests**, tag `[core][GeoJsonImport]`. Parse inputs from raw string literals.

```cpp
TEST_CASE("every type round-trips through export and import", ...)
// rectangle, inverted rectangle, ellipse, closed polygon, open polyline, point, arrow,
// each with label, classification "Tumor", colour {1,2,3,200}, lineWidth 3.5, fillOpacity 0.5,
// notes "n\n\"q\"", author "a", createdAt/modifiedAt at whole seconds.
// After export+import: same count, same ids, typeOf equal, outline(g).points equal (exact
// for all but the ellipse; the ellipse's EllipseGeometry box equals the original normalised box),
// arrow tail/head equal, every property and metadata field equal.
// A degenerate stored polygon {(0,0),(0,0),(5,5)} also round-trips with 3 vertices.

TEST_CASE("the top level may be a collection, a feature or an array", ...)
// each yields 1 annotation; "[]" yields 0 and no error

TEST_CASE("an unreadable file is an error and returns nothing", ...)
// "", "not json", "{\"type\":\"Point\",\"coordinates\":[1,2]}", "42", truncated "{\"type\":",
// a feature whose name holds bytes "\xC3\x28" -> error non-empty, annotations empty, no throw

TEST_CASE("a byte-order mark is skipped", ...)            // "\xEF\xBB\xBF" + collection -> 1 annotation

TEST_CASE("a QuPath file maps by geometry", ...)
// no slideio blocks: Polygon 5-ring axis-aligned -> Rectangle; Polygon triangle -> closed
// PolygonGeometry with 3 vertices; LineString 3 -> open polyline; Point -> PointGeometry;
// name -> label; classification.name -> classification; color [10,20,30] -> {10,20,30,255}

TEST_CASE("multi-part geometries become one annotation per part", ...)
// MultiPoint of 5 with id "m" -> 5 PointGeometry, ids "m","","","","" , all classification "Tumor";
// MultiPolygon of 2 -> 2 closed polygons; MultiLineString of 2 -> 2 open polylines;
// featuresImported counts each feature once

TEST_CASE("holes, collections and null geometry are skipped under their reasons", ...)
// Polygon with interior ring -> withHoles 1; MultiPolygon with one holed part -> withHoles 1 and
// no annotation from it; GeometryCollection and "geometry": null -> unsupportedGeometry 2

TEST_CASE("only annotations are imported", ...)
// objectType detection x2, cell, tile, "weird" -> skippedObjectTypes {detection:2,cell:1,tile:1,weird:1};
// missing objectType and object_type "annotation" -> imported

TEST_CASE("nested child objects are counted and skipped", ...)
// an annotation with childObjects [annotation with childObjects [detection]] -> 1 annotation,
// nestedObjects 2

TEST_CASE("invalid geometry is counted, never thrown", ...)
// coordinate "1", null, true, [[1]] as a position, a 1-number position, NaN-producing 1e999,
// ring [[0,0],[5,5],[0,0]] (2 after the closing repeat), LineString of 1 position,
// a MultiPoint with one bad position (whole feature skipped) -> each invalidGeometry, no annotations

TEST_CASE("a z coordinate is ignored", ...)               // Point [1,2,3] -> (1,2)

TEST_CASE("an unclosed ring is accepted as it is", ...)   // [[0,0],[4,0],[4,4]] -> 3 vertices

TEST_CASE("a slideio type that disagrees with the geometry is ignored", ...)
// slideio.type "arrow" on a 3-position LineString -> open polyline;
// "rectangle" on a triangle -> polygon; "ellipse" on a Point -> point

TEST_CASE("an id written as a number is kept as its text", ...)  // "id": 17 -> "17"

TEST_CASE("classification names and the unclassified rule", ...)
// names ["Tumor","Positive"] -> "Tumor: Positive"; "UNCLASSIFIED" and "  " -> "";
// "Νέκρωση" and "Опухоль" byte-identical; name given as a number -> label ""

TEST_CASE("colour precedence", ...)
// slideio.color "#01020304" beats color; color beats colorRGB; colorRGB 0x0A141E -> {10,20,30,255};
// classification.color used when nothing else; none -> default {0xE6,0x7E,0x22,0xFF};
// slideio.color "#zz" falls through to color

TEST_CASE("out-of-range style falls back to the defaults", ...)
// slideio.lineWidth 5000, fillOpacity 2 -> 2.0f and 0.3f

TEST_CASE("missing metadata takes the import instant and no author", ...)
// createdAt == modifiedAt == importedAt (whole seconds), author ""; a malformed createdAt
// string also falls back

TEST_CASE("provenance is returned when the collection carries it", ...)
// slideio {slide, slideId "abc", scene 1} -> present, fields equal; absent -> present false, scene -1

TEST_CASE("every feature is accounted for", ...)
// a mixed collection of 12 features (2 multi, 3 detections, 1 hole, 1 null, 2 invalid, 3 plain)
// -> featuresRead 12 == featuresImported + sum(skippedObjectTypes) + withHoles
//    + unsupportedGeometry + invalidGeometry

TEST_CASE("a large detection export is skipped whole", ...)
// 20 000 detection Points -> skippedObjectTypes["detection"] == 20000, annotations empty
```

- [ ] **Step 2: Run to verify they fail.** Build `slideio-viewer-core-tests`, run `build/build/tests/Release/slideio-viewer-core-tests.exe "[GeoJsonImport]"`. Expected: compile error, `geoJsonToAnnotations` not declared.

- [ ] **Step 3: Implement `geoJsonToAnnotations`.** Parse with `nlohmann::json::parse(text, nullptr, /*allow_exceptions*/ false)` after stripping a BOM; a discarded value is the error "the file is not valid JSON". Wrap the walk in `try`/`catch (const nlohmann::json::exception&)` so a type surprise anywhere becomes an error rather than an escape — but read every field with type checks first (`is_number()`, `is_string()`, `is_array()`), so the catch is a backstop, not the mechanism. One function per geometry type returning `std::vector<AnnotationGeometry>` or a skip reason. Rectangle detection: after dropping a closing repeat, exactly 4 positions, every edge has equal x or equal y, two distinct x and two distinct y. Ellipse restoration: `EllipseGeometry` from the ring's bounding box. Properties per spec §3, with `parseAnnotationColor`, `isRepresentableStyle` (check a candidate properties struct; on false reset lineWidth/fillOpacity to `AnnotationProperties{}` defaults) and `parseIso8601Utc`. Default timestamps: `importedAt` truncated to seconds (the file format's precision). Build with the four-argument `Annotation` constructor.

- [ ] **Step 4: Run to verify they pass.** Same command, no filter. Expected: all core tests pass, the existing `[GeoJson]` and serializer tests included.

- [ ] **Step 5: Commit** — `git add src/core tests/core/AnnotationGeoJsonImportTest.cpp tests/CMakeLists.txt && git commit -m "Read annotations from GeoJSON"`

### Task 2: Bulk model operations and the import command

**Files:**
- Modify: `src/app/include/slideio/viewer/app/AnnotationModel.h`, `src/app/src/AnnotationModel.cpp`
- Create: `src/app/include/slideio/viewer/app/AnnotationImportCommand.h`, `src/app/src/AnnotationImportCommand.cpp` (add to `src/app/CMakeLists.txt`)
- Test: `tests/app/AnnotationModelTest.cpp`, `tests/app/AnnotationImportCommandTest.cpp` (new; add to app-tests in `tests/CMakeLists.txt`), `tests/app/AnnotationPersistenceServiceTest.cpp`

**Interfaces:**
- Produces (`AnnotationModel`):
  - `std::size_t insertMany(std::vector<core::Annotation> annotations);` — appends in order; each with an empty id or an id already present (in the model or earlier in the vector) is dropped silently, as `insert()`; emits `annotationsBulkChanged()` once if any was stored; returns the count stored.
  - `std::size_t removeMany(const std::vector<std::string>& ids);` — unknown ids skipped; clears the selection if it named a removed id (emitting `selectionChanged` as `remove()` does); emits `annotationsBulkChanged()` once if any was removed; returns the count.
- Produces (`AnnotationImportCommand.h`, namespace `slideio::viewer::app`):

```cpp
class AnnotationImportCommand : public ICommand
{
public:
    /// Null when `added` is empty.
    [[nodiscard]] static std::unique_ptr<AnnotationImportCommand> create(
        AnnotationModel& model, std::vector<core::Annotation> added, std::string description);
    void execute() override; // insertMany(added) -- appended at the end
    void undo() override;    // removeMany(ids of added)
    [[nodiscard]] std::string description() const override;
};
```

- [ ] **Step 1: Write the failing tests.**

```cpp
// AnnotationModelTest.cpp
TEST_CASE("insertMany appends in order and signals once", ...)
// model with "x"; insertMany {a,b,c} -> returns 3, order x,a,b,c, bulk signal count 1,
// annotationAdded count 0
TEST_CASE("insertMany drops empty and repeated ids", ...)
// model with "x"; insertMany {"", "x", "a", "a"} -> returns 1, model x,a
TEST_CASE("insertMany of nothing storable emits nothing", ...)   // {""} -> 0, no signal
TEST_CASE("removeMany removes the named ids and signals once", ...)
// x,a,b; select "a"; removeMany {"a","b","zz"} -> 2, model x, selection empty, one bulk signal

// AnnotationImportCommandTest.cpp
TEST_CASE("an import command with nothing to add is null", ...)
TEST_CASE("undoing an import removes exactly the imported annotations", ...)
// model x; insertMany {a,b}; push create(model,{a,b},"Import 2 annotations"); undo -> model x
TEST_CASE("redoing an import puts the annotations back at the end", ...)
// after undo, redo -> x,a,b with a,b equal to the originals (metadata included)
TEST_CASE("an import undone after a later edit leaves that edit's undo first", ...)
// import {a,b}; then deleteWithUndo "x"; undo -> x back, a,b still there; undo -> only x;
// model never holds duplicate ids

// AnnotationPersistenceServiceTest.cpp
TEST_CASE("a bulk insert marks the open document dirty", ...)
// active service on a NotFound scene; insertMany {a} -> isDirty() true
```

- [ ] **Step 2: Run to verify they fail.** `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release -R app --output-on-failure`. Expected: compile errors for `insertMany`, `removeMany`, `AnnotationImportCommand`.

- [ ] **Step 3: Implement** the two model methods and the command, modelled on `restoreMany` and `AnnotationRetagCommand`. Doc comments say why one signal (list-panel rebuild per annotation) and why the command exists beside the other two (spec §3 decisions).

- [ ] **Step 4: Run to verify they pass.** `ctest --test-dir build/build -C Release --output-on-failure`. Expected: all four suites pass.

- [ ] **Step 5: Commit** — `git commit -m "Add bulk insert and remove, and an undoable import command"`

### Task 3: Import planning and the summary text

**Files:**
- Create: `src/app/include/slideio/viewer/app/AnnotationImport.h`, `src/app/src/AnnotationImport.cpp` (add to `src/app/CMakeLists.txt`)
- Create: `src/ui/include/slideio/viewer/ui/AnnotationGeoJsonImport.h`, `src/ui/src/AnnotationGeoJsonImport.cpp`
- Test: `tests/app/AnnotationImportTest.cpp`, `tests/ui/AnnotationGeoJsonImportTest.cpp` (add both in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `core::GeoJsonImportResult` (Task 1), `AnnotationModel` (Task 2).
- Produces (app):

```cpp
struct ImportPlan
{
    std::vector<core::Annotation> toAdd; // every id non-empty and new to the model
    int alreadyPresent = 0;              // in the model, or repeated in the file
};
/// Mints a QUuid (WithoutBraces) for every empty id; drops ids already in `model`
/// or seen earlier in `imported`.
[[nodiscard]] ImportPlan planImport(std::vector<core::Annotation> imported, const AnnotationModel& model);
```

- Produces (ui, namespace `slideio::viewer::ui`):

```cpp
/// Annotations whose bounding box lies entirely outside [0,width] x [0,height].
/// Touching an edge is inside; overhang is inside.
[[nodiscard]] int countOutsideScene(const std::vector<core::Annotation>& annotations, double width, double height);

struct GeoJsonImportSummaryInput
{
    QString fileName;              // shown in the first line
    const core::GeoJsonImportResult* result = nullptr;
    int toAdd = 0;
    int alreadyPresent = 0;
    int outsideScene = 0;
    std::string openSlideId;
    int openScene = 0;
};
/// The whole dialog text, spec §3 layout; zero-count lines omitted.
[[nodiscard]] QString geoJsonImportSummary(const GeoJsonImportSummaryInput& input);
```

- [ ] **Step 1: Write the failing tests.**

```cpp
// app
TEST_CASE("planImport mints ids for empty ones", ...)       // {"",""} -> 2, both non-empty, distinct
TEST_CASE("planImport skips ids already in the scene", ...) // model {a}; {a,b} -> toAdd {b}, alreadyPresent 1
TEST_CASE("planImport skips an id repeated in the file", ...) // {b,b} -> toAdd {b}, alreadyPresent 1
TEST_CASE("re-importing an export adds nothing", ...)
// model with 3 annotations; annotationsToGeoJson -> geoJsonToAnnotations -> planImport:
// toAdd empty, alreadyPresent 3

// ui
TEST_CASE("countOutsideScene counts only shapes wholly outside", ...)
// 100x100 scene: point (50,50) in; point (150,50) out; rect (-10,-10)-(5,5) overhang in;
// rect (100,0)-(120,10) touching the edge in; point (-1,-1) out -> 2
TEST_CASE("the summary names the file and the count to add", ...)
// contains "x.geojson" and "27 annotations will be added"
TEST_CASE("zero-count lines are omitted", ...)       // nothing but toAdd -> no "skipped", no "⚠"
TEST_CASE("every skip reason has its line", ...)
// alreadyPresent 3, detection 112, withHoles 1, unsupported 1, invalid 2, nested 4 ->
// each count appears with its reason
TEST_CASE("a different scene or slide is warned about", ...)
// provenance scene 1, open scene 0 -> warning names scene 1 and the provenance slide's file name;
// provenance slideId "abc", open "def" -> warning; same id and scene -> no warning;
// provenance absent -> no warning; provenance slideId empty -> no slide warning
TEST_CASE("shapes outside the scene are warned about", ...)  // outsideScene 4 -> "4 of the annotations"
```

- [ ] **Step 2: Run to verify they fail** — build, `ctest ... -R "app|ui"`. Expected: compile errors for the new headers.

- [ ] **Step 3: Implement.** Summary wording per spec §3's mock-up; build with `QCoreApplication::translate("GeoJsonImport", ...)` like other ui text and `%n` plural forms. The provenance slide is shown by its file name (`QFileInfo(...).fileName()`), not its full path.

- [ ] **Step 4: Run to verify they pass** — full `ctest`. Expected: all four suites pass.

- [ ] **Step 5: Commit** — `git commit -m "Plan a GeoJSON import and describe it before it happens"`

### Task 4: The menu action

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `CLAUDE.md` (a "GeoJSON import" bullet after the export's; move "annotation import" out of "Not built yet" for GeoJSON, keeping ASAP XML there)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Add `importAnnotationsAction`** — text `"&Import Annotations from GeoJSON..."`, a status tip, created disabled; added to the File menu **directly above** `exportCountsAction`; enabled/disabled in `applyAnnotationAvailability` beside the six tool actions (and nowhere else — a comment says so, as the export's says the opposite).

- [ ] **Step 2: Add `void importAnnotations();`**, connected to the action:
  1. Return unless a slide is open and `annotationPersistence->isActive()`.
  2. `QFileDialog::getOpenFileName` in the workspace with the filter above; cancel returns silently.
  3. Read the file with `QFile::readAll`; a failure warns "The annotations could not be read from:\n\n<native path>\n\n<reason>".
  4. `geoJsonToAnnotations(bytes, now)`; a non-empty `error` warns the same way with the error.
  5. `planImport`, `countOutsideScene(plan.toAdd, info.width, info.height)` from `viewportWidget->slideInfo()`, `geoJsonImportSummary`.
  6. `plan.toAdd` empty → `QMessageBox::information` with the summary, return. Else `QMessageBox` with *Import* (accept role) and *Cancel*, default Cancel; cancel returns.
  7. Re-check `annotationPersistence->isActive()` (the dialog was modal but the defence is cheap); `annotations->clearSelection()`; `annotations->insertMany(plan.toAdd)`; push `AnnotationImportCommand::create(*annotations, std::move(plan.toAdd), tr("Import %n annotation(s)", "", count))`; status message `tr("Imported %n annotation(s) from %1", "", count)` with the file name.

- [ ] **Step 3: Build, run all four suites, install.** Kill any running viewer; `cmake --install build/build --config Release`; confirm the installed exe's timestamp equals the build tree's and that it contains `"Import Annotations from"`.

- [ ] **Step 4: Smoke test by hand**: export Philips-1 scene 0, import the file back — the summary reports all already present, nothing changes.

- [ ] **Step 5: Commit** — `git commit -m "Add File > Import Annotations from GeoJSON"`

### Task 5: Manual checks — group AG

**Files:**
- Modify: `docs/superpowers/specs/2026-10-22-annotation-geojson-import-design.md` §6 (Result column and a run record)
- Modify: `CLAUDE.md` (the Task 4 bullet: checks run, result)

- [ ] **Step 1: Run AG1–AG12.** Fixtures: Philips-1 scene 0 (back up its annotation file first and restore it after), the three-scene SCN, `CMU-1-Small-Region.svs`, QuPath at `%LOCALAPPDATA%\QuPath-0.7.0`. For AG6, produce the QuPath file with a console Groovy script (`QuPath-0.7.0 (console).exe script --image=<slide>`) that creates the five ROIs and a 5-point Points annotation and calls `PathIO.exportObjectsAsGeoJSON` (or `exportObjectsToGeoJson`), since the GUI is not scriptable here. For AG7, add two detections in the same script. Compare imported annotation files on disk with a JSON parser, not by eye.

- [ ] **Step 2: Record and commit** — `git commit -m "Record the group AG run"`
