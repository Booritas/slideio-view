# Export of annotations to GeoJSON — design

Stage 3 item 9, the export half; FR-ANN-34 (Must), its GeoJSON part. The
per-annotation CSV of measurements that FR-ANN-34 also names is a separate piece
of work (§2). Written 2026-10-21 without a question round — the project owner
asked for the design directly — so every decision below is the author's call
unless it is quoted from the requirements or the code, and is marked as such
where it is a judgement rather than a consequence.

## 1. Why

Annotations made in this viewer can be read back only by this viewer. The saved
file (`<slideId>.s<N>.annotations.json`) is JSON and documented, but it is our
schema: no other tool reads it. The tool a pathologist or an image analyst is
most likely to move work into is **QuPath**, and QuPath's interchange format is
GeoJSON. One menu action that writes a scene's annotations as a GeoJSON file
QuPath imports — shapes in the right place, classifications carried — is what
makes work done here usable anywhere else.

This is the export half only. Import (FR-ANN-33, QuPath GeoJSON and ASAP XML) is
the next piece, and this format is deliberately the one it will read back, so
the two meet in the middle.

## 2. Goals, non-goals, anti-goals

**Goals**

- `File > Export Annotations as GeoJSON...` writes the current scene's annotations to a
  `.geojson` file.
- QuPath 0.7 imports that file (drag-and-drop or `File > Import objects from
  file`) onto the same image and shows every shape where the viewer showed it,
  with its classification and name.
- Nothing the viewer knows about an annotation is lost in the file, even where
  QuPath has no field for it: arrow direction, notes, author, timestamps,
  colour, line width, fill opacity.
- The same gating and file handling as the counts export, so the two exports
  behave alike.

**Non-goals**

- **Import.** FR-ANN-33, next.
- **The per-annotation measurements CSV** of FR-ANN-34. Different reader,
  different file; its own design.
- **ASAP XML, W3C Web Annotation, or any other format.**
- **Exporting a subset** — the selection, the filtered list, one category.
  Possible later; not now (and see the anti-goals for why the filter is not a
  subset by accident).
- **Exporting several scenes or slides in one go.** One file per scene, as the
  coordinate space is per scene.
- **Geographic correctness.** RFC 7946 says coordinates are WGS84
  longitude/latitude; in pathology they are pixel coordinates by universal
  convention (QuPath, OpenSlide-based pipelines). We follow the convention and
  say so in the file.

**Anti-goals** — it fails even if it "works" when:

- **Shapes land in the wrong place in QuPath**: offset, scaled, flipped, or on
  the wrong scene. A file that imports cleanly with every shape displaced is
  worse than one that refuses to import.
- **The export silently drops or alters annotations** — the list filter
  narrowing it, an unusual shape skipped, an arrow reversed, a classification
  renamed. Every annotation in the scene lands in the file as exactly one
  feature, or the export fails loudly.
- **The file disagrees with the screen about a shape** — the painter, hit test
  and measurement all read `core::outline()`, so the export reads it too.
- **The file cannot be parsed** — a non-finite coordinate written as `null`, a
  truncated write reported as success.

## 3. What it is

### The file

One GeoJSON `FeatureCollection` per scene, UTF-8, **no byte-order mark** (RFC
8259 forbids one in JSON; this is not the CSV, which wanted one for Excel), LF
line endings.

```json
{
  "type": "FeatureCollection",
  "slideio": {
    "format": "slideio-viewer annotation export",
    "formatVersion": 1,
    "slide": "D:/slides/Philips-1.tiff",
    "slideId": "ec83fc2ebbfbc515",
    "scene": 0,
    "exportedAt": "2026-10-21T10:00:00Z",
    "coordinates": "level-0 pixels of the scene; origin top-left, x right, y down",
    "pixelSizeMicrons": [0.25, 0.25]
  },
  "features": [
    {
      "type": "Feature",
      "id": "ef4c7b95-faee-423f-a17f-51d5fc53fd3a",
      "geometry": { "type": "Polygon", "coordinates": [[[100,100],[400,100],[400,300],[100,300],[100,100]]] },
      "properties": {
        "objectType": "annotation",
        "name": "Region 1",
        "classification": { "name": "Tumor", "color": [230, 126, 34] },
        "color": [230, 126, 34],
        "slideio": {
          "type": "rectangle",
          "notes": "",
          "author": "booritas",
          "createdAt": "2026-10-18T09:44:29Z",
          "modifiedAt": "2026-10-18T09:44:29Z",
          "color": "#E67E22FF",
          "lineWidth": 2.0,
          "fillOpacity": 0.3
        }
      }
    }
  ]
}
```

