# Annotation Measurements CSV Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `File > Export Measurements...` writes one CSV per scene with one row per annotation: id, type, label, classification, bounding-box centre, and area / perimeter / length in pixels and, when calibrated, in microns.

**Architecture:** The counts export's CSV conventions (BOM, CRLF, RFC 4180 quoting, formula guard, provenance columns) move into a shared ui unit, `CsvFormat`. A pure formatter `annotationMeasurementCsv` beside it calls `core::measure()` twice per annotation (pixels, microns). `MainWindow` gains one action modelled on `exportCounts` and gated like the other two exports.

**Tech Stack:** C++17, Qt 6 Widgets (ui only), Catch2 v3.

**Spec:** `docs/superpowers/specs/2026-10-23-annotation-measurements-export-design.md`

## Global Constraints

- Header, verbatim: `id,type,label,classification,center_x_px,center_y_px,area_px2,perimeter_px,length_px,area_um2,perimeter_um,length_um,area_note,pixel_width_um,pixel_height_um,slide,scene,slide_id,exported_at`
- One row per annotation, model order, never filtered; an empty scene writes the header alone.
- UTF-8 with BOM, CRLF, RFC 4180 quoting and the formula guard — through the shared helpers, never a copy.
- **Numbers are written raw, never through the text-field helper** (the formula guard would put an apostrophe before a negative number).
- Numbers: `std::to_chars` fixed notation, shortest round-trip; no exponent, no thousands separator, no display rounding.
- `type` is `core::annotationTypeName`; classification ASCII-trimmed, empty when unclassified (blank or `core::isUnclassifiedCategory`).
- Calibrated = both pixel sizes finite and > 0. Uncalibrated: every `_um` column and both `pixel_*_um` empty.
- `perimeter_*` for closed shapes, `length_*` for open shapes, `area_*` for closed shapes when `measure()` gives one; `area_note` = `self-intersecting` exactly when `ShapeMetrics::selfIntersecting`; a point has every measurement column empty.
- Action `File > Export Measurements...` directly below `Export Counts...`; every `exportCountsAction->setEnabled` gets a twin; never in `applyAnnotationAvailability`; refuses via `annotationsReadyToExport`; dialog filter `CSV (*.csv);;All files (*)`; suggested `<stem>.s<N>.measurements.csv`.
- Never run a Qt-linked test exe directly — only through `ctest`.

## Review Focus

1. **An annotation at negative coordinates** (drawing past the edge is allowed): `center_x_px` is `-5`, not `'-5` — numbers bypass the formula guard. Pinned in Task 2.
2. **One axis calibrated, the other zero or NaN**: treated as uncalibrated — no half-micron row. Pinned in Task 2.
3. **A long closed trace** (3000 vertices): the self-intersection test runs once, not twice (`measureKnown` for the second call); the export completes. Pinned in Task 2 (completion and correctness only).
4. **A label holding a tab, newline or leading `=`**: guarded and quoted exactly as in the counts file. Pinned in Task 2.
5. **The counts CSV is byte-identical after the helpers move.** Pinned in Task 1 by its unchanged 19 tests plus a direct test of the shared helpers.

---

### Task 1: Shared CSV helpers

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/CsvFormat.h`, `src/ui/src/CsvFormat.cpp` (add the .cpp to `src/ui/CMakeLists.txt` after `src/AnnotationCountExport.cpp`)
- Modify: `src/ui/src/AnnotationCountExport.cpp` — delete its anonymous-namespace copies and call the shared ones
- Test: `tests/ui/CsvFormatTest.cpp` (add to ui-tests in `tests/CMakeLists.txt`)

**Interfaces:**
- Produces (namespace `slideio::viewer::ui`):

```cpp
inline constexpr const char* kCsvLineEnding = "\r\n";
inline constexpr const char* kCsvUtf8ByteOrderMark = "\xEF\xBB\xBF";
/// One text field: apostrophe before a formula-looking value, then RFC 4180 quoting.
[[nodiscard]] std::string csvField(const std::string& value);
/// The four provenance values, comma-joined, each already a field:
/// slide,scene,slide_id,exported_at -- the columns both CSV exports end with.
[[nodiscard]] std::string csvProvenance(const std::string& slidePath, int sceneIndex,
                                        const std::string& slideId, const std::string& exportedAtIso8601);
