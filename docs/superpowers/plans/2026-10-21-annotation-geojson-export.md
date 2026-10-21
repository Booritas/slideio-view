# GeoJSON Export of Annotations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `File > Export Annotations as GeoJSON...` writes the current scene's annotations as one GeoJSON FeatureCollection that QuPath 0.7 imports.

**Architecture:** A pure writer in core (`annotationsToGeoJson`) turns `std::vector<core::Annotation>` plus a provenance context into the whole file text, using `nlohmann::ordered_json` (already linked PRIVATE to core). The ui adds one action in `MainWindow` modelled line for line on `exportCounts`, and a shared file-stem helper beside `countExportFileStem`.

**Tech Stack:** C++17, nlohmann_json (core, PRIVATE), Qt 6 Widgets (ui only), Catch2 v3.

**Spec:** `docs/superpowers/specs/2026-10-21-annotation-geojson-export-design.md`

## Global Constraints

- Core stays Qt-free; nlohmann appears in no core public header.
- Output: UTF-8, **no BOM**, compact JSON (`dump()` with no indent), one trailing `\n`.
- Coordinates: level-0 pixels of the scene exactly as stored — no transform, no clamping.
- Every shape is written from `core::outline()`; one feature per annotation, in model order, never filtered.
- Rings closed (first position repeated); winding as drawn; self-intersecting polygons unchanged.
- QuPath property names, verified against qupath-core-0.7.0: `objectType` (always `"annotation"`), `name`, `color` (`[r,g,b]`), `classification` (`{"name","color"}`).
- Unclassified = `core::isUnclassifiedCategory(classification)` → `classification` omitted.
- Collection foreign member `slideio`: `format` = `"slideio-viewer annotation export"`, `formatVersion` = `1`, `slide`, `slideId`, `scene`, `exportedAt`, `coordinates` = `"level-0 pixels of the scene; origin top-left, x right, y down"`, `pixelSizeMicrons` only when both sizes are > 0.
- Action gating: enabled exactly where `exportCountsAction` is; never in `applyAnnotationAvailability`.
- Suggested file name `<slide file name>.s<N>.geojson`; dialog filter `GeoJSON (*.geojson);;JSON (*.json);;All files (*)`.
- Never run a Qt-linked test exe directly — only through `ctest` (`core-tests` is the exception).

## Review Focus

1. **Invalid UTF-8 in a label, note or classification** (files are hand-editable): nlohmann's `dump()` throws `type_error.316` by default. Expected: the export still succeeds with the bad bytes replaced by U+FFFD, not a crash or a silent abort. Pinned in Task 2.
2. **JSON-special characters in text** — quotes, backslash, a newline in notes, a tab in a label: escaped so the file parses and reads back byte-identical. Pinned in Task 2.
3. **Annotations outside the slide bounds or at negative coordinates** (drawing past the edge is allowed): written verbatim, not clamped. Pinned in Task 1.
4. **A Windows slide path with backslashes and non-ASCII characters** in provenance: escaped, round-trips exactly. Pinned in Task 2.
5. **A large scene** (2000 ellipses ≈ 1M positions): completes and yields 2000 features; no per-feature quadratic work. Pinned in Task 1 (count only, no timing assertion).

---

### Task 1: Core writer — collection, geometry, ids, failure

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationGeoJson.h`
- Create: `src/core/src/AnnotationGeoJson.cpp`
- Modify: `src/core/CMakeLists.txt` (add `src/AnnotationGeoJson.cpp` after `src/AnnotationSerialization.cpp`)
- Modify: `src/core/include/slideio/viewer/core/AnnotationSerialization.h`, `src/core/src/AnnotationSerialization.cpp` (expose two helpers, below)
- Test: `tests/core/AnnotationGeoJsonTest.cpp`; add `core/AnnotationGeoJsonTest.cpp` to `slideio-viewer-core-tests` in `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::outline(const AnnotationGeometry&) -> Outline`, `core::Annotation`, `core::formatIso8601Utc`.
- Produces (in `AnnotationGeoJson.h`, namespace `slideio::viewer::core`):

```cpp
struct GeoJsonExportContext
{
    std::string slidePath;          // as opened, verbatim
    std::string slideId;            // may be empty
    int sceneIndex = 0;
    double pixelSizeMicronsX = 0.0; // 0 or less = unknown
    double pixelSizeMicronsY = 0.0;
};

