# Import of annotations from ASAP XML — design

Stage 3 item 9, its last open piece; FR-ANN-33 (Should), the ASAP XML half.
Written 2026-10-23 without a question round — the project owner asked for the
design directly — so every decision below is the author's call unless it is
quoted from the requirements or the code, and is marked as such where it is a
judgement rather than a consequence.

It extends the GeoJSON import (`2026-10-22-annotation-geojson-import-design.md`)
rather than building a second one: the summary, the planning, the one-step undo,
the autosave, the worker thread and the cancellable dialog are reused as they
are. What is new is a reader for a second format.

## 1. Why

ASAP (Automated Slide Analysis Platform, Radboud UMC) is the other widely used
open annotation tool in digital pathology, and its XML format is how the
CAMELYON16/17 lymph-node metastasis datasets ship their ground truth. A user who
has ASAP annotations — their own, a colleague's, or a public dataset's — can
today only redraw them. The architecture document lists the format as
import-only for v1 (`documents/03-software-architecture-and-design.md`,
"ASAP XML | Yes | No (v1) | Common legacy format").

## 2. Goals, non-goals, anti-goals

**Goals**

- `File > Import Annotations...` reads an ASAP XML file as readily as a GeoJSON
  one and adds its annotations to the current scene, through the same summary,
  undo step and autosave.
- Every ASAP annotation type lands as the viewer shape closest to it, with its
  group as the classification and its colour, on the coordinates ASAP stored.
- Importing the same ASAP file twice adds nothing the second time.

**Non-goals**

- **Export to ASAP XML.** The architecture lists ASAP as import-only for v1.
- **ASAP group hierarchy as structure.** Groups become classifications; the tree
  of groups is not kept (§3).
- **Group attributes** (`<Attributes>`), which ASAP files rarely carry and the
  viewer has nowhere to put.
- **Exact spline curves** (§3).

**Anti-goals** — it fails even if it "works" when:

- **Shapes land in the wrong place** — offset, scaled, or with a decimal comma
  read as a thousands separator.
- **Something in the file is silently not imported.** Every annotation is
  imported or counted under a named reason, as the GeoJSON import guarantees.
- **A second import of one file doubles every annotation.**
- **A malformed or hostile file changes anything, or hangs or crashes the
  viewer.**

## 3. What it is

### The format, as read

```xml
<?xml version="1.0"?>
<ASAP_Annotations>
  <Annotations>
    <Annotation Name="Annotation 0" Type="Polygon" PartOfGroup="Tumor" Color="#F4FA58">
      <Coordinates>
        <Coordinate Order="0" X="10412.2" Y="5833.7"/>
        <Coordinate Order="1" X="10433.0" Y="5815.6"/>
        ...
      </Coordinates>
    </Annotation>
  </Annotations>
  <AnnotationGroups>
    <Group Name="Tumor" PartOfGroup="None" Color="#64FE2E"><Attributes/></Group>
  </AnnotationGroups>
</ASAP_Annotations>
```

Coordinates are level-0 pixels of the image, origin top-left — the viewer's own
space, so no transform is applied, as with GeoJSON. ASAP has no notion of a
scene; the file is read into whichever scene is open, and the summary's
off-scene warning is the guard.

### Type mapping

