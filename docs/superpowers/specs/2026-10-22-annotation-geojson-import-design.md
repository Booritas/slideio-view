# Import of annotations from GeoJSON — design

Stage 3 item 9, the import half; FR-ANN-33 (Should), its QuPath GeoJSON part.
ASAP XML, which FR-ANN-33 also names, is a separate piece of work (§2). Written
2026-10-22 without a question round — the project owner asked for the design
directly — so every decision below is the author's call unless it is quoted
from the requirements or the code, and is marked as such where it is a
judgement rather than a consequence.

It is the counterpart of `2026-10-21-annotation-geojson-export-design.md`, and
reads what that writes.

## 1. Why

The export made this viewer's work usable in QuPath. Import is the other
direction: annotations drawn in QuPath, produced by a QuPath script or
classifier, or exported by this viewer on another machine, brought onto the
same scene here — shapes in the right place, classifications carried, and the
counting strip counting them. Today the only way to get annotations into the
viewer is to draw them.

## 2. Goals, non-goals, anti-goals

**Goals**

- `File > Import Annotations from GeoJSON...` reads a `.geojson` / `.json` file
  and **adds** its annotations to the current scene.
- Everything QuPath 0.7 writes with *Export objects as GeoJSON* for annotations
  imports: polygons, rectangles, ellipses (as QuPath writes them — polygons),
  lines and polylines, and points, with name, classification and colour.
- A file this viewer exported comes back **exactly**: every type including the
  arrow and the ellipse, and every field the `slideio` block carries — notes,
  author, timestamps, RGBA colour, line width, fill opacity — and the same ids.
- The user sees what will happen **before** anything changes, and the import is
  one undo step.

**Non-goals**

- **ASAP XML.** FR-ANN-33's other half; its own design.
- **Replacing the scene's annotations.** Import adds. To replace, delete first;
  a replace that has to be undone is a much worse surprise than an add (§3
  decisions).
- **Detections, cells, tiles and TMA cores.** The viewer has annotations only.
  They are counted and reported, never silently dropped (§3).
- **Polygons with holes, and geometry collections.** The viewer has no shape
  for either. Reported and skipped (§3).
- **Coordinate transforms.** No scaling, offset or flip on the way in; the file
  is read in the scene's level-0 pixels, as the export writes it and as QuPath
  writes it.
- **Measurements, metadata, `isLocked`, hierarchy.** Ignored.
- **Compressed or zipped files**, and importing into several scenes at once.
- **Adding imported classifications to the configured category list.** The
  vocabulary is a global preference; the tally already shows an unconfigured
  category as a row.

**Anti-goals** — it fails even if it "works" when:

- **Shapes land in the wrong place**: offset, scaled, flipped, or onto a
  different scene from the one they were drawn on — **without the user being
  told**. The file cannot always know which image it belongs to, so the
  importer cannot always refuse; what it must do is say so when the file says
  it came from somewhere else, or when the shapes lie off the scene.
- **Something in the file is silently not imported.** Every feature becomes
  one annotation, or several (a multi-part geometry), or is counted under a
  named reason in the summary. The numbers in the summary add up to the
  features in the file.
- **A second import of the same file doubles every annotation.** The clinical
  count would double with nothing visibly wrong.
- **A malformed file partly changes the scene.** A file that cannot be parsed
  changes nothing.
- **An import cannot be undone in one step**, or is not saved.

## 3. What it is

### The action

`File > Import Annotations from GeoJSON...`, directly **above** the two
exports.

- **Enabled exactly when the drawing tools are**: a slide open and its
  annotations editable — it is the only menu action that creates annotations,
  so it is gated where creation is, in `applyAnnotationAvailability`, not with
  the exports. A read-only slide, a scene locked by another instance, an
  unloaded scene, an associated image: disabled. (`annotationsEditable()`
  implies `annotationsLoaded()`, so the loaded check the exports needed is
  implied here.) The `commitPendingShape`-style gate is repeated where the
  import commits, as defence in depth, per the "every path that creates an
  annotation asks `annotationsEditable()` first" rule.