struct GeoJsonExportResult
{
    std::string text;  // the whole file; empty on failure
    std::string error; // empty on success; on failure names the annotation id (or "export time")
};

[[nodiscard]] GeoJsonExportResult annotationsToGeoJson(
    const std::vector<Annotation>& annotations, const GeoJsonExportContext& context,
    std::chrono::system_clock::time_point exportedAt);
```

- Produces (in `AnnotationSerialization.h`, moved out of the anonymous namespace, bodies unchanged):
  `[[nodiscard]] const char* annotationTypeName(const AnnotationGeometry& geometry);` (today's `geometryTypeName`; keep its `static_assert`, update its one internal caller) and
  `[[nodiscard]] std::string formatAnnotationColor(const Color& color);` (today's `formatColor`, `#RRGGBBAA`).

Task 1 writes features with `type`, `id`, `geometry` and a `properties` object holding only `objectType: "annotation"`; Task 2 fills the rest.

- [ ] **Step 1: Write the failing tests** in `tests/core/AnnotationGeoJsonTest.cpp`, tag `[core][GeoJson]`. Parse `result.text` with `nlohmann::json::parse` in the test (core-tests links core; add `nlohmann_json::nlohmann_json` to the core-tests `target_link_libraries` if the header is not reachable). Helpers in the test file build annotations with fixed ids `"a1"`, `"a2"`…

