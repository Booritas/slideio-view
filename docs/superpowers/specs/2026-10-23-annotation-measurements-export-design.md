# Export of annotation measurements to CSV — design

Stage 3 item 9, the last piece of its export half; FR-ANN-34 (Must), its "CSV
(measurements)" part. Written 2026-10-23 without a question round — the
project owner asked for the design directly — so every decision below is the
author's call unless it is quoted from the requirements or the code, and is
marked as such where it is a judgement rather than a consequence.

It sits between two exports that already exist and borrows from both: the
**counts export** (`2026-10-20-annotation-count-export-design.md`) settled how
a CSV is written for Excel, and the **GeoJSON export**
(`2026-10-21-annotation-geojson-export-design.md`) settled that a per-annotation
file covers every annotation, unfiltered, from the one geometry the screen
uses.

## 1. Why

The viewer measures every annotation already — the properties panel shows a
selected shape's area and perimeter, or a line's length, in microns when the
slide is calibrated — but only one at a time, on screen. Getting the
measurements of a scene into a spreadsheet or a statistics script means
reading them off one by one. The counts export answers "how many of each";
this answers "how big is each one". The architecture document's export table
names the shape of it: "Flat table: id, type, label, measurement, units"
(`documents/03-software-architecture-and-design.md`, §export formats).

## 2. Goals, non-goals, anti-goals

**Goals**

- `File > Export Measurements...` writes one CSV per scene: one row per
  annotation, with its area, perimeter or length, in pixels always and in
  microns when the slide is calibrated.
- Excel opens it by double-click, with non-ASCII labels and classifications
  intact and nothing evaluated as a formula — the counts export's guarantees,
  by sharing its code.
- A script reads it without guessing: a fixed header whatever the slide, one
  unit per column named in the column, numbers as plain decimals.
- Every number is the number the properties panel shows for that shape, before
  the panel's display rounding.

**Non-goals**

- **Display units.** FR-MEAS-02 (µm or mm on screen) is about display. The file
  always uses micrometres — a column whose unit depends on a preference is a
  column a script cannot trust.
- **A manual calibration override.** FR-MEAS-04 is not built. The file uses the
  slide's own pixel size and says so on every row; when an override exists, it
  will be the value written there (§7).
