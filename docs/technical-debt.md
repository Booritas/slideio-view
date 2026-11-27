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

## Viewport rendering and built pyramid levels

### TD-5. GPU textures are released only wholesale

*From the built-pyramid-levels design (2026-11-27), section 9.*

- **What:** `ViewportWidget::Impl::textures` only grows while a slide is open.
  It is emptied wholesale, by `clearAllTextures` on a slide close, a reopen
  or a Z/T change, and by `releaseTexturesOfOtherMode` on a colour-mode
  change. `Impl::deleteTexture` exists but **has no callers**, so no single
  texture is ever evicted, and a tile evicted from the `LruTileCache` keeps
  its texture.
- **Cost today:** built levels removed the worst case. A zoomed-out frame of
  a pyramid-less slide is now about 50 textures rather than 44 000, and before
  that change one zoomed-out pass of `PIT_01_00248_01.tiff` put ~8.6 GB of
  256x256 textures on a 4 GB GPU. But a long full-resolution pan across any
  large slide still accumulates one texture per tile it has ever drawn (192 KB
  for an 8-bit RGB tile of 256x256) until the slide is closed. Nothing
  bounds it.
- **Risk:** on a small GPU a long session on one slide ends in driver paging
  or failed uploads, which `paintGL` reports as skipped tiles. That looks like
  a slow slide, not a memory problem.
- **Fix:** give the texture map an LRU of its own with a byte budget (or
  evict a texture when its tile leaves the cache), and call `deleteTexture`
  from it. The trap is that `paintGL` uses a texture uploaded earlier in the
  same frame, so eviction must never take a key in the current frame's
  visible set. Medium: one class, plus a budget to choose.

### TD-6. An unreliable source level leaves the zoomed-out view blank

*From the final review of built pyramid levels, ruling F2 (2026-11-27).*

- **What:** `infra::BuiltPyramidSource` reads its source level (the coarsest
  stored level) with mode-explicit `readBlock` calls, which do not pass
  through `SlideIOAdapter::readTile`'s fallback. That fallback reads a level
  marked unreliable from a finer level and box-filters it down, and it exists
  for SVS files whose tile-offset table is corrupt at one level. When the
  source level is marked (`addOnLevelMarkedUnreliable`), the build is
  abandoned. Every resident tile not built yet becomes an error tile, and no
  minimap overview is published.
- **Cost today:** on such a file, every zoom at which a built level is chosen
  shows error tiles where the slide was drawn before the feature, through the
  fallback. Zooming in to a stored level still works. No file in the corpus
  has been seen to trigger it.
- **Why it is this way:** the alternative was to build from the fallback's
  output, or to keep accumulating bands the level returned. A level marked
  unreliable returns data from the wrong file offsets without failing, so
  building from it can move tissue, which the design's anti-goals rank above
  showing a zoomed-out view.
- **Fix:** when the source level is marked, restart the build from the next
  finer reliable stored level. The built levels' factors are relative to the
  source level, so this means re-planning with a different source and a
  larger read (four times the pixels per step), or reading each band through
  the adapter's fallback arithmetic. Medium, and only worth doing once such a
  file is in hand to test against.