```cpp
TEST_CASE("each shape maps to its GeoJSON geometry", "[core][GeoJson]")
// rectangle (10,20)-(110,70) -> "Polygon", ring size 5, ring[0]==ring[4]==[10,20]
// ellipse                    -> "Polygon", ring size kEllipseOutlineVertices + 1 (513)
// closed polygon 3 vertices  -> "Polygon", ring size 4
// open polyline 3 vertices   -> "LineString", size 3
// point (5,6)                -> "Point", coordinates == [5,6]
// arrow tail (1,2) head (9,8)-> "LineString", coordinates == [[1,2],[9,8]]

TEST_CASE("an inverted rectangle is written normalised, as outline() paints it", ...)
// topLeft (110,70) bottomRight (10,20) -> ring[0] == [10,20]

TEST_CASE("every closed ring ends where it starts", ...)          // rectangle, ellipse, polygon

TEST_CASE("a consecutive duplicate vertex is dropped, as outline() drops it", ...)
// closed {(0,0),(10,0),(10,0),(10,10)} -> ring size 4: (0,0),(10,0),(10,10),(0,0)

TEST_CASE("a polygon outline() reduces below three points is written from its stored vertices", ...)
// closed {(0,0),(0,0),(5,5)} -> ring == [[0,0],[0,0],[5,5],[0,0]] (4 positions)
TEST_CASE("an open line outline() reduces below two points is written from its stored vertices", ...)
// open {(3,3),(3,3)} -> LineString [[3,3],[3,3]]

TEST_CASE("a self-intersecting polygon is written unchanged", ...)
// bow-tie {(0,0),(10,10),(10,0),(0,10)} -> ring == those four + (0,0)

TEST_CASE("coordinates outside the slide are written verbatim", ...)
// point (-50.5, 1e6) -> [-50.5, 1000000.0]

TEST_CASE("one feature per annotation, ids and order kept", ...)
// three annotations a1,a2,a3 -> features.size()==3, ids "a1","a2","a3"; every feature
// type=="Feature" and properties.objectType=="annotation"

TEST_CASE("an empty scene is a valid empty collection", ...)
// type=="FeatureCollection", features is an empty array; text ends with exactly one '\n'

TEST_CASE("a non-finite coordinate fails the export and names the annotation", ...)
// a2 has point (NaN, 0) -> result.text.empty(), result.error contains "a2"

TEST_CASE("a large scene is written whole", ...)
// 2000 ellipses -> features.size()==2000
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then `build/build/tests/Release/slideio-viewer-core-tests.exe "[GeoJson]"` (core-tests is safe to run directly; locate the exe under `build/build` if the path differs).
Expected: build fails on the missing header `AnnotationGeoJson.h`.

- [ ] **Step 3: Implement `annotationsToGeoJson` geometry half in `src/core/src/AnnotationGeoJson.cpp`**

`using json = nlohmann::ordered_json;`. Collection keys in order `type`, `slideio` (Task 2 fills it; write it as an empty object now), `features`. Feature keys in order `type`, `id`, `geometry`, `properties`. Geometry type is chosen by a `std::visit` over the variant with one overload per alternative and **no generic fallback**, so a new shape is a compile error here as at the other dispatch sites. Positions come from `outline(geometry).points`; for a closed outline append `points.front()`; apply the stored-vertices fallback from the tests when a closed outline has fewer than 3 points or an open one fewer than 2. Before building a feature, reject any non-finite position (`std::isfinite` on x and y) — return `{ "", "annotation <id> has a coordinate that is not a finite number" }`. Return `text = j.dump() + "\n"`.

- [ ] **Step 4: Move the two helpers** out of the anonymous namespace in `AnnotationSerialization.cpp` under the new names, declare them in `AnnotationSerialization.h`, and update the serializer's own calls. No behaviour change: `AnnotationSerializationTest.cpp` must stay green untouched.

- [ ] **Step 5: Run to verify they pass**

Run: the core-tests exe from Step 2 with no filter.
Expected: all test cases pass, including every existing serializer test.

- [ ] **Step 6: Commit**

```bash
git add src/core tests/core/AnnotationGeoJsonTest.cpp tests/CMakeLists.txt
git commit -m "Write a scene's annotations as GeoJSON geometry (FR-ANN-34)"
```

---

### Task 2: Core writer — properties and provenance

**Files:**
- Modify: `src/core/src/AnnotationGeoJson.cpp`
- Test: `tests/core/AnnotationGeoJsonTest.cpp`

**Interfaces:**
- Consumes: Task 1's `annotationsToGeoJson`, `GeoJsonExportContext`, `annotationTypeName`, `formatAnnotationColor`; `core::isUnclassifiedCategory(const std::string&)`; `core::formatIso8601Utc`.
- Produces: no new names; the full property set below.

Per feature, `properties` keys in order: `objectType`, `name` (only if the label is non-empty), `color` (`[r,g,b]`), `classification` (only if not `isUnclassifiedCategory`; `{"name": <as stored>, "color": [r,g,b]}`), `slideio` (`type`, `notes`, `author`, `createdAt`, `modifiedAt`, `color`, `lineWidth`, `fillOpacity` — all always present). Collection `slideio` keys in the order listed in Global Constraints; `pixelSizeMicrons` = `[x, y]` only when both > 0.

- [ ] **Step 1: Write the failing tests** (same file, same tag)

```cpp
TEST_CASE("unclassified annotations carry no classification", ...)
// classification "" , "   ", "unclassified", "UNCLASSIFIED" -> !properties.contains("classification")
TEST_CASE("a classification is written as stored, with the annotation's colour", ...)
// "Tumor, invasive", color {0xE6,0x7E,0x22,0xFF} -> classification == {"name":"Tumor, invasive","color":[230,126,34]}
TEST_CASE("every feature carries its own colour", ...)
// classified and unclassified both -> properties.color == [r,g,b]
TEST_CASE("an empty label is omitted and a label is the name", ...)
TEST_CASE("the slideio block carries every field, empty ones included", ...)
// type "arrow" for an arrow, "polyline" for an open line; notes "" present; author;
// createdAt/modifiedAt == formatIso8601Utc of the metadata; color "#E67E22FF";
// lineWidth 2.0; fillOpacity approx 0.3
TEST_CASE("the collection records where it came from", ...)
// slide, slideId, scene, exportedAt == formatIso8601Utc(fixed time), format, formatVersion 1,
// coordinates string exact
TEST_CASE("pixel size is written only when known", ...)
// (0.25,0.25) -> pixelSizeMicrons == [0.25,0.25]; (0,0) and (0.25,0) -> key absent
TEST_CASE("Greek and Cyrillic text is written as UTF-8, unescaped", ...)
// classification "Νέκρωση", label "Опухоль" -> raw text contains those UTF-8 bytes; parse round-trips
TEST_CASE("JSON-special characters round-trip", ...)                     // Review Focus 2
// label "say \"hi\"\t\\x", notes "line1\nline2" -> parse back byte-identical
TEST_CASE("invalid UTF-8 does not abort the export", ...)                // Review Focus 1
// notes "ok\xFF\xFEend" -> result.error.empty(), parsed notes == "ok\uFFFD\uFFFDend" (U+FFFD per bad byte)
TEST_CASE("a Windows path with non-ASCII characters round-trips", ...)  // Review Focus 4
// slidePath "D:\\слайды\\Philips-1.tiff" -> parsed slideio.slide equals it exactly
TEST_CASE("an unrepresentable timestamp fails the export", ...)
// metadata.createdAt before 1970 -> text empty, error names the annotation id
```

- [ ] **Step 2: Run to verify they fail**

Run: the core-tests exe with `"[GeoJson]"`.
Expected: the new cases FAIL on missing keys (Task 1 writes only `objectType`); the invalid-UTF-8 case fails by an exception escaping `dump()`.

- [ ] **Step 3: Implement the properties and provenance** in `AnnotationGeoJson.cpp`. Dump with `j.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace)` so invalid UTF-8 becomes U+FFFD and non-ASCII stays unescaped. A timestamp `formatIso8601Utc` returns empty for fails the export with an error naming the annotation (or `"export time"` for `exportedAt`).

- [ ] **Step 4: Run to verify they pass**

Run: the core-tests exe with no filter.
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/src/AnnotationGeoJson.cpp tests/core/AnnotationGeoJsonTest.cpp
git commit -m "Carry classification, colour and provenance into the GeoJSON export"
```

