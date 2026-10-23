# ASAP XML Annotation Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `File > Import Annotations...` reads ASAP XML as well as GeoJSON, choosing the reader by content, and adds an ASAP file's annotations through the existing summary, undo, autosave, worker thread and cancellable dialog.

**Architecture:** The import result becomes the format-neutral `core::AnnotationImportResult` in a new `core/AnnotationImport.h`, which also holds the content-sniffing format choice and the geometry helpers both readers share. A streaming `QXmlStreamReader` reader in the app layer, `app::asapXmlToAnnotations`, fills that result, giving every annotation a content seed that `app::planImport` turns into a stable UUID. MainWindow picks the reader inside its existing worker lambda.

**Tech Stack:** C++17, Qt 6 Core (`QXmlStreamReader`, app layer), Qt 6 Widgets (ui), Catch2 v3.

**Spec:** `docs/superpowers/specs/2026-10-23-annotation-asap-import-design.md`

## Global Constraints

- Core stays Qt-free; the XML reader is in app (`src/app`), which links Qt6::Core only. No new dependency.
- Type mapping exactly as spec §3: `Dot` → point; `PointSet` → one point per coordinate; `Polygon` → closed polygon; `Rectangle` → rectangle when its 4 coordinates are an axis-aligned box, else closed polygon; `Spline` → closed polygon through its control points, counted in `splinesApproximated`; `Measurement` → open two-point line; anything else → skipped, counted by its type name.
- Vertex minimums: `kMinPolygonVertices` (3) for Polygon / Rectangle / Spline, `kMinPolylineVertices` (2) for Measurement, 1 for Dot and PointSet; and a finite extent (`hasFiniteExtent`). Failing → `invalidGeometry`.
- Coordinates sorted by `Order` when every coordinate has one, else document order. `X`/`Y`: a value with a comma and no full stop has the comma read as the decimal point; any other non-finite or unparseable value → `invalidGeometry`.
- Classification = `PartOfGroup` unless `None` or blank; colour = annotation `Color`, else its group's `Color`, else the default; label = `Name` unless it matches `Annotation <digits>` exactly; notes and author empty; timestamps the import instant.
- Every ASAP annotation gets an empty id and a non-empty seed: `"asap" \x1F Type \x1F PartOfGroup \x1F X \x1F Y ...` (the coordinates' attribute TEXT as written, in sorted order) `\x1F <repeat index>`.
- A `DTD` token anywhere is a read failure ("ASAP files never declare a document type"); any `QXmlStreamReader` error is a read failure; a root other than `ASAP_Annotations` is a read failure. Cancel is polled once per token.
- Accounting: `featuresRead` = `<Annotation>` elements read = `featuresImported` + `invalidGeometry` + every `unsupportedTypes` count.
- Action text `"&Import Annotations..."`; filter `Annotation files (*.geojson *.json *.xml);;GeoJSON (*.geojson *.json);;ASAP XML (*.xml);;All files (*)`.
- Never run a Qt-linked test exe directly — only through `ctest`.

## Review Focus

1. **A file written by real ASAP with attributes in another order, extra attributes, self-closing `<Coordinates/>` or an empty `<Annotations/>`**: read, not refused. Pinned in Task 2.
2. **A group named in `PartOfGroup` that has no `<Group>` element**: still the classification; colour falls back to the annotation's or the default. Pinned in Task 2.
3. **Coordinates with exponents (`1.5e3`), leading `+`, or surrounding spaces**: read as numbers, as a C locale parser would. Pinned in Task 2.
4. **A GeoJSON file that starts with whitespace or a BOM, and an XML file that starts with a BOM or `<?xml`**: each reaches the right reader. Pinned in Task 1.
5. **A 50 000-annotation ASAP file** (CAMELYON detail polygons): read once, no quadratic step in group lookup or seed repetition counting. Pinned in Task 2 (count only, no timing assertion).

---

### Task 1: The format-neutral result and the shared helpers

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationImport.h`, `src/core/src/AnnotationImport.cpp` (add to `src/core/CMakeLists.txt`)
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeoJson.h`, `src/core/src/AnnotationGeoJson.cpp` — move `GeoJsonImportProvenance` and the result struct out, renamed; move `cornersOf`, `hasFiniteExtent`, `isAxisAlignedBox`, `areaFrom` out of the anonymous namespace into `AnnotationImport.cpp` as the public functions below.
- Modify every user of `GeoJsonImportResult` (ui summary header/source, MainWindow, tests) to the new name.
- Test: `tests/core/AnnotationImportTest.cpp` (new)

**Interfaces — Produces** (`AnnotationImport.h`, namespace `slideio::viewer::core`):

```cpp
struct AnnotationImportProvenance { /* the fields GeoJsonImportProvenance had */ };
struct AnnotationImportResult
{
    // every field GeoJsonImportResult had, plus:
    /// ASAP splines imported as polygons through their control points.
    int splinesApproximated = 0;
    /// Shapes of a type the viewer has no counterpart for, by the file's name for it.
    std::map<std::string, int> unsupportedTypes;
};
enum class AnnotationFileFormat { GeoJson, AsapXml };
/// By content: after an optional UTF-8 BOM and ASCII whitespace, '<' is XML; anything else is GeoJSON.
[[nodiscard]] AnnotationFileFormat detectAnnotationFileFormat(std::string_view text);
[[nodiscard]] std::pair<PointF, PointF> cornersOf(const std::vector<PointF>& points); // precondition: non-empty
[[nodiscard]] bool hasFiniteExtent(const std::vector<PointF>& points);
[[nodiscard]] bool isAxisAlignedBox(const std::vector<PointF>& ring);
/// A closed ring as the viewer's shape for it: a rectangle when it is an axis-aligned box.
[[nodiscard]] AnnotationGeometry closedAreaFrom(std::vector<PointF> ring);
```

`geoJsonToAnnotations` keeps its name and returns `AnnotationImportResult`.

- [ ] **Step 1: Write the failing tests** (tag `[core][AnnotationImport]`):

```cpp
TEST_CASE("the format is chosen by content", ...)
// "<?xml version=\"1.0\"?><ASAP_Annotations/>" -> AsapXml; "  \r\n<ASAP_Annotations/>" -> AsapXml;
// "\xEF\xBB\xBF<ASAP_Annotations/>" -> AsapXml; "{\"type\":\"Feature\"}" -> GeoJson;
// "\xEF\xBB\xBF  [" -> GeoJson; "" -> GeoJson; "garbage" -> GeoJson
TEST_CASE("isAxisAlignedBox and closedAreaFrom", ...)
// 4 axis-aligned corners -> Rectangle with the min/max corners; a rotated square -> closed polygon
TEST_CASE("hasFiniteExtent", ...)   // {(-1.7e308,0),(1.7e308,0)} false; {(0,0),(5,5)} true; {} true
```

- [ ] **Step 2: Run** — build; `build/build/tests/Release/slideio-viewer-core-tests.exe "[AnnotationImport]"`. Expected: compile error, header not found.
- [ ] **Step 3: Implement** the header and source; rename every use of `GeoJsonImportResult` / `GeoJsonImportProvenance` (no alias left behind); GeoJSON reader calls the moved helpers.
- [ ] **Step 4: Run** all four suites. Expected: all pass, every existing GeoJSON test unchanged except the type name.
- [ ] **Step 5: Commit** — `git commit -m "Make the import result format-neutral and share its geometry helpers"`

### Task 2: The ASAP XML reader

**Files:**
- Create: `src/app/include/slideio/viewer/app/AsapXmlImport.h`, `src/app/src/AsapXmlImport.cpp` (add to `src/app/CMakeLists.txt`)
- Test: `tests/app/AsapXmlImportTest.cpp` (add to app-tests in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: Task 1.
- Produces: `[[nodiscard]] core::AnnotationImportResult asapXmlToAnnotations(std::string_view text, std::chrono::system_clock::time_point importedAt, const std::atomic<bool>* cancel = nullptr);` in namespace `slideio::viewer::app`. Never throws.

Approach: one streaming pass with `QXmlStreamReader` over `QByteArray::fromRawData`; collect each `<Annotation>`'s attributes and coordinate list, and each `<Group>`'s `Name` → `Color`, in maps; resolve colours after the pass (groups follow annotations in ASAP files). Repeat counting for seeds uses a `std::map<std::string,int>`.

- [ ] **Step 1: Write the failing tests** (tag `[app][AsapXmlImport]`), building files from a small helper `asap(annotationsXml, groupsXml)`:

```cpp
TEST_CASE("each ASAP type maps as the spec's table says", ...)
// Dot -> PointGeometry; PointSet of 4 -> 4 PointGeometry; Polygon of 5 -> closed PolygonGeometry, 5 vertices;
// Rectangle (axis-aligned) -> RectangleGeometry; Rectangle (rotated) -> closed polygon;
// Spline of 6 -> closed polygon of 6 and splinesApproximated == 1; Measurement -> open PolygonGeometry of 2
TEST_CASE("coordinates follow Order, or document order without it", ...)
TEST_CASE("decimal commas and other numbers", ...)
// "1234,5" -> 1234.5; "1.5e3" -> 1500; " +7 " -> 7; "1,234.5", "abc", "nan", "inf", "" -> invalidGeometry
TEST_CASE("too few coordinates are invalid; an unknown type is skipped by name", ...)
// Polygon of 2, Measurement of 1, Dot of 0, Spline of 2 -> invalidGeometry 4; Type="Ellipse" -> unsupportedTypes["Ellipse"] == 1
TEST_CASE("groups become classifications", ...)
// PartOfGroup "Tumor" -> "Tumor"; "None" and "" -> ""; a group nested in "Parent" -> the annotation's own group;
// a PartOfGroup with no <Group> element -> still the classification
TEST_CASE("colour comes from the annotation, then its group, then the default", ...)  // malformed "#zz" falls through
TEST_CASE("automatic names are not labels", ...)  // "Annotation 12" -> ""; "Annotation 12b" and "Tumour margin" kept
TEST_CASE("every annotation is seeded from its content", ...)
// ids empty, seeds non-empty; same text twice -> same seeds; two identical shapes -> different seeds;
// a different group -> a different seed; and through app::planImport into a model: a second import adds nothing
TEST_CASE("a file that is not ASAP fails and changes nothing", ...)
// "not xml", "<Other/>", truncated "<ASAP_Annotations><Annotations><Annotation", a DTD with an external entity,
// a billion-laughs DTD -> error non-empty, annotations empty, no throw
TEST_CASE("files real ASAP might write are read", ...)
// attributes in another order, an extra attribute, <Coordinates/> on a Dot (invalid, counted),
// <Annotations/> empty, <AnnotationGroups> before <Annotations>, CRLF line ends, a BOM
TEST_CASE("a cancelled read returns cancelled and nothing else", ...)
TEST_CASE("every annotation element is accounted for", ...)  // mixed file: featuresRead == imported + invalid + unsupported
TEST_CASE("Greek and Cyrillic group names survive", ...)
TEST_CASE("a large file is read whole", ...)  // 50 000 Polygon annotations of 4 points -> 50 000 annotations
```

- [ ] **Step 2: Run** — build; `ctest --test-dir build/build -C Release -R app`. Expected: compile error, header not found.
- [ ] **Step 3: Implement** per Global Constraints.
- [ ] **Step 4: Run** all four suites. Expected: pass.
- [ ] **Step 5: Commit** — `git commit -m "Read annotations from ASAP XML"`

### Task 3: The summary lines and the action

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationGeoJsonImport.h` and `.cpp` (the summary takes `AnnotationImportResult`; two new lines); `src/ui/src/MainWindow.cpp` (action text, filter, reader choice); `CLAUDE.md` (ASAP bullet; remove "ASAP XML import" from "Not built yet"; FR-ANN-33 complete)
- Test: `tests/ui/AnnotationGeoJsonImportTest.cpp`

**Interfaces — Consumes:** Tasks 1–2.

- [ ] **Step 1: Write the failing tests**: `splinesApproximated = 2` → the text contains `2 splines imported as polygons through their control points`; `1` → singular; `0` → no such line. `unsupportedTypes["Ellipse"] = 3` → `3 annotations of type "Ellipse" — skipped (the viewer has no such shape)`.
- [ ] **Step 2: Run** — ctest `-R ui`. Expected: FAIL on the missing lines.
- [ ] **Step 3: Implement** the lines (byte-escaped dashes, as the file already does). In `MainWindow::Impl::importAnnotations`: action text and filter per Global Constraints; inside the worker lambda, `detectAnnotationFileFormat` on the bytes chooses `app::asapXmlToAnnotations` or `core::geoJsonToAnnotations`, both given `&cancel`.
- [ ] **Step 4: Run** all suites; install; confirm the installed exe's timestamp and that it contains `"Import Annotations..."`.
- [ ] **Step 5: Commit** — `git commit -m "Import ASAP XML through File > Import Annotations"`

### Task 4: Manual checks — group AI

**Files:** the spec's §6 (results, run record); `CLAUDE.md` (checks run, result).

- [ ] **Step 1: Write the fixtures** to the scratchpad: one file with each type, a CAMELYON-style file (Polygons in groups `_0`, `_1`, `_2`, `Exclusion` with their colours), a decimal-comma twin of the first, a truncated copy, and a non-ASAP `.xml` (`D:\Projects\slideio\images\images\scn\mtd.xml`).
- [ ] **Step 2: Run AI1–AI7** on `CMU-1-Small-Region.svs` (back up its annotation file first, restore after). Read the saved annotation file back with a JSON parser to check positions, classifications and colours.
- [ ] **Step 3: Record and commit** — `git commit -m "Record the group AI run"`