```

- [ ] **Step 1: Write the failing tests** (tag `[ui][CsvFormat]`): `csvField("a,b") == "\"a,b\""`; `csvField("say \"hi\"") == "\"say \"\"hi\"\"\""`; `csvField("=1+1") == "'=1+1"`; `csvField("-x,y") == "\"'-x,y\""`; `csvField("plain") == "plain"`; `csvProvenance("D:/a,b.svs", 2, "abc", "2026-10-23T09:00:00Z") == "\"D:/a,b.svs\",2,abc,2026-10-23T09:00:00Z"`.
- [ ] **Step 2: Run** — build, `ctest --test-dir build/build -C Release -R ui`. Expected: compile error, `CsvFormat.h` not found.
- [ ] **Step 3: Implement** — move `looksLikeFormula` (stays private to `CsvFormat.cpp`) and `field` (becomes `csvField`) with their doc comments; `categoryCountCsv` uses `csvField`, `csvProvenance` and the two constants.
- [ ] **Step 4: Run** all four suites. Expected: pass, the 19 `[AnnotationCountExport]` tests untouched.
- [ ] **Step 5: Commit** — `git commit -m "Share the counts export's CSV conventions"`

### Task 2: The measurements formatter

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationMeasurementExport.h`, `src/ui/src/AnnotationMeasurementExport.cpp` (CMake as Task 1)
- Test: `tests/ui/AnnotationMeasurementExportTest.cpp`

**Interfaces:**
- Consumes: Task 1; `core::measure`, `core::measureKnown`, `core::PixelSize`, `core::annotationTypeName`, `core::isUnclassifiedCategory`, `core::formatIso8601Utc`, `ui::sceneExportFileStem`.
- Produces:

```cpp
struct MeasurementExportContext
{
    std::string slidePath;
    std::string slideId;
    int sceneIndex = 0;
    /// Microns per pixel (SlideInfo::resolutionX/Y x 1e6). Calibrated only when
    /// both are finite and > 0.
    double pixelWidthMicrons = 0.0;
    double pixelHeightMicrons = 0.0;
};
struct MeasurementExportResult
{
    std::string text;  // the whole file; empty on failure
    std::string error; // empty on success; names the annotation on failure
};
[[nodiscard]] MeasurementExportResult annotationMeasurementCsv(
    const std::vector<core::Annotation>& annotations, const MeasurementExportContext& context,
    std::chrono::system_clock::time_point exportedAt);
/// sceneExportFileStem(...) + ".measurements"
[[nodiscard]] std::string measurementExportFileStem(const std::string& slideFileName, int sceneIndex);
```

Tests parse the output by splitting on `\r\n` and on unquoted commas (a small helper in the test file), then address cells by header name.

- [ ] **Step 1: Write the failing tests** (tag `[ui][AnnotationMeasurementExport]`):