---

### Task 3: Shared scene file stem

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationCountExport.h`, `src/ui/src/AnnotationCountExport.cpp`
- Test: `tests/ui/AnnotationCountExportTest.cpp`

**Interfaces:**
- Produces: `[[nodiscard]] std::string sceneExportFileStem(const std::string& slideFileName, int sceneIndex);` returning `<sanitised name>.s<N>` — today's `countExportFileStem` body without the `".counts"` suffix. `countExportFileStem` becomes `sceneExportFileStem(...) + ".counts"`, its doc unchanged in substance.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("sceneExportFileStem names the scene without a purpose suffix", "[ui][AnnotationCountExport]")
// ("Philips-1.tiff", 0) == "Philips-1.tiff.s0"; ("Philips-1.tiff", 1) == "Philips-1.tiff.s1"
TEST_CASE("sceneExportFileStem replaces what a file system refuses", ...)
// ("a:b*c?d\"e<f>g|h", 1) == "a_b_c_d_e_f_g_h.s1"
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build/build --config Release --target slideio-viewer-ui-tests` then `ctest --test-dir build/build -C Release -R "^ui-tests$" --output-on-failure`.
Expected: build error, `sceneExportFileStem` not declared.

- [ ] **Step 3: Implement** the split described in Interfaces.

- [ ] **Step 4: Run to verify they pass** — same ctest command; expected `100% tests passed`, the existing `countExportFileStem` cases included.

- [ ] **Step 5: Commit**

```bash
git add src/ui tests/ui/AnnotationCountExportTest.cpp
git commit -m "Share the scene file stem between the two exports"
```

---

### Task 4: The menu action

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `CLAUDE.md` (Key Design Decisions, shipped list)

**Interfaces:**
- Consumes: `core::annotationsToGeoJson`, `core::GeoJsonExportContext`, `core::GeoJsonExportResult` (Task 1); `ui::sceneExportFileStem` (Task 3).
- Produces: `QAction* exportAnnotationsAction` and `void exportAnnotations();` on `MainWindow::Impl`.

No suite can reach this code (no test creates a `QApplication`); verification is the build plus group AF in Task 5.