| ASAP `Type` | Viewer annotation |
|---|---|
| `Dot` | point marker |
| `PointSet` | **one point marker per coordinate**, as a GeoJSON MultiPoint |
| `Polygon` | closed polygon |
| `Rectangle` | **rectangle** when its four coordinates form an axis-aligned box, otherwise a closed polygon |
| `Spline` | closed polygon **through its control points**, and counted in a summary line (below) |
| `Measurement` | open two-point line (ASAP's ruler) |
| anything else | skipped, counted "unsupported type" |

Coordinates are sorted by `Order` when every coordinate has one, in document
order otherwise. The serializer's vertex minimums apply — fewer than three
points for a polygon, rectangle or spline, fewer than two for a measurement, or
none for a dot — and such an annotation is counted "invalid coordinates".

**Splines are approximated, and the summary says so** (author's call). ASAP
draws a `Spline` as a smooth closed curve through its control points; the
viewer has no curve shape. Sampling a curve would mean matching ASAP's exact
curve formula, which this design does not have a reference for and which no
ASAP installation is available to compare against. The control-point polygon
passes through every point the user placed and is never further from the curve
than the curve bulges between two of them. The summary adds a line — "N
splines imported as polygons through their control points" — so nobody is told
the shape is exact when it is not.

### Numbers

`X` and `Y` are read as plain decimals. **A decimal comma is accepted** when the
value holds a comma and no full stop (author's call): ASAP versions running
under a comma locale have written files that way, and since an attribute holds
one number, `"1234,5"` can only mean 1234.5. Anything else that is not a finite
number makes the annotation "invalid coordinates".

### Groups, classification and colour

- **Classification** is the annotation's `PartOfGroup`, unless it is `None` or
  blank (ASAP's "no group"), in which case the annotation is unclassified. When
  the group is itself part of a group, the annotation's own group is used, not
  its ancestor's (author's call: it is the most specific thing the user said).
  CAMELYON's `_0`, `_1`, `_2` and `Exclusion` groups therefore arrive as
  classifications of those names, and the counting strip counts them.
- **Colour**: the annotation's `Color`, else its group's `Color`, else the
  viewer's default. `#RRGGBB`, alpha opaque.
- **Label**: the annotation's `Name`, **except ASAP's automatic names**
  `Annotation <n>`, which become an empty label (author's call). Every ASAP file
  numbers from `Annotation 0`, so those names say nothing, and the viewer
  numbers its own unlabelled rows already.
- Notes, author and timestamps: ASAP records none. Empty, and the import instant,
  as for a QuPath file.

### Identity: no ids, so ids from content

ASAP annotations carry no id. Their names are not one — every file starts at
`Annotation 0`. Each imported annotation therefore gets **a version-5 UUID made
from its content**: the format, its type, its group and its coordinates as
written, plus which repeat of that content it is (author's call). The same file
imported twice derives the same ids, so the second import finds every
annotation already present — the anti-goal. Two different files collide only on
an identical shape in an identical group, which is the same finding. The cost:
an annotation edited in ASAP and re-imported arrives as a new annotation beside
the old one, since its content — and so its id — changed. The summary's count
makes that visible.

This uses the GeoJSON import's seed mechanism (`idSeeds`, turned into UUIDs by
`app::planImport`), so nothing new is needed downstream.

### One action, two formats

The existing action becomes **`File > Import Annotations...`** (renamed from
"…from GeoJSON..."), with the dialog filter
`Annotation files (*.geojson *.json *.xml);;GeoJSON (*.geojson *.json);;ASAP XML (*.xml);;All files (*)`.
The format is chosen **by content, not by extension** (author's call): the first
non-whitespace byte after an optional BOM is `<` for XML and anything else is
handed to the GeoJSON reader. A `.json` file that is really XML, or the reverse,
is read correctly; a file that is neither fails in the reader it was handed to,
with that reader's message.

Everything after the read is shared: the worker thread, the cancellable
"Reading…" dialog, `planImport`, the summary dialog, `insertMany`, the import
command and the status message.

### Hostile and malformed files

- **No DTD processing and no entity expansion beyond XML's five built-ins.**
  Qt's `QXmlStreamReader` does not fetch external entities, and its entity
  expansion limit stops a "billion laughs" document; the reader treats any
  error the stream reports as an unreadable file.
- **Not an ASAP file** — a root other than `ASAP_Annotations`, or XML that does
  not parse — is a read failure that changes nothing, with the reader's message.
- **Deeply nested XML** is never walked recursively: the reader is a streaming
  loop over `QXmlStreamReader` tokens, so depth costs nothing.
- **Cancel** is polled once per element, as the GeoJSON reader polls it.

### Decisions, and why

- **The reader lives in the app layer, not core** (author's call). Core is
  Qt-free and has no XML parser; adding one to core for one importer is a new
  dependency, while `QXmlStreamReader` is already in Qt Core, which the app
  layer links. App is still free of widgets, so app-tests reach every line of
  it, and it runs on the worker thread as the GeoJSON reader does.
- **One result type for both readers.** `core::GeoJsonImportResult` is renamed
  to the format-neutral `core::AnnotationImportResult` (the GeoJSON function
  keeps its name), so the summary, planning and dialog code take one type. Two
  fields are added for ASAP: a count of approximated splines, and a count of
  annotations skipped for an unsupported type keyed by the type's name, which
  can share `skippedObjectTypes` — the summary then names the ASAP type the way
  it names QuPath's "detection". `provenance` stays absent: ASAP records none.
- **No new dependency.**

## 4. Where the code lives

- `src/app/include/slideio/viewer/app/AsapXmlImport.h`,
  `src/app/src/AsapXmlImport.cpp` — `asapXmlToAnnotations(std::string_view text,
  std::chrono::system_clock::time_point importedAt, const std::atomic<bool>*
  cancel = nullptr) -> core::AnnotationImportResult` (names are the builder's).
- `src/core/...AnnotationGeoJson.{h,cpp}` — the result type renamed; the shared
  helpers the ASAP reader needs (axis-aligned box detection, the vertex
  minimums, the content seed) exposed rather than copied.
- `src/ui/src/AnnotationGeoJsonImport.cpp` — the summary takes the renamed type
  and gains the spline line; the file may be renamed to match (builder's call).
- `src/ui/src/MainWindow.cpp` — the action's text and filter, and the
  format choice inside the existing worker lambda.

## 5. Tests

App-tests, against the reader:

1. Each type maps as the table says: Dot, PointSet of 4, Polygon, axis-aligned
   Rectangle → rectangle, rotated Rectangle → polygon, Spline → polygon through
   its control points plus the spline count, Measurement → open two-point line.
2. Coordinates out of `Order` are sorted; missing `Order` keeps document order.
3. `"1234,5"` reads as 1234.5; `"1,234.5"`, `"abc"`, `"nan"`, `"inf"` and an
   empty value make the annotation invalid.
4. Too few coordinates per type → invalid; an unknown type → skipped by that
   type's name.
5. `PartOfGroup` becomes the classification; `None` and blank → unclassified; a
   group nested in another uses the annotation's own group.
6. Colour precedence: annotation, then group, then default; a malformed colour
   falls through.
7. `Annotation 12` → empty label; `Tumour margin` → kept.
8. Content seeds: the same file read twice gives the same seeds; two identical
   shapes in one file get different seeds; a different group gives a different
   seed. Through `planImport`, a second import of a file adds nothing.
9. Not XML, wrong root, truncated XML, an external entity and an
   entity-expansion bomb each fail with an error and no annotations, without
   throwing.
10. A cancelled read returns `cancelled` and nothing else.
11. Accounting: annotations imported plus every skip count equals the
    `<Annotation>` elements read.
12. Greek and Cyrillic group names survive byte for byte.

Ui-tests: the summary's spline line, and its absence when there are none.

Format choice: a pure function picking the reader from the first bytes — BOM,
whitespace, `<`, `{`, `[` — tested in app or ui (builder's call).

## 6. Manual checks — group AI

Written before the run. No ASAP installation and no ASAP-written file is
available on the development machine, so the fixtures are files written to the
format above, CAMELYON-style; **a real ASAP-written file from the project owner
would make AI3 stronger**, and the run record will say which was used.

| | Check | Result |
|---|---|---|
| AI1 | File > Import Annotations... offers GeoJSON and ASAP XML files; the old GeoJSON check (AG2) still passes through the renamed action. | Pass — a QuPath GeoJSON file still summarises as before |
| AI2 | An ASAP file with one of each type on CMU-1-Small-Region: the summary counts them, notes the spline, and after Import every shape is where its coordinates say. | Pass — 8 added, the Ellipse named as skipped, the spline noted |
| AI3 | A CAMELYON-style file (Polygon annotations in `_0`, `_1`, `_2`, `Exclusion`): the classifications appear in the tally by those names, and the colours are the groups'. | Pass — see below on colour |
| AI4 | Import the same ASAP file twice: the second summary says every annotation is already in this scene. | Pass — "8 already in this scene" |
| AI5 | Undo removes the whole ASAP import in one step; it is autosaved and survives closing and reopening the slide. | Pass |
| AI6 | A `.xml` that is not ASAP, and a truncated ASAP file: a warning naming the file and the reason; nothing changes. | Pass |
| AI7 | An ASAP file with decimal-comma coordinates imports to the same places as its full-stop twin. | Pass — every saved shape identical |

**Run 2026-10-23 against the installed build of `24bd970`: 7/7 passed.** No
ASAP installation or ASAP-written file was available, so the fixtures were
written to the format in §3: one file with one annotation of each type plus an
`Ellipse`, a CAMELYON-style file, a decimal-comma twin of the first, a
truncated copy, and an SCN metadata file (`scn/mtd.xml`) as the non-ASAP XML.
The viewer was driven by UI Automation; every result was read back from the
saved annotation file with a JSON parser.

- **AI2:** each shape was saved where its coordinates put it -- the dot at
  (300.5, 300.25), the three PointSet markers, the five-vertex polygon labelled
  "Stroma patch", the rectangle (500, 500)-(800, 700), the six-vertex spline
  polygon, and the two-point measurement line -- with its group as the
  classification and its `Color`. The automatic names were not kept as labels.
- **AI3:** `_0`, `_1`, `_2` and `Exclusion` arrived as classifications and as
  rows of the counting strip. The fixture gave every annotation its own
  `Color`, which §3 ranks above its group's, so the saved colours were the
  annotations'; the group fallback is covered by the reader's unit tests.
- **AI6:** the SCN metadata file was refused as "not an ASAP annotation file"
  and the truncated file as "not valid XML: Premature end of document."
- **AI7:** the comma twin derived different ids -- its coordinate text
  differs -- so it was added, and its eight shapes were byte-for-byte the
  first file's.

**AI3 with a real CAMELYON or ASAP-written file is still open** for the
project owner.

CMU-1-Small-Region's annotation file was restored from its backup afterwards
and checked by hash.

## 7. Requirements

| Requirement | How |
|---|---|
| FR-ANN-33 — import from QuPath GeoJSON and ASAP XML | With the GeoJSON import, complete. |
| Stage 3 item 9 — import and export | Complete with this. |

## 8. Left to the builder

- Function, struct and file names, and whether the summary file is renamed.
- Where the format-choice function lives, as long as a test reaches it.
- Which core helpers are exposed for the ASAP reader, as long as none is copied.
- The exact wording of the spline line, within §3.
