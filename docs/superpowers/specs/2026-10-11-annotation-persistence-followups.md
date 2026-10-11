# Annotation persistence (E1) — deferred findings and follow-ups

**Date:** 2026-10-11
**Status:** Open
**Covers:** `main` @ `ca38c5d` (merge of `annotation-persistence`) and `c819a54`
**Spec:** `2026-10-10-annotation-persistence-design.md`
**Plan:** `../plans/2026-10-10-annotation-persistence.md`

Every finding raised during the annotation persistence work that was
deliberately *not* fixed, with the reason and who should pick it up. Extracted
from the execution ledger before that scratch workspace was discarded; the
ledger itself is gone, so this is the record.

Findings that were fixed are in the commit messages on `main`. The whole-branch
review triaged 21 deferred Minors into 14 keep and 7 fix; the seven were fixed
before merge and are not listed here.

**What is NOT in this document:** the accepted risk on storing slide filenames
and paths in cleartext. That is a decision, not a follow-up, and it lives in
§9A of the design spec with its rationale and the routes that rationale does
not cover.

---

## Not yet verified at all

0. **Most of this sub-project has still not been run against a real slide.** No
   test suite creates a `QApplication`, so no widget can be constructed in a
   test, and every implementer was barred from launching the viewer. The
   subsystem's value rests on behaviours only the running application
   exercises.

   **Verified by the user, 2026-10-11:** annotation creation and saving work.

   Still unverified, in the order they matter:

   1. **With rectangles on screen, change the slide's ICC profile.** This is
      the defect that justified the whole design (foundation follow-up item 4):
      the slide reopens through `closeSlide()`, and the annotations must still
      be there. **The single most important outstanding check.**
   2. Hand-break a saved file's JSON, reopen: a dialog names the file and the
      problem, the tool is disabled, and **the broken file is unchanged on disk
      afterwards**. This is the governing rule of the whole design, and the one
      failure that would cost a clinician their work.
   3. Change a saved file's `slideId`, reopen: the three-way mismatch dialog
      appears, and each of its three outcomes behaves as §7 describes. Note
      that "Use for This Slide" must survive a close *without drawing* — that
      was a late fix and is the shape of bug that reappears.
   4. Open an associated image (label or macro). The Rectangle tool is disabled.
   5. On a read-only slide (resolve a mismatch as "Open Read-Only"), try to drag
      an annotation and to press Delete. Neither must change anything, and the
      status bar should say why.
   6. Draw, then quit the application **without closing the slide**. The work
      must be on disk — that path is `closeEvent`, not autosave.

---

## User-visible, worth fixing when the area is next touched

1. **`annotationEditBlocked` fires on a plain click, not just a blocked edit.**
   `ViewportWidget`. On a read-only slide, any left-press that hits an
   annotation emits it — including a click that only selects. The status bar
   then says the annotations "cannot be changed" when the user changed nothing.
   Not wrong, but mildly misleading in ordinary read-only use. Narrow it to
   fire once the drag passes the hit tolerance, or on the first move event.

2. **The save-failure dialog's escape route loops.** `MainWindow`. Esc is bound
   to **Retry**, which is non-destructive and keeps the user in control — but
   `endSlide()` bounds retries at five, so five Esc presses end in a discard.
   The dialog text states the limit, and the real escape hatch is **Choose
   Another Folder**. Consider making that clearer, or binding Esc to it (noting
   that opens a modal picker on a keystroke, which is why it was not chosen).

3. **The quit cannot be aborted.** `MainWindow::closeEvent` discards and closes
   whatever the save-failure resolver returns. This is consistent with the
   resolver's own button labels ("Discard and Close"), so it is defensible — but
   a user who cannot save has no way to stay in the application and fix the
   cause. Adding a fourth outcome is a design change, not a bug fix.