- [ ] **Step 1: Add the action**, mirroring `exportCountsAction` at each of its sites in `MainWindow.cpp` — find them with `grep -n exportCountsAction src/ui/src/MainWindow.cpp`, not by line number: the member declaration (`QAction* exportCountsAction = nullptr;`); creation (`new QAction("&Export Counts..."`) with text `"Export Annotations as &GeoJSON..."` (G is free in the File menu; A is taken by Open Annotations Folder) and status tip `"Write this scene's annotations to a GeoJSON file QuPath can import"`, created disabled; `fileMenu->addAction(exportCountsAction)` — add the new one directly after it; the `QObject::connect(exportCountsAction, &QAction::triggered` connection; and **every** `exportCountsAction->setEnabled(` toggle, each given a twin with the same value. Re-run the grep afterwards: every `exportCountsAction->setEnabled` hit must have an `exportAnnotationsAction->setEnabled` beside it.

- [ ] **Step 2: Implement `exportAnnotations()`** directly after `void MainWindow::Impl::exportCounts()`, following it step for step: return if `!viewportWidget->isSlideOpen()`; context from `currentFilePath()`, `currentSlideId()`, `currentSceneIndex()`, and `slideInfo().resolutionX/Y * 1e6` for the micron sizes; call `annotationsToGeoJson(annotations->annotations(), context, std::chrono::system_clock::now())` **before** the dialog, and if `result.error` is non-empty show `QMessageBox::warning(owner, tr("Export Annotations"), tr("The annotations could not be exported.\n\n%1").arg(QString::fromStdString(result.error)))` and return; suggested name `sceneExportFileStem(fileName, scene) + ".geojson"` in `annotationSettings->workspaceDirectory()`; filter `tr("GeoJSON (*.geojson);;JSON (*.json);;All files (*)")`; the same `WriteOnly | Truncate`, byte count, `flush()`, `close()`, failure warning (`tr("The annotations could not be written to:\n\n%1\n\n%2")`) and status message `tr("Annotations exported to %1")` as `exportCounts`.

- [ ] **Step 3: Build and run all suites**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`.
Expected: build exit 0, `100% tests passed out of 4`. Then confirm the artifact, not the exit code: `grep -c "Export Annotations as" build/build/Release/slideio-viewer.exe` returns ≥ 1.

- [ ] **Step 4: Record it in `CLAUDE.md`** — one bullet under Key Design Decisions after the counts export bullet: the GeoJSON export, that it reads `outline()`, the verified QuPath names, unclassified omitted, everything else under `slideio`, gated like Export Counts, and that `annotationsToGeoJson`'s `std::visit` is one more exhaustive dispatch site a new shape must answer. Point at the spec.

- [ ] **Step 5: Commit**

```bash
git add src/ui/src/MainWindow.cpp CLAUDE.md
git commit -m "Add File > Export Annotations as GeoJSON"
```

---

### Task 5: Manual checks — group AF

**Files:**
- Modify: `docs/superpowers/specs/2026-10-21-annotation-geojson-export-design.md` §7 (fill the Result column and add a run record, as groups AD and AE did)
- Modify: `CLAUDE.md` (the bullet from Task 4: checks run, result)

**Interfaces:** none.

- [ ] **Step 1: Install and confirm the binary.** Kill any running viewer, `cmake --install build/build --config Release`, then confirm `build/install/release/bin/slideio-viewer.exe` has the same timestamp as `build/build/Release/slideio-viewer.exe` and contains `"Export Annotations as"`.

- [ ] **Step 2: Run AF1–AF15** from spec §7. Fixtures: Philips-1 scene 0 with the 27-annotation file restored from its backup, the three-scene `scn/ultivue/Leica Aperio Versa 5 channel fluorescent image.scn`, QuPath at `%LOCALAPPDATA%\QuPath-0.7.0`. Before AF6/AF7, draw one ellipse, one closed polygon and one freehand line on a scratch slide. Read every exported file back with a JSON parser, not only by eye; compare ids against the annotation file for AF3.

- [ ] **Step 3: Record and commit**

```bash
git add docs/superpowers/specs/2026-10-21-annotation-geojson-export-design.md CLAUDE.md
git commit -m "Record the group AF run"
```
