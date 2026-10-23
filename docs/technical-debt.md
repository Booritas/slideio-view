# Technical debt

Known shortcomings that were looked at, judged, and deliberately left for
later. Each says what it is, where it lives, what it costs today, and what
fixing it would take, so it can be picked up without re-deriving it.

This is not the review ledger. Minor findings a review raised and nobody acted
on are indexed in `docs/superpowers/specs/2026-10-19-deferred-findings.md`;
an entry moves here when the project owner decides it is debt worth carrying
rather than a fix to make now, and that file says so beside it.

## Annotation GeoJSON export and import

### TD-1. The export's tests are narrower than its code paths

*From deferred finding 121, 2026-10-21.*

- **What:** invalid UTF-8 is tested only in an annotation's notes, and
  coordinates outside the slide only on a point marker
  (`tests/core/AnnotationGeoJsonTest.cpp`).
- **Cost today:** none observed. The label, classification and author go
  through the same `dump(..., error_handler_t::replace)` as the notes, and
  every shape's coordinates through the same `outline()` path as a point's.
- **Risk:** a later change that special-cases one field or one shape (for
  example, trimming or re-encoding the label, or clamping a polygon) would not
  be caught.
- **Fix:** extend the two tests to every text field and every geometry type.
  Small.

### TD-2. A very large QuPath detection file freezes the UI while it is skipped — RESOLVED 2026-10-23

*From deferred finding 122, 2026-10-22. Fixed 2026-10-23 with steps 1 and 2 of
the three below; step 3 was judged not worth its cost.*

- **What it was:** `MainWindow::Impl::importAnnotations` read and parsed the
  whole file on the UI thread. A QuPath export of 60 000 cells froze the
  window for its whole duration, and the parsed document was held whole only to
  be thrown away.
- **What was done:**
  1. **The read runs on a worker thread** (`std::async`) behind a window-modal
     "Reading <file>..." dialog with Cancel, shown after 400 ms, and a busy
     cursor. `core::geoJsonToAnnotations` takes an optional
     `const std::atomic<bool>*` it polls throughout, and returns a result with
     `cancelled` set.
  2. **Non-annotation features are dropped while the file is parsed.** The
     parse callback tracks which container it is in and, when a top-level
     feature completes, counts it and discards it if its `objectType` is not
     `annotation` -- so a detection export is never held whole. Nested
     `childObjects` are never dropped on their own.
- **Measured** on a generated 155 MB QuPath-style export of 60 000 cells (41-
  point outlines, nucleus geometry, 40 measurements each), with the hidden
  core-tests benchmark `"benchmark: reading a large GeoJSON file"`
  (`SLIDEIO_GEOJSON_BENCHMARK=<file>`):

  | | Reader time | Reader peak memory |
  |---|---|---|
  | Before | 13.8 s | 991 MB |
  | After | 6.0 s | 297 MB (147 MB of it the file's text) |

  In the installed viewer, the same file showed its summary after 7.0 s
  with the window answering throughout (Windows never reported it as not
  responding), and the whole process peaked at 418 MB with a slide open.
  Cancel closed the dialog within about 1.7 s and changed nothing.
- **Not done:** step 3, a streaming SAX reader that never builds a document at
  all. What remains of the 6 s is mostly nlohmann's lexing and the building of
  each feature before it is dropped; a SAX pass would remove some of that, but
  it would mean restructuring the reader around events, for a wait that now
  happens behind a dialog the user can cancel.

### TD-3. An exported ellipse comes back within a few ulps, not bit-exact

*From deferred finding 125, 2026-10-22.*

- **What:** an ellipse is exported as its 512-gon, and the import recovers the
  box from the ring's extremes (`readExportedShape` in
  `src/core/src/AnnotationGeoJson.cpp`). The extremes are `cx ± a`, which in
  floating point are not always exactly the stored corners: x = 0.1 and
  width 0.7 come back as 0.7999999999999999 instead of 0.8.
- **Cost today:** none a person could see — the error is around 1e-16 of a
  pixel. Only a comparison of the stored numbers notices, and the import spec's
  §3 says "exact", which overstates it.
- **Fix:** write the box into the export's `slideio` block (for example
  `slideio.box: [x0, y0, x1, y1]`) and prefer it on import when the type is
  ellipse, falling back to the ring's extremes for older files. Small, but it
  changes the export format, so `formatVersion` and both specs move with it.

### TD-4. Line width and fill opacity are narrowed to float before they are range-checked

*From deferred finding 126, 2026-10-22.*

- **What:** the import reads `slideio.lineWidth` and `slideio.fillOpacity` with
  `get<float>()` and only then asks `isRepresentableStyle` whether the value is
  in range (`readProperties` in `src/core/src/AnnotationGeoJson.cpp`). For a
  value outside float's range, such as 1e300, the conversion is undefined
  behaviour in standard C++.
- **Cost today:** none on the supported compilers. MSVC yields infinity, which
  the range check then rejects, and the default is used, as intended.
- **Risk:** another compiler, or an optimiser that exploits the undefined
  behaviour, could produce any value.
- **Fix:** read as `double`, check it against the serializer's range (the range
  check would have to accept a double, or the bounds be exposed), then narrow.
  Small.