```cpp
TEST_CASE("the header is fixed whatever the calibration", ...)
// calibrated (0.25, 0.25) and uncalibrated (0, 0): line 1 after the BOM == the Global Constraints header
TEST_CASE("one row per annotation, in model order", ...)        // ids a1,a2,a3 in order; {} -> header only
TEST_CASE("a rectangle is measured in pixels and microns", ...)
// (0,0)-(300,200) at 0.25 um: area_px2 60000, perimeter_px 1000, area_um2 3750, perimeter_um 250,
// length_* empty, area_note empty, center 150,100, pixel_width_um 0.25
TEST_CASE("non-square pixels scale each direction separately", ...)
// 0.25 x 0.5 um; polyline (0,0)-(100,0) length_um 25; polyline (0,0)-(0,100) length_um 50
TEST_CASE("an ellipse's area is measure()'s, not pi a b", ...)  // cell == to_chars of core::measure(...).area
TEST_CASE("a self-intersecting polygon has no area and says why", ...)
// bow-tie: area_px2 and area_um2 empty, area_note "self-intersecting", perimeter_px set
TEST_CASE("open shapes have a length and no area or perimeter", ...)  // polyline and arrow
TEST_CASE("a point has a position and no measurement", ...)     // center = its position; 7 measurement cells empty
TEST_CASE("an uncalibrated slide leaves every micron column empty", ...)
// pixel size (0,0), (0.25,0), (NaN,0.25), (inf,0.25): all _um and pixel_*_um empty; _px columns set
TEST_CASE("classification is trimmed and unclassified is empty", ...)  // " Tumor " -> Tumor; "UNCLASSIFIED", "  " -> empty
TEST_CASE("text fields are guarded and quoted as in the counts file", ...)
// label "=1+1" -> '=1+1; label "a,b" quoted; label "two\nlines" quoted, row count unchanged; "\tx" guarded
TEST_CASE("negative numbers are written as numbers", ...)       // point (-5,-7.5): center cells "-5" and "-7.5"
TEST_CASE("numbers have no exponent and round-trip", ...)
// rectangle 1e-3 x 1e-4 px at 1 um and a 1.5e5 x 1e5 px rectangle: no 'e'/'E' in any cell;
// std::stod(cell) == the measured double; a whole value has no '.'
TEST_CASE("non-ASCII text, BOM and CRLF", ...)                  // Greek label bytes intact; starts with BOM; every line ends \r\n
TEST_CASE("a long closed trace is measured", ...)               // 3000-vertex non-intersecting polygon: area present
TEST_CASE("a non-finite measurement fails the export and names the annotation", ...)
// polygon with a vertex at 1e308 and one at -1e308 (finite coords, overflowing area): text empty, error contains its id
TEST_CASE("measurementExportFileStem", ...)                     // ("Philips-1.tiff", 1) == "Philips-1.tiff.s1.measurements"
```

- [ ] **Step 2: Run** — build; ctest `-R ui`. Expected: compile error, header not found.
- [ ] **Step 3: Implement.** Per annotation: `outline`-based bounding box via `Annotation::boundingBox()` for the centre; `core::measure(geometry, {1,1})` for pixels; when calibrated, `core::measureKnown(geometry, {pw, ph}, pixels.selfIntersecting)` for microns (the self-intersection answer is scale-invariant). A number cell is `std::to_chars(..., std::chars_format::fixed)`; reject (failure result) any non-finite value about to be written. `exportedAt` formatted once.
- [ ] **Step 4: Run** all four suites. Expected: pass.
- [ ] **Step 5: Commit** — `git commit -m "Format a scene's annotation measurements as CSV"`

### Task 3: The menu action

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`; `CLAUDE.md` (a "measurements CSV" bullet beside the two export bullets; FR-ANN-34 complete; remove "the per-annotation measurements CSV" from "Not built yet")

**Interfaces:**
- Consumes: Task 2.

- [ ] **Step 1: Add `exportMeasurementsAction`** — text `"Export &Measurements..."` (check the mnemonic is free in the File menu; pick another letter if not), status tip, created disabled, added after `exportCountsAction`; a `setEnabled` twin beside every `exportCountsAction->setEnabled` (four sites).
- [ ] **Step 2: Add `void exportMeasurements();`** modelled on `exportCounts`: slide-open guard; `annotationsReadyToExport(tr("Export Measurements"))`; build the result first (context from `viewportWidget` and `slideInfo().resolutionX/Y * 1e6`); warn with `result.error` on failure; save dialog per Global Constraints; the same Truncate / byte-count / flush / close write; status message `tr("Measurements exported to %1")`.
- [ ] **Step 3: Build, run all suites, install;** confirm the installed exe's timestamp equals the build tree's and it contains `"Export &Measurements"`.
- [ ] **Step 4: Commit** — `git commit -m "Add File > Export Measurements"`

### Task 4: Manual checks — group AH

**Files:** `docs/superpowers/specs/2026-10-23-annotation-measurements-export-design.md` §8 (results and a run record); `CLAUDE.md` (the Task 3 bullet: checks run, result).

- [ ] **Step 1: Run AH1–AH11.** Fixtures: Philips-1 scene 0 (back up its annotation file first and restore it after), `CMU-1-Small-Region.svs` for drawn shapes, a slide with no pixel size for AH6 (find one among the fixtures by its Properties panel; if none, record AH6 as answered by the unit test). AH5 needs Excel: if the machine has none, run it in LibreOffice Calc and mark it for the project owner. Read the file back with a CSV parser, not by eye.
- [ ] **Step 2: Record and commit** — `git commit -m "Record the group AH run"`