- **Aggregates** — sums, means, per-class totals. A spreadsheet does those; a
  total row is the classic thing that becomes a data row on re-read (the
  counts export's reasoning).
- **Stain, intensity or any pixel-content measurement.** Geometry only.
- **A subset** — the selection, the filtered list, one class.
- **Several scenes or slides in one file.**
- **Making the file work in a German-locale Excel by double-click** (§6).

**Anti-goals** — it fails even if it "works" when:

- **A number in the file disagrees with the panel** for the same shape.
- **An annotation is missing** — filtered, skipped for an unusual shape, or
  dropped because it has nothing to measure. Every annotation is one row.
- **A missing measurement reads as zero.** A self-intersecting polygon has no
  area; an empty cell with the reason beside it is right, a `0` is a false
  finding.
- **A pixel figure is read as microns, or the reverse.** The unit is in the
  column name, and an uncalibrated slide leaves the micron columns empty.
- **A cell the user typed is evaluated** by the spreadsheet.

## 3. What it is

### The file

UTF-8 **with** a byte-order mark, CRLF line endings, RFC 4180 quoting, comma
separated — exactly as the counts export, through the same helpers (§4). One
header row, then one row per annotation in model order (the order of the
list's "Creation order" view, which is paint order).

```
id,type,label,classification,center_x_px,center_y_px,area_px2,perimeter_px,length_px,area_um2,perimeter_um,length_um,area_note,pixel_width_um,pixel_height_um,slide,scene,slide_id,exported_at
ef4c7b95-…,rectangle,Region 1,Tumor,250,200,60000,1000,,3088.8,226.89,,,0.2269,0.2269,D:/slides/Philips-1.tiff,0,ec83fc2ebbfbc515,2026-10-23T09:00:00Z
1b2c…,polyline,,,512.5,90,,,840.25,,,190.65,,0.2269,0.2269,…
9d8e…,polygon,bow tie,,50,50,,40,,,9.076,,self-intersecting,0.2269,0.2269,…
77aa…,point,,Ki67,1000,1500,,,,,,,,0.2269,0.2269,…
```

### Columns

Fixed, in this order, whatever the slide — so two exports paste one under the
other and a script's column indices never move. Names are snake_case ASCII,
like the counts export's.

| Column | Contents |
|---|---|
| `id` | The annotation's UUID — the same id as in the GeoJSON export and the annotation file, so rows join across files. |
| `type` | The serializer's type name: `rectangle`, `ellipse`, `polygon`, `polyline`, `point`, `arrow` (`core::annotationTypeName`). Untranslated: it is a key, not a label. |
| `label` | As stored. |
| `classification` | Trimmed, and empty when unclassified (blank or "Unclassified" in any case) — the rule the tally and the GeoJSON export use. |
| `center_x_px`, `center_y_px` | The centre of the annotation's bounding box, level-0 pixels of the scene. A marker's is its position. Locates the row on the slide; not a centroid (author's call: a centroid of a self-intersecting trace is not well defined, and the box centre is). |
| `area_px2` | Area in square pixels; closed shapes only; empty when `measure()` withholds it. |
| `perimeter_px` | Closed shapes only (rectangle, ellipse, polygon). |
| `length_px` | Open shapes only (polyline, arrow). |
| `area_um2`, `perimeter_um`, `length_um` | The same, in µm² and µm. **Empty on an uncalibrated slide.** |
| `area_note` | Why `area_*` is empty for a closed shape: `self-intersecting`. Empty otherwise — including for open shapes and points, which have no area by nature, not by failure. |
| `pixel_width_um`, `pixel_height_um` | The pixel size the micron columns were computed with; empty when the slide reports none. Provenance on every row, as below. |
| `slide`, `scene`, `slide_id`, `exported_at` | As the counts export writes them, on every row, so pasted exports still say where each row came from. |

A point marker therefore has its id, type, label, classification and position,
and every measurement column empty: it is counted, not measured. It is still a
row — the anti-goal says every annotation is one.

### Measurements

- **One call per annotation, `core::measure()`**, the function the properties
  panel calls, so the numbers cannot disagree with it. It is called twice per
  shape — once with `PixelSize{1, 1}` for the pixel columns and once with the
  slide's pixel size for the micron columns — because `measure()` scales
  vertices before measuring, which is the only order correct on non-square
  pixels; a pixel figure multiplied afterwards would be wrong there.
- **Calibrated** means both `SlideInfo::resolutionX` and `resolutionY` are
  finite and greater than zero, the test the properties panel makes. Metres
  from SlideIO become micrometres (×1e6) for the provenance columns and µm
  (and µm²) for the measurements.
- **An ellipse is its 512-gon**, as everywhere: its area reads about 0.0025%
  under the true ellipse, deterministically.
- **Self-intersection** is asked once per closed polygon, by `measure()`; an
  ellipse and a rectangle skip it (`hasWellDefinedArea`).

### Numbers

Plain decimals, locale-free, with a full stop: `std::to_chars` in fixed
notation at the shortest precision that round-trips the double. No thousands
separators, no exponent, **no display rounding** (author's call): the panel's
three significant figures are for reading, and a file that rounded would make
two shapes that differ look equal and lose precision a script may need. A
pixel count that is whole is written without a decimal point (`60000`).

### The action

`File > Export Measurements...`, directly below `Export Counts...`.

- **Gated exactly as the other two exports**: enabled with a slide open,
  editable or not; every `exportCountsAction->setEnabled` gets a twin; absent
  from `applyAnnotationAvailability`.
- **Refuses a scene whose annotations were not loaded**, through
  `annotationsReadyToExport`, as both other exports do.
- **Every annotation in the model**, never the filtered list.
- **Built before the dialog**, as the GeoJSON export is, so a scene that cannot
  be written is reported without first asking where to put it.
- **Save dialog**: suggested `<stem>.s<N>.measurements.csv` from
  `sceneExportFileStem`, opened in this instance's annotation workspace, filter
  `CSV (*.csv);;All files (*)`.
- **Write** as the counts export does: Truncate, byte count, explicit flush and
  close, a warning naming the path and reason on failure, a status-bar message
  naming the file on success.
- **An empty scene** writes the header row alone (author's call, as the other
  two exports write an empty count and an empty collection).
- **A measurement that is not finite** — possible only for an annotation the
  serializer would already refuse — fails the export naming the annotation,
  the GeoJSON export's rule.

## 4. Where the code lives

- `src/ui/include/slideio/viewer/ui/AnnotationMeasurementExport.h` /
  `src/ui/src/AnnotationMeasurementExport.cpp` — `annotationMeasurementCsv`
  (names are the builder's), pure, beside `categoryCountCsv`. In ui rather than
  core because the CSV conventions it must share live there.
- **The counts export's CSV helpers move out of its anonymous namespace** into
  a small shared ui unit (the formula guard, field quoting, the BOM, CRLF, and
  the provenance columns), so the two CSV files cannot drift. The counts
  export's 19 tests stay green unchanged.
- `src/ui/src/MainWindow.cpp` — the action, its enable sites, the dialog and
  the write, modelled on `exportCounts`.

## 5. Tests

Ui-tests, against the formatter:

1. The header is exactly the column list above, on a calibrated and an
   uncalibrated slide alike.
2. One row per annotation, in model order, ids as given; an empty list gives
   the header alone.
3. A rectangle 300×200 px on a 0.25 µm slide: area 60000 px² and 3750 µm²,
   perimeter 1000 px and 250 µm, length empty.
4. Non-square pixels (0.25 × 0.5 µm): a horizontal and a vertical line of
   equal pixel length have different micron lengths — the scaling-order
   guarantee.
5. An ellipse's area equals `core::measure()`'s, not πab.
6. A self-intersecting polygon: area columns empty, `area_note` is
   `self-intersecting`, perimeter present.
7. A polyline and an arrow: `length_*` set, `area_*` and `perimeter_*` empty,
   `area_note` empty.
8. A point: position set, every measurement column empty.
9. Uncalibrated (pixel size 0, and NaN): every `_um` column and both
   `pixel_*_um` empty; pixel columns present.
10. Classification trimmed; "Unclassified" and blank written empty.
11. A label or classification beginning `=`, `+`, `-` or `@` gets the
    apostrophe; one with a comma, quote or newline is quoted — through the
    shared helpers.
12. Greek and Cyrillic labels survive as UTF-8; the file starts with the BOM;
    every line ends CRLF.
13. Numbers: no exponent and no thousands separator for 1e-7 µm² and 1.5e10
    px²; a whole number has no decimal point; every number parses back to the
    double it came from.
14. A non-finite measurement fails the export naming the annotation.
15. The file stem is `<stem>.s<N>.measurements`.

## 6. Known limitation: comma-separated CSV and non-English Excel

Excel in a locale whose decimal separator is a comma (German, among others)
expects a **semicolon**-separated `.csv` on double-click, and opens this file —
and the counts file — with every row in column A. The usual fixes each break
something else: a `sep=,` first line makes Excel ignore the byte-order mark
and turns non-ASCII text to mojibake; semicolons and decimal commas would make
the file locale-dependent for every script. The file stays RFC 4180 with full
stops (author's call), and Excel's *Data > From Text/CSV* import reads it
correctly in any locale. This is recorded here rather than solved, and applies
to both CSV exports equally.

## 7. Requirements

| Requirement | How |
|---|---|
| FR-ANN-34 — export to CSV (measurements) | This design. With the GeoJSON export, FR-ANN-34 is complete. |
| FR-MEAS-01 — scale from the slide's microns-per-pixel | The micron columns use `SlideInfo::resolutionX/Y`. |
| FR-MEAS-02 — µm or mm display units | Not applicable: the file uses µm always (§2). |
| FR-MEAS-04 — manual calibration override | Not built. When it is, its value is what `pixel_*_um` and the micron columns use. |
| FR-MEAS-05 — warning when calibration is absent | In the file, the empty micron and pixel-size columns are that warning; nothing is invented. |

## 8. Manual checks — group AH

Written before the run.

| | Check | Result |
|---|---|---|
| AH1 | With no slide open, File > Export Measurements is disabled; open a slide: enabled; close: disabled. | Pass |
| AH2 | The suggested name is `<slide>.s<N>.measurements.csv`, in the annotation workspace. | Pass — `Philips-1.tiff.s0.measurements.csv` in `D:\tmp\ad-run` |
| AH3 | Export Philips-1 scene 0: one row per annotation (27), ids matching the annotation file. | Pass — 27 rows, ids identical and in order, BOM present |
| AH4 | Select a rectangle, a polygon and a freehand line in turn: the panel's area, perimeter and length agree with the file's micron columns to the panel's three figures. | Pass — rectangle, ellipse, polygon and freehand line all agree |
| AH5 | Open the file in Excel by double-click: columns split, Greek and Cyrillic intact, `=1+1` shown as text. | Pass in **LibreOffice Calc** — Excel left to the project owner, see below |
| AH6 | An uncalibrated slide: micron and pixel-size columns empty, pixel columns filled. | Pass — a BMP fixture |
| AH7 | A self-intersecting trace: area empty, `area_note` says `self-intersecting`. | Pass |
| AH8 | Type in the list's filter box, then export: every annotation is still in the file. | Pass — "0 of 4" shown, 4 rows written |
| AH9 | A read-only slide exports; a scene whose annotation file could not be read is refused with a warning. | Pass |
| AH10 | Export onto a write-denied file: a warning names the path and the reason. | Pass — "Access is denied."; file untouched |
| AH11 | The status bar names the file. | Pass |

**Run 2026-10-23 against the installed build of `a63da40`: 11/11 passed**,
AH5 in LibreOffice Calc. The viewer was driven by UI Automation and synthetic
input; every file was read back with a CSV parser, not by eye.

- **AH4** compared the properties panel with the file for four shapes. The
  panel's figures were 1.83 mm² and 5.61 mm for Philips-1's rectangle, and
  149000 µm² and 1.44 mm, 85600 µm² and 1.19 mm, and 696 µm for CMU's ellipse,
  polygon and freehand line. The file holds 1828029.69 µm² and 5605.72 µm, and
  149332.70 µm² and 1437.01 µm, 85563.48 µm² and 1186.48 µm, and 696.32 µm —
  the same numbers before the panel's three-figure rounding.
- **AH5 ran in LibreOffice Calc**, the test machine having no Excel: Calc's
  default import of the Philips-1 file gave 28 rows of 19 cells, the Greek and
  Cyrillic classifications byte-identical, and `=1+1` as the text `'=1+1`. As
  with the counts export's AD3/AD6/AD8, it stays for the project owner to run in
  Excel itself.
- **AH6** used `CMU-1-Small-Region-page-0.bmp`, which carries no pixel size: a
  rectangle drawn on it exported with its pixel area and perimeter and every
  micron and pixel-size column empty.
- **AH7** drew a bow-tie with the polygon tool on CMU-1-Small-Region: area
  empty, `area_note` `self-intersecting`, perimeter present.
- **AH9**'s read-only case was a second window on Philips-1 (the scene lock);
  the refusal was a scene whose annotation file was temporarily replaced with
  malformed JSON, which gave the shared "not loaded" warning.

Every fixture annotation file was restored from its backup afterwards and
checked by hash.

## 9. Left to the builder

- Function, struct and file names; the shared helper unit's name and shape.
- Whether the formatter takes a context struct mirroring `CountExportContext`
  or extends it with the pixel size.
- Whether the two `measure()` calls are shared work (they are cheap except for
  a closed polygon's self-intersection test, which `measureKnown` can reuse).
- A shortcut: none proposed.