4. **A hand-edited file becomes permanently unloadable over a cosmetic field.**
   A `fillOpacity` of `1.1` or a `lineWidth` of `0` is rejected rather than
   clamped, so the whole document refuses to load and — by the governing rule —
   is never overwritten. This was a deliberate ruling, consistent with how a
   malformed colour already behaves, and the whole-branch reviewer called it
   correct *and* the item most likely to generate a support ticket. If it does,
   the fix is to clamp cosmetic fields while still rejecting structural ones.

---

## Performance / robustness

5. **The annotation file is read and written on the UI thread.**
   `ViewportWidget.cpp`, at the `beginSlide` call, and every autosave `flush()`.
   `JsonAnnotationRepository::load` is `QFileInfo::exists` plus
   `QFile::readAll`; `save` goes through `QSaveFile`. A workspace on a dead
   network share therefore stalls the UI for the filesystem timeout — once per
   slide open, and again every autosave interval.

   This is the same hazard `openScene` deliberately avoids for ICC profiles by
   moving resolution onto the worker thread ("an override pointing at an
   unreachable share would block the UI thread for the filesystem timeout").
   The asymmetry is recorded in a comment at both sites. Fixing it properly
   means moving the repository onto a worker, which is a structural change.

6. **Windows reserved device names pass the slide-id allowlist.** `CON`, `NUL`,
   `AUX`, `PRN`, `COM1`-`COM9`, `LPT1`-`LPT9` are all `[A-Za-z0-9_-]`, so a save
   to `NUL.s0.annotations.json` would silently vanish. It cannot escape the
   workspace, and it is unreachable in practice: `computeSlideId` produces 16
   hex characters and the service always rebuilds the document with its own
   computed key. A length floor would close it but would reject the short ids
   the tests use.

---

## Test gaps

Each of these is a test that is weaker than it appears. None indicates a defect
in the code it covers.

7. **The autosave pacing fix is untested.** `AnnotationPersistenceService`. On a
   failed save, `m_lastMutation` is pushed forward so retries back off to the
   debounce instead of firing every 500 ms tick. Verifying it needs a running
   timer, and no suite creates a `QCoreApplication`. Correct by inspection only.

8. **`endSlide()` with no save-failure resolver is untested.** The suppression
   of `saveFailed` during a close-time flush deliberately applies *only* when a
   resolver is installed — without one, nothing else would report the failure.
   The test covers the resolver case and an ordinary failing `flush()`, but not
   the no-resolver half that the reasoning leans on.

9. **`QSaveFile` atomicity is untested.** `JsonAnnotationRepositoryTest`. The
   "no temporary file left behind" test would pass with a plain `QFile`, and the
   "refused save leaves the previous file untouched" test does not pin the
   serialize-before-filesystem ordering — an uncommitted `QSaveFile` discards
   its temp file on destruction either way. Atomicity remains Qt's property
   rather than one this suite demonstrates. Proving it needs fault injection.

10. **`LoadStatus::Unreadable` is never exercised.** It shares a switch arm with
    `Malformed`, and producing a file that exists but cannot be opened is
    platform-specific.

11. **The `bad_alloc` catch is untested** and reports as `ParseError::InvalidField`
    rather than something distinguishable. Needs fault injection.

12. **The ISO 8601 ceiling test is vacuous on MSVC and libc++.** `Iso8601Test`.
    Its `if (!formatted.empty())` guard skips every assertion on any standard
    library whose `system_clock` reaches past year 9999 — which is both of
    those. It does real work only on libstdc++ (~2262). The companion test
    covers the other two, so the behaviour is checked; this one just overstates
    what it does. There is also no exact-boundary case (the ceiling accepted vs
    ceiling + 1 rejected), so a `>` versus `>=` slip would go unnoticed.

13. **The `QSettings` cross-session test would pass with `sync()` removed.**
    `AnnotationSettingsTest`. `QSettings`' destructor flushes, and the first
    instance goes out of scope before the second opens. It proves persistence
    through the INI file, not the flush. To cover `sync()`, keep the first
    instance alive and open a second over the same file.

14. **"The default workspace is not the log directory" is an inequality only.**
    It cannot catch a wrong production path format, and with no
    `QCoreApplication` the default resolves with a trailing separator.

15. **The getter-versus-static-resolver test is partly tautological** now that
    `AnnotationSettings::workspaceDirectory()` delegates to
    `resolveWorkspaceDirectory()`. Its companion carries the discrimination.

---

## Smaller, unowned

16. **The annotation-level `"type"` field is parsed but never validated against
    the geometry.** `AnnotationSerialization.cpp`. `"type": "polygon"` with a
    rectangle geometry loads as a rectangle, because the type is derived from
    the geometry on read. The field is inert today. The `static_assert` on the
    variant means a second geometry type cannot be added without revisiting
    this.

17. **`formatIso8601Utc`'s doc comment names only the year-9999 ceiling**, not
    the `system_clock` ceiling that `parseIso8601Utc` documents. Accurate for
    the format side — the clock ceiling binds only the parser — but the pair
    reads asymmetrically.

---

## Owned by a later sub-project

**The Stage 3 sub-project decomposition lives in
`2026-10-08-annotation-foundation-design.md`, under "Where this sits in Stage 3".**
A — domain model; B — overlay rendering; C — drawing tools and interaction;
D — undo/redo; E — model, persistence, autosave, locking; F — list, properties
and layer panels; G — measurement and calibration; H — import/export. Layers
belong to A, not to a sub-project of their own.

These are carried from `2026-10-10-annotation-foundation-followups.md` and
remain open.

- **Widening `AnnotationGeometry` spans A, B and C** — items 1, 2 and 3 of the
  foundation list, one per sub-project: `AnnotationType` and the geometry
  variant are independent constructor arguments and can disagree (**A**, the
  domain model); `paintAnnotations` has no per-type dispatch and always draws a
  bounding box (**B**, overlay rendering); the move handler rebuilds every
  annotation as a `RectangleGeometry` (**C**, drawing tools and interaction).
  All three are guarded by
  `static_assert(std::variant_size_v<core::AnnotationGeometry> == 1)`, so
  **adding a second geometry type fails to compile until they are addressed** —
  which is deliberate, the alternative being silent data corruption the first
  time someone dragged an ellipse.
- **Sub-project F (panels)** — items 7 and 8: `ViewportWidget` never connects to
  `AnnotationModel`'s signals (it repaints because it is the only mutator, which
  stops being true the moment a panel edits an annotation), and the app-layer
  model is owned by a presentation widget. Persistence did not move it; F is
  where the second consumer appears and justifies the move.