The example is pretty-printed for reading; the written file is compact (§3
decisions).

### Coordinates

**Level-0 pixels of the scene, origin top-left, x right, y down** — exactly the
space every viewer annotation is stored in, and exactly what QuPath uses for an
image's annotations. No transform is applied, which is the point: there is no
transform to get wrong. Multi-scene files: QuPath opens each scene as its own
image, with its own pixel space, so a per-scene file in that scene's coordinates
is what it expects.

### Geometry mapping

Every shape is written from `core::outline()` — the one sequence of points the
painter strokes, the hit test tests and `measure()` measures — so the exported
shape is the shape on screen by construction, never a second reading of the
stored geometry.

| Viewer type | GeoJSON geometry | Notes |
|---|---|---|
| Rectangle | `Polygon`, one ring of 5 positions | the four normalised corners, closed |
| Ellipse | `Polygon`, one ring of 513 positions | the 512-gon `outline()` already is; GeoJSON has no ellipse, so it imports into QuPath as a polygon, not as QuPath's ellipse ROI |
| Polygon (closed) | `Polygon`, one ring | outline points, first repeated at the end |
| Freehand line (open) | `LineString` | outline points as they are |
| Point marker | `Point` | |
| Arrow | `LineString` of 2 positions, **tail first, head second** | GeoJSON and QuPath have no arrow; `slideio.type: "arrow"` keeps it, and the position order keeps its direction |

Rings are written closed (first position repeated) as RFC 7946 requires.
Winding order is left as drawn: RFC 7946's right-hand rule is a SHOULD that
QuPath does not need, and re-winding would reverse what a reader of the
`slideio` block may compare against. **A self-intersecting polygon is written
as it is.** RFC 7946 calls such a polygon invalid, but pathologists draw them,
the viewer treats them as valid data (even-odd fill and hit test), and QuPath
imports them; "fixing" one would change the region.

**Degenerate shapes** (author's call). `outline()` drops consecutive duplicate
points, so a stored polygon of three points two of which coincide yields two.
A ring needs at least four positions (three distinct plus the closing repeat).
When `outline()` leaves a closed shape fewer than three points, the ring is
written from the **stored** vertices instead, which the serializer guarantees
number at least `kMinPolygonVertices` (3); a closed ring of those has the four
positions RFC 7946 requires. Likewise an open line left with fewer than two
outline points is written from its stored vertices (at least
`kMinPolylineVertices`, 2). A zero-extent rectangle or ellipse writes its
repeated corners as they are. Nothing is dropped.

### Properties

- `objectType: "annotation"` — QuPath's discriminator between annotations and
  detections. Always written.
- `name` — the annotation's **label**, omitted when empty (QuPath shows the class
  instead).
- `classification` — `{ "name": <classification>, "color": [r, g, b] }`,
  **omitted when the annotation is unclassified**, as decided by
  `core::isUnclassifiedCategory` — empty, whitespace or any casing of
  "Unclassified" — since QuPath represents "no class" by the absence of the
  field, and a class literally named Unclassified would appear in QuPath as a
  real class. The name is written as stored, not case-folded. Its `color` is the
  annotation's colour without alpha (author's call: the viewer has no per-class
  colour, and QuPath keeps one colour per class).
- `color` — `[r, g, b]`, the **annotation's own** colour, for every feature,
  classified or not. QuPath reads an object colour separately from its class
  colour, so annotations of one class drawn in different colours keep them;
  without this they would all take whichever colour QuPath settled on for the
  class.