- **Open dialog** in the annotation workspace (this instance's), filter
  `GeoJSON (*.geojson *.json);;All files (*)`.
- **Parse first, then confirm.** The file is read and converted entirely
  before anything is shown or changed. Then **one** dialog summarises what
  will happen, and only its *Import* button changes the scene:

  ```
  Import annotations from Philips-1.tiff.s0.geojson?

    27 annotations will be added.
     3 already in this scene (same id) — skipped.
   112 detections — skipped (only annotations are imported).
     1 polygon with holes — skipped.

  ⚠ This file was exported from scene 1 of "Philips-1.tiff";
    the open scene is scene 0.
  ⚠ 4 of the annotations lie entirely outside this scene.

                                    [Import]  [Cancel]
  ```

  Lines with a zero count are not shown. With nothing to add, the dialog is an
  information box with *OK* only, and nothing changes.
- **A file that cannot be read** — not JSON, not UTF-8, not a Feature,
  FeatureCollection or array of Features — is a warning naming the file and
  the reason, and nothing changes.
- **On Import**: the annotations are appended to the model in file order, in
  one call that emits one `annotationsBulkChanged` (not one signal per
  annotation — a list panel rebuild per annotation would make a large import
  quadratic), recorded as **one undo step** ("Import 27 annotations"), and
  autosaved through the same signal that already marks the document dirty for
  a retag. The selection is cleared. A transient status-bar message confirms
  the count and names the file.

### Reading the file

The top level may be a `FeatureCollection`, a single `Feature`, or a JSON
**array** of Features — QuPath writes the array form, and `PathIO` reads all
three. A UTF-8 byte-order mark is tolerated and skipped (author's call:
Windows tools add one). Anything else at the top level, including a bare
geometry, is a read failure.

**Which features are annotations.** `properties.objectType` — or the legacy
`properties.object_type` QuPath still accepts — of `"annotation"`, or **no
object type at all** (a plain GeoJSON file from any other tool is a set of
annotations to the person importing it). `"detection"`, `"cell"`, `"tile"`,
`"tma_core"` and any other value are counted per kind and skipped.
`childObjects` nested under a feature are counted and skipped (QuPath writes
them only when asked to export a hierarchy).

### Geometry mapping

Coordinates are taken as level-0 pixels of the scene, unchanged.

| GeoJSON geometry | Viewer annotation |
|---|---|
| `Point` | Point marker |
| `MultiPoint` | **one point marker per position** — QuPath's Points tool writes a whole counting class as one MultiPoint, and each is a counted object here |
| `LineString`, 2+ positions | open polyline (freehand line) |
| `MultiLineString` | one polyline per part |
| `Polygon`, exterior ring only | closed polygon, from the ring with its closing repeat dropped |
| `Polygon` whose ring is 4 distinct axis-aligned corners | **rectangle** (author's call: QuPath itself reads such a ring back as its Rectangle ROI, and a rectangle here gets resize handles; the outline is identical) |
| `MultiPolygon` without holes | one closed polygon (or rectangle) per part |
| `Polygon` / `MultiPolygon` with any interior ring | skipped, counted "with holes" |
| `GeometryCollection`, null geometry | skipped, counted "unsupported geometry" |

**A multi-part feature becomes several annotations.** Each part carries the
feature's properties; the first part keeps the feature's id and the rest get
fresh ones, so a re-import still finds the first part already present. The
summary counts annotations added, which may exceed features read; that is why
it says "annotations", not "features".

**Rejected shapes**, counted "invalid geometry" and skipped: a non-finite or
non-numeric coordinate, a position with fewer than two numbers (a third, `z`,
is ignored), a polygon ring with fewer than `kMinPolygonVertices` distinct
positions after the closing repeat is dropped, a line with fewer than
`kMinPolylineVertices`. These are the serializer's own limits, so an imported
annotation is always one the application can save and read back.

**Self-intersecting rings are imported as they are**, as the export writes
them: the viewer treats them as valid data.

### Shapes this viewer exported

When a feature carries the export's `properties.slideio` block and its `type`
matches the geometry, the original type is restored:

| `slideio.type` | Requires | Restored as |
|---|---|---|
| `rectangle` | a 4-corner axis-aligned ring | rectangle with those corners |
| `ellipse` | a closed ring | ellipse whose box is the ring's bounding box — exact, because the 512-gon's vertices include the box's four extremes |
| `arrow` | a `LineString` of exactly 2 positions | arrow, **first position the tail** |
| `point`, `polygon`, `polyline` | the matching geometry | as the table above |

**The geometry wins any disagreement.** A `slideio.type` that does not fit the
geometry (a hand-edited file, another tool reusing the key) is ignored and the
table above applies. That keeps a lying block from producing a shape the file
does not describe.

### Properties

From the feature, in this order of preference, all optional:

- **id** — the feature's `id` (string, or a number written as its decimal
  text), when non-empty. Absent or empty: a fresh UUID. **An id already in the
  scene is skipped and counted "already in this scene"**: it is the same
  annotation, imported before or exported from here. That is what makes a
  second import of one file add nothing (anti-goal). An id repeated within the
  file: the first occurrence wins and the rest are counted the same way.
- **label** — `properties.name`.
- **classification** — `properties.classification.name`; or, if it has a
  `names` array (QuPath's derived classes), the names joined with `": "`,
  which is how QuPath displays them. A classification `isUnclassifiedCategory`
  accepts is stored empty, as everywhere else in the viewer.
- **colour** — `slideio.color` (`#RRGGBBAA`) when present; else
  `properties.color` `[r, g, b]` (alpha 255); else the legacy integer
  `properties.colorRGB`; else `classification.color`; else the viewer's
  default colour.
- **line width, fill opacity** — from `slideio` when present and inside the
  serializer's ranges; else the defaults.
- **notes, author, createdAt, modifiedAt** — from `slideio` when present and
  parseable (`core::parseIso8601Utc`); else empty notes, **empty author** (the
  author is who drew it, not who imported it, and QuPath does not record one),
  and both timestamps set to the import instant.

A value that is present but malformed — a colour that does not parse, a line
width out of range — falls back to the next source or the default; it does
not reject the annotation (author's call: the shape is the finding, the
styling is not). Unknown keys are ignored.

### Provenance and placement warnings

Two checks feed the summary's warning lines. Neither refuses the import: a
file legitimately moves between copies of a slide whose content-derived id
differs, and annotations may legitimately overhang a scene's edge.

- **Wrong slide or scene.** When the file's top-level `slideio` block names a
  `slideId` different from the open slide's, or a `scene` different from the
  open scene, the summary says which slide and scene the file came from.
- **Off the scene.** Annotations whose bounding box lies **entirely** outside
  `[0, width] × [0, height]` of the scene are counted in a warning. Overhang is
  not counted; a shape wholly outside almost always means the wrong image or
  the wrong scene.

### Decisions, and why

- **Core, not ui.** The reader is a pure function in core next to the writer,
  e.g. `GeoJsonImportResult geoJsonToAnnotations(std::string_view text,
  std::chrono::system_clock::time_point importedAt)` (names are the builder's),
  returning the converted annotations, the skip counts by reason, the file's
  provenance if any, and an error string for a file that could not be read.
  core-tests drive it with no Qt. nlohmann stays out of every public header.
- **Core cannot mint UUIDs**, being Qt-free, so it returns an empty id where
  one must be minted and the app layer fills it with `QUuid`, as
  `AnnotationModel::add` does. The "already in this scene" check needs the
  model, so it lives in the app or ui wiring, over the reader's result —
  ideally as a small pure function a test can reach (`ui::` or `app::`, the
  builder's call).
- **Two model operations**, because neither exists: insert many annotations
  emitting `annotationsBulkChanged` once, and remove many by id the same way —
  the second is what undo calls. `insert`'s silent-drop rules (empty or
  present id) still apply per annotation.
- **One new command**, an import command holding the added annotations: undo
  removes them by id, redo re-inserts them at the end. Neither existing command
  fits: `AnnotationEditCommand` is one annotation; `AnnotationRetagCommand`
  changes annotations in place.
- **Add, never replace** (author's call). A replace destroys work, and the
  user who wants it can select-all and delete first, which is already undoable.
- **The summary dialog is the only question.** No per-feature prompts, no
  options dialog. Detections are always skipped; there is no "import
  detections too" checkbox until there is a reason to have one.
- **No size cap** (author's call). A file of tens of thousands of annotations
  is a QuPath detection export, and detections are skipped; a genuine
  annotation file of that size will be slow, and the summary tells the user
  the count before they commit. Recorded as a known risk.

## 4. Where the code lives

- `src/core/include/slideio/viewer/core/AnnotationGeoJson.h` /
  `src/core/src/AnnotationGeoJson.cpp` — the reader beside the writer (or a
  sibling file pair; the builder's call), sharing the key names.
- `src/app` — the model's bulk insert and bulk remove, and the import command.
- `src/ui/src/MainWindow.cpp` — the action, its availability in
  `applyAnnotationAvailability`, the dialogs, and the commit.
- The summary text built by a pure ui function from the reader's result, so
  its counts and wording are tested (the `categoryCountCsv` pattern).

## 5. Tests

Core-tests, against the reader:

1. **Round trip**: every annotation type, exported with `annotationsToGeoJson`
   and read back, equals the original — geometry (ellipse via its box, arrow
   tail first, rectangle corners), id, label, classification, RGBA, line
   width, fill opacity, notes, author, both timestamps.
2. Top-level FeatureCollection, single Feature and array all read; a bare
   geometry, a non-object, invalid JSON and invalid UTF-8 are read failures; a
   BOM is skipped.
3. A QuPath-style file without `slideio` blocks: Polygon → polygon,
   axis-aligned 4-corner ring → rectangle, LineString → polyline, Point →
   marker, with name, classification and `color`.
4. MultiPoint of 5 → 5 markers sharing the classification; the first keeps
   the feature id, the rest have empty ids. MultiPolygon and MultiLineString
   likewise.
5. Polygon with a hole, MultiPolygon with one holed part, GeometryCollection
   and null geometry are skipped under their reasons, and nothing from those
   features is returned.
6. `objectType` detection, cell, tile and an unknown value are counted per
   kind; missing objectType and legacy `object_type: "annotation"` import.
7. Invalid geometry — NaN written as a string, a 1-number position, a 3-point
   ring that closes to 2 distinct positions, a 1-point line — is counted and
   skipped; a `z` coordinate is ignored.
8. A `slideio.type` that disagrees with the geometry is ignored.
9. Classification `names` array is joined with `": "`; "Unclassified" in any
   case is stored empty; Greek and Cyrillic names survive byte for byte.
10. Colour precedence: `slideio.color` over `color` over `colorRGB` over
    `classification.color` over the default; a malformed value falls through.
11. Out-of-range line width and fill opacity fall back to the defaults.
12. Missing timestamps take the import instant; missing author is empty.
13. The provenance block is returned when present and absent otherwise.
14. Every feature is accounted for: annotations returned plus skipped counts
    equals the features read (with multi-part features counted per part).

App-tests: bulk insert emits one `annotationsBulkChanged` and marks the
persistence service dirty; the import command's undo removes exactly the
imported ids and its redo puts them back at the end; already-present ids are
dropped by the model, not duplicated.

Ui-tests: the summary text for each warning and skip line, zero lines hidden;
the already-present filter; the off-scene count (entirely outside counted,
overhang not).

## 6. Manual checks — group AG

Written before the run.

| | Check | Result |
|---|---|---|
| AG1 | With no slide open, Import is disabled; open an editable slide: enabled; a read-only slide, a scene locked by another instance, an associated image: disabled. | |
| AG2 | Export Philips-1 scene 0, delete every annotation, import the file: the summary says 27 will be added; after Import the scene is identical — same ids, types, arrows' directions, colours, classifications, notes. | |
| AG3 | Import the same file again: "27 already in this scene", nothing added. | |
| AG4 | Undo after an import removes all imported annotations in one step; redo restores them. | |
| AG5 | After an import, close and reopen the slide: the imported annotations were autosaved. | |
| AG6 | In QuPath 0.7.0, draw a rectangle, ellipse, polygon, polyline and a Points class of 5 on CMU-1-Small-Region.svs; export as GeoJSON; import here: each shape on the same tissue, the rectangle with resize handles, 5 markers counted in the tally under the class name. | |
| AG7 | A QuPath export containing detections: they are counted as skipped in the summary and not imported. | |
| AG8 | Import scene 1's export of the 3-scene SCN into scene 0: the summary warns it came from scene 1 and that shapes lie outside the scene; Cancel changes nothing. | |
| AG9 | Cancel the summary: nothing changes, nothing is dirty. | |
| AG10 | A file that is not JSON: a warning naming the file and the reason; nothing changes. | |
| AG11 | A file with Greek and Cyrillic classifications: they show in the list and the tally byte-identical. | |
| AG12 | The status bar confirms the import, with the count and the file name. | |

## 7. Requirements

| Requirement | How |
|---|---|
| FR-ANN-33 — import from QuPath GeoJSON | This design. ASAP XML is separate work. |
| FR-ANN-34 — export to GeoJSON | Unchanged; this reads what it writes. |
| FR-ANN-31 — autosave | Through `annotationsBulkChanged`, as a retag. |

## 8. Left to the builder

- Function, struct, command and file names.
- Whether the reader sits in `AnnotationGeoJson.cpp` or its own file.
- Where the already-present filter and the off-scene count live, as long as a
  test reaches them.
- The exact wording of the summary, within §3's content.
- Whether the action has a shortcut. None is proposed.