- **Sub-project E2** — advisory file locking (FR-ANN-32). Two instances editing
  one slide's annotations currently have a last-writer-wins collision.
- **Sub-project E3** — write-ahead log and incremental save, and with it
  NFR-REL-04 crash recovery. Up to one autosave interval of work is lost on a
  hard kill.
- **Sub-project H** — import and export (QuPath GeoJSON, ASAP XML, CSV),
  including the GeoJSON interchange that FR-ANN-27 originally called for as the
  native format, and NFR-SEC-03's "export for sharing" redaction, which §8 of
  the design spec was written to make implementable.
- **Sub-project G** — measurement and calibration, which is where
  `calibrationSource` (omitted from schema version 1, §3.3) arrives.

---

## Requirements defects

Carried from §11 of the design spec and still open in `documents/`:

1. **FR-ANN-27 is assigned to two different requirements** — "jump to
   next/previous annotation" (SRS §5.4) and "annotations shall be stored in JSON
   format" (§5.5).
2. **Architecture §10.4 puts `QSize` in `ISlideSource`**, inside the layer
   required to be Qt-free. The shipped header correctly does not.
3. **FR-ANN-27's "(GeoJSON-based)" should be amended** to "JSON, with GeoJSON as
   an export format". As written it mandates a lossy native format for every
   shape GeoJSON cannot express.
4. **FR-ANN-29 assumes the slide identifier alone prevents misassociation.** It
   does not, for multi-scene files — the key is (slide id, scene index).
5. **Architecture §10.4's `IAnnotationRepository` throws on failure** and takes
   only a `slideId`. Both are corrected in the shipped interface.