- `slideio` — everything else, under one key so it cannot collide with a
  property QuPath or a future reader defines: `type` (the viewer's type name,
  which is how an arrow survives), `notes`, `author`, `createdAt`,
  `modifiedAt` (ISO 8601 UTC via `core::formatIso8601Utc`), `color` as
  `#RRGGBBAA`, `lineWidth`, `fillOpacity`. Written for every feature, empty
  strings included, so a reader never has to guess whether a field was empty or
  missing.

`id` is the annotation's UUID. QuPath (0.4 onward) writes an object's UUID as
the feature's `id` and is expected to read it back the same way, so a round
trip should keep identity; AF4 confirms it against QuPath 0.7.0 rather than this
document asserting it. Nothing in this design depends on it.

**The QuPath field names were checked against QuPath 0.7.0 itself**, not
recalled: the names compiled into `qupath-core-0.7.0.jar`'s
`QuPathTypeAdapters$PathObjectTypeAdapter` include `objectType`, `id`, `name`,
`color`, `classification` and `isLocked`, and its class proxy
(`GsonTools$PathClassTypeAdapter$PathClassProxy`) carries `name` and `color`.
That establishes the names, not every detail of how QuPath reads them; AF4–AF7
are the behavioural check.

### The collection's `slideio` block

Provenance, as the counts CSV repeats it on every row: the slide path as
opened, its content-derived `slideId`, the scene index, the export instant, a
plain-English statement of the coordinate space, and `pixelSizeMicrons` when the
slide reports a resolution (from `SlideInfo::resolutionX/Y`, metres converted to
microns), **omitted when it does not** rather than written as zero. RFC 7946
allows such foreign members; QuPath ignores them. `formatVersion` lets the
importer refuse a future layout instead of misreading it.

### The action

`File > Export Annotations as GeoJSON...`, directly below `Export Counts...` —
named for its format, because the GeoJSON import sits beside it and a CSV of
per-annotation measurements (FR-ANN-34) will join them.

- **Enabled exactly when Export Counts is**: a slide open, editable or not — a
  read-only slide's annotations are exactly what a reader wants to take
  elsewhere. Paired with `closeAction` at the same four sites as
  `exportCountsAction`, and absent from `applyAnnotationAvailability`.
- **Every annotation in the scene**, from `AnnotationModel::annotations()`,
  never the filtered list — the anti-goal above.
