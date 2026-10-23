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

### TD-2. A very large QuPath detection file freezes the UI while it is skipped

*From deferred finding 122, 2026-10-22.*

- **What:** `MainWindow::Impl::importAnnotations` reads and parses the whole
  file on the UI thread. Measured by the final review: a 206 MB QuPath export of
  60 000 cells takes about 10 s, of which nlohmann's parse is about 5.5 s and
  freeing the parsed document about 2.5 s. Every cell is correctly counted as
  skipped and nothing is imported. There is no wait cursor or progress.
- **Cost today:** the window does not respond for the duration, which reads as
  a hang. Annotation files people import by hand are far smaller; detection
  exports are the case that hits it.
- **Already done:** the reader no longer copies the parsed document (that was
  about 1.2 s and doubled peak memory on this file).
- **Fix, in order of cost:**
  1. A wait cursor (`QApplication::setOverrideCursor`) around the read. Minutes.
  2. A SAX pass with nlohmann's `sax_parse` that discards non-annotation
     features as it meets them instead of building them into a DOM. Removes
     most of the parse and all of the destruction cost; the reader would need
     restructuring around events.
  3. Parse on a worker thread with a cancellable progress dialog. Needs care:
     the model and the persistence lock must not change under it.

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