- **Save dialog**: suggested `<slide file name>.s<N>.geojson`, opened in the
  annotation workspace (this instance's, per the 2026-10-21 fix), filter
  `GeoJSON (*.geojson);;JSON (*.json);;All files (*)`. The dialog asks about
  overwriting.
- **Write** exactly as the counts export does — `WriteOnly | Truncate`, check
  the byte count, explicit `flush()` and `close()`, a warning naming the path
  and the reason on failure, a transient status-bar confirmation naming the
  file on success.
- **An empty scene** writes a valid empty `FeatureCollection` (author's call:
  the counts export writes an empty count too, and "this scene has no
  annotations" is a statement worth filing).
- **A scene that cannot be represented** — a non-finite coordinate, a
  timestamp `formatIso8601Utc` cannot write — fails the export with a warning
  naming the first annotation that could not be written; no file is created.
  The same rule as the serializer: never write what will not parse.

### Decisions, and why

- **Core, not ui.** The writer is a pure function in core:
  `std::string annotationsToGeoJson(const std::vector<Annotation>&, const
  GeoJsonExportContext&)` (names are the builder's), next to
  `AnnotationSerialization.cpp`, because core already links nlohmann (PRIVATE)
  and core-tests can drive it with no Qt. The ui action is wiring around it,
  as `exportCounts` is around `categoryCountCsv`. nlohmann stays out of every
  core public header.
- **`nlohmann::ordered_json`**, as the serializer uses: keys in the order above,
  `type` first, so the file reads naturally when opened.
- **Compact output** (author's call). An ellipse alone is 513 positions; pretty
  printing puts every number on its own line and multiplies a file of a few
  hundred annotations into hundreds of thousands of lines nobody reads. QuPath
  and scripts do not care. Trailing newline kept.
- **One feature per annotation, always.** No MultiPolygon merging, no
  splitting, no skipping — the count of features equals the annotation count,
  which is what makes "nothing was dropped" checkable.
- **The file stem reuses the counts export's sanitising** — reserved
  characters to `_`, trailing dots and spaces stripped. Today that is
  `ui::countExportFileStem`, which appends `.counts`; the builder should factor
  the shared part rather than copy it, so the two stems cannot drift.

## 4. Where the code lives

- `src/core/include/slideio/viewer/core/AnnotationGeoJson.h` /
  `src/core/src/AnnotationGeoJson.cpp` — the writer and its context struct.
  Added to `src/core/CMakeLists.txt`.
- `tests/core/AnnotationGeoJsonTest.cpp` — see §6.
- `src/ui/src/MainWindow.cpp` — the action, its four enable/disable sites, the
  dialog and the write. `exportCounts` is the template.
- The file-stem helper in `AnnotationCountExport.{h,cpp}` (or a new shared
  place), with its existing tests kept green.

## 5. Deliberate absences

- No layer information: there are no layers yet.
- No QuPath `measurements` block. QuPath recomputes measurements on import;
  writing ours would invite two disagreeing numbers. The CSV is where
  measurements go.
- No `isLocked`. QuPath defaults it to false.
- No z-plane or time-point: annotations in this viewer are not per plane.

## 6. Tests

Core-tests, against `annotationsToGeoJson`, parsing the output back with
nlohmann in the test:

1. Each of the six types maps to the geometry type and position count in the
   table; the arrow's positions are tail then head.
2. A rectangle drawn right-to-left (inverted corners) is written normalised,
   matching `outline()`.
3. Every closed ring's first and last positions are equal.
4. The ellipse ring is `outline()` plus its closing repeat — 513 positions.
5. A polygon whose stored vertices include a consecutive duplicate is written
   from `outline()` (duplicate gone); one that `outline()` reduces below three
   points is written from its stored vertices, closed, with at least four
   positions.
6. A self-intersecting polygon is written unchanged.
7. Unclassified, whitespace, and "unclassified" in any case all omit
   `classification`; a real one is written with its spelling and the
   annotation's RGB.
8. An empty label omits `name`; every feature carries the object `color` as
   its RGB, classified or not.
9. `slideio` per feature carries every field, empty strings included; colour
   as `#RRGGBBAA`.
10. Feature count equals annotation count; ids are the annotation ids, in model
    order.
11. Provenance block: path, slide id, scene, timestamp; `pixelSizeMicrons`
    present when the resolution is positive and absent when zero.
12. A non-finite coordinate makes the writer return its failure value (empty
    string or the builder's equivalent) and name the annotation.
13. An empty annotation list yields a valid collection with an empty
    `features` array.
14. Non-ASCII classification and label (Greek, Cyrillic) round-trip byte for
    byte as UTF-8, unescaped.

Ui-tests: the new file stem for a plain name, a name with reserved characters,
and scene 1.

## 7. Manual checks — group AF

Written before the run. QuPath 0.7.0 is installed on the development machine.

| | Check | Result |
|---|---|---|
| AF1 | With no slide open, File > Export Annotations is disabled; open a slide: enabled; close: disabled. | Pass |
| AF2 | The suggested name is `<slide>.s<N>.geojson` and the dialog opens in the annotation workspace. | Pass — `Philips-1.tiff.s0.geojson` in `D:\tmp\ad-run` |
| AF3 | Export Philips-1 scene 0 (27 annotations: points, arrows, rectangles): the file parses, holds 27 features, and its ids match the annotation file's. | Pass — 27 features, ids identical and in order |
| AF4 | Open Philips-1 in QuPath 0.7.0 and import the file: 27 objects, each on the shape the viewer shows, none offset or scaled. | Pass, by QuPath's reader rather than its window — see below |
| AF5 | In QuPath, classifications appear by name — Greek, Cyrillic and `Tumor, invasive` intact — unclassified annotations have no class, and each object shows the colour it had in the viewer. | Pass — names byte-identical; 9 with no class |
| AF6 | An ellipse and a closed polygon drawn in the viewer import into QuPath as areas with the same outline. | Pass — ellipse bounds equal the stored box |
| AF7 | A freehand line and an arrow import as lines; the arrow's first point is its tail. | Pass |
| AF8 | Export scene 1 of the 3-scene SCN, open that scene in QuPath, import: shapes on scene 1's tissue. | Pass — QuPath series 1 is scene 1 |
| AF9 | Type into the list's filter box, then export: the file still holds every annotation. | Pass — "0 of 3" filtered, 3 features written |
| AF10 | Export from a read-only slide: it works and holds every annotation. | Pass |
| AF11 | Export an empty scene: a valid empty collection, and QuPath imports it without error. | Pass |
| AF12 | Cancel the dialog: no file, no message. | Pass |
| AF13 | Export twice to one name: the dialog asks first, and the result is one complete file. | Pass |
| AF14 | Export onto a write-denied file: a warning names the path and the reason; the file is untouched. | Pass |
| AF15 | The status bar confirms the export and names the file. | Pass |

**Run 2026-10-21 against the installed build of `ef24542`: 15/15 passed.** The
viewer was driven by UI Automation and synthetic input; every exported file was
parsed back, not read by eye.

**QuPath was checked through its own reader, not its window.** AF4–AF8 and AF11
ran each file through `QuPath-0.7.0 (console).exe script --image=<slide>`,
whose Groovy script calls `PathIO.readObjects` — the reader behind QuPath's
*Import objects* — and prints every object's id, ROI kind, class, name, colour,
bounds and first and last point. That compares numbers where a screenshot
compares pixels, and it is stronger for the anti-goal it guards ("shapes land
in the wrong place"); what it does not exercise is QuPath's GUI import command
and the drawing of the result, which a reader opening the file in QuPath's
window would still see. **The project owner did that on 2026-10-23 — QuPath's
GUI import of the exported files — and it passed.** Results:

- Philips-1: 27 objects, every id the annotation file's, every marker, arrow
  end and box corner at its stored coordinate to the printed precision. The
  five-point rectangle ring came back as QuPath's own `Rectangle` ROI. Class
  names compared as percent-encoded UTF-8: Νέκρωση and Опухоль byte-identical,
  `Tumor, invasive` and `=1+1` intact, the 9 unclassified with no class. Every
  object carried `#E67E22` — all 27 happen to share one colour, so per-object
  colour was shown to arrive, not to stay distinct across a class.
- A scratch ellipse, polygon and freehand line on `CMU-1-Small-Region.svs`:
  the ellipse became a 513-point `Polygon` whose bounds equal the stored box
  exactly (including its overhang past the slide's left edge) and whose area,
  599 728 px², is within 0.003 % of the true ellipse; the polygon an area; the
  line a `Line` starting at its stored first vertex. The arrow check came from
  Philips-1's nine arrows, each starting at its stored tail.
- The three-scene SCN opens in QuPath through Bio-Formats as one image per
  series. Series 1 is 9462×14555, slideio's scene 1; its export imported there
  with all six markers at their stored positions. Scene 2's empty export
  imported onto series 2 as 0 objects, without error.

AF14 used a write-denied file, as AD14 did, since a read-only folder is refused
by the Windows save dialog before the application sees the path.

## 8. Requirements

| Requirement | How |
|---|---|
| FR-ANN-34 — export to GeoJSON | This design. The CSV half is separate work. |
| FR-ANN-33 — import from QuPath GeoJSON | Not here; this format is chosen so import reads it back. |
| FR-ANN-27 — native storage GeoJSON-based | Unchanged. The native file is not GeoJSON and this does not make it so. |

## 9. Left to the builder

- Function, struct and file names; whether the context struct mirrors
  `CountExportContext`.
- How the writer reports which annotation failed (a result struct or an
  out-parameter), as long as the ui can name it.
- Where the shared file-stem helper lives.
- Whether the action has a shortcut. None is proposed; the bare-letter keys are
  taken by tools.
