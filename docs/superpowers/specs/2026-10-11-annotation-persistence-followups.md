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

   **Polygon and freehand line, added 2026-10-12 — none of these has been run.**
   Nobody has run the viewer at any point across the geometry-widening work
   (`2026-10-12-annotation-geometry-widening-design.md`); every implementer was
   barred from it, and the integration lives inside a widget no suite can
   construct. This is the first execution of any of this code, and the list is
   for the project owner. It comes from the final whole-branch review of
   `annotation-geometry` (038c016..c41b003), whose findings then produced two
   fix commits, `1bae489` and `8d2039b`; the review predates both fixes, so a
   check traced back to a defect it found may describe code that has since
   changed. Numbered P1-P15 so as not to
   collide with the checks above. Priority order; each says what a break looks
   like.

   P1. **Count the vertices in the saved JSON after closing a polygon by double-click
      — on a triangle.** For N placed points the file must hold exactly N entries.
      *Break:* a dropped vertex takes a triangle below `kMinPolygonVertices` and it
      vanishes silently at commit; an extra one is invisible on screen and permanent
      in the file. This settles the §6.3 double-click question empirically and is the
      single most valuable item here.

   P2. **Drag a committed polygon.** It keeps its shape and vertex count.
      *Break:* it snaps to a rectangle — `core::translated` is not on the move path,
      the regression foundation item 1 exists to prevent.

   P3. **Click inside the notch of a concave polygon, then inside the solid part.**
      The notch selects nothing; the solid part selects.
      *Break:* clicking the notch selects — the paint fill rule and `core::hitTest`'s
      even-odd ray casting disagree. Also check the **selection outline traces the
      polygon's edges, not a box around it**; a box means `screenPath(core::outline(...))`
      is not on the selection path.

   P4. **Close the slide and reopen it.** The polygon returns unchanged — same vertex
      count, same position over the same tissue.
      *Break:* absent (the flush-before-reset ordering broke), or visibly different —
      a lost or reordered vertex shows as one wrong corner, which at a glance reads as
      "roughly right". Compare against the saved JSON from check P1, not by eye.

   P5. **Alt-tab away mid-trace and come back.** The half-traced shape is still there,
      rubber band following the cursor again.
      *Break:* empty canvas — minutes of tracing gone with no warning. `CLAUDE.md` calls this "designed to, not yet observed"; this check is what would observe it. This is a
      deliberate asymmetry with the line directly above it in `focusOutEvent`, and the
      thing most likely to be "tidied up" by a future change.

   P6. **Trace a freehand line.** Not filled; its two ends not joined.
      *Break:* a translucent fill appears, or a straight segment joins the last point
      back to the first — Qt inventing a segment the pathologist never drew.

   P7. **Place vertices with slow, deliberate single clicks, then count them in the file.**
      *Break:* more vertices than clicks. Hand jitter between press and release can
      trail a second point, because the trail threshold (2.0 screen px) is tighter than
      the rectangle tool's misclick threshold (6.0). Cosmetic on screen, permanent in
      the record. This was deliberately left unchanged rather than tuned blind.

   P8. **Switch tools with the button still held mid-trace** — press `R` during a polygon
      drag, then release.
      *Break:* an annotation selects or deselects on that release. Per spec §6.6 a
      release under a drawing tool should do nothing.

   P9. **Escape mid-trace, and Backspace repeatedly.** Escape discards the whole pending
      shape; each Backspace drops exactly one vertex and the preview visibly shortens;
      Backspace past the first vertex ends the trace cleanly.
      *Break:* Backspace dropping two at once, or the preview not updating until the
      next mouse move.

   P10. **Close a polygon all three ways** — click vertex 1, double-click, Enter — and
       watch for the **close marker**, a filled orange dot on vertex 1 as the cursor
       comes within ~6 px.
       *Break:* the marker appears but clicking does not close (preview and press-handler
       conditions have drifted apart), or it never appears, in which case click-to-close
       is undiscoverable even though it works.

   P11. **Trace quickly with the button held.** The outline follows the cursor smoothly
       without visible corners; the file's vertex count is proportional to on-screen
       path length, not to how long you took.
       *Break:* a few long straight segments (trailing is not firing), or thousands of
       points from a short trace (the threshold is not applied in screen space).

   P12. **On a read-only slide** (resolve a slide-ID mismatch as "Open Read-Only"):
       Polygon and Freehand are greyed out exactly as Rectangle is.
       *Break:* either is clickable. There is a second, inner guard that shows the
       status-bar "cannot be changed" message after a full trace — **if you ever see
       that message, the outer gate failed and a completed outline was just lost.**

   P13. **Read the status-bar hint with Freehand selected, not just Polygon.** Both tools
       share "Click to add points, double-click or Enter to finish, Esc to cancel".
       *Notice:* it is accurate for both — they share every gesture and differ only in
       whether the path closes — but it omits click-on-vertex-1, which closes a polygon
       and does nothing for a freehand line. Judge whether that reads as misleading.
       Also notice how long it survives: it is transient, so a `statusTip` hover or any
       later message replaces it, possibly before a first trace is finished.

   P14. **A build predating this branch, shown a file containing a polygon:** it must
       refuse to load, name the geometry type, and **leave the file unchanged on disk.**
       *Break:* it loads partially, or — far worse — overwrites the file with only the
       rectangles it could parse, destroying every polygon in it. Check the modification
       timestamp, not just the contents: a silent rewrite that happens to preserve the
       text today is still the bug. This validates the decision not to bump
       `schemaVersion`; if it fails, that decision needs revisiting, not the code.

   P15. **Switch tools mid-*move*-drag** — drag a committed annotation, press a tool
       key with the button still held, then release.
       *Break:* anything beyond the move completing. The tool change leaves the
       move owner set, and the release commits the move through its own arm and
       selects nothing, so this is believed harmless. It has the same shape as the
       bug check P8 covers, which was just fixed, and nobody has seen it.

   P16. **With a half-traced polygon on screen, press `V` to switch to the pan tool.**
       The preview must disappear immediately, without any further mouse movement.
       *Break:* the orange outline stays painted until an unrelated pan or zoom.

   P17. **With the Polygon tool, click one vertex then double-click.** The trace must
       still be live — the rubber band still follows the cursor and further clicks
       still place vertices.
       *Break:* everything vanishes, no annotation, no message.

   **Order and slide state**

   Run once, on one slide, in this order: **P1, P2, P3, P10, P9, P5, P8, P15, P7, P11, P6, then P4,
   then P12, P13, P14.**

   Checks P1-P3 and P5-P11 accumulate state on one slide; check P4 (close and reopen) is the
   one that *consumes* it. By then there will be a polygon, a freehand line and a
   fast-traced outline in the model, so the reopen exercises several shapes at once.

   - **Start without existing saved annotations.** Open a slide with no annotation file,
     so check P4's reopen is unambiguous — anything that returns came from this session.
     Then reopen that slide a *second* time as a bonus: the shapes should load from disk
     and be immediately draggable and selectable, which exercises the load path rather
     than the in-memory one.

   - **A multi-scene slide is worth one extra pass, and is the most valuable extra
     available.** Draw a polygon on scene 0, switch to scene 1, draw a different one,
     switch back. Each scene's shapes must stay with their own scene. Annotation files
     are keyed by (slide ID, scene index) precisely so scene 0's coordinates are never
     drawn over scene 1's tissue. **A misassociation here is the most clinically serious
     failure on this list** — marks on the wrong tissue, looking entirely plausible. No
     single-scene slide can reach it.

   - **A read-only slide is needed only for check P12**, reached the real way: open a
     slide whose stored file mismatches and resolve the dialog as "Open Read-Only". Do
     not simulate it by making the file read-only on disk — that is a different path and
     will not tell you whether the tool-gating signal fired.

   - **Use a brightfield H&E slide** for the selection-outline half of check P3. The
     two-tone selection pen (black 3.0 solid beneath white 1.0 dashed) exists because a
     plain white dash was invisible against near-white glass and tissue. A dark-background
     fluorescence slide shows the outline clearly and tells you nothing about the case
     that pen was written for.

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
    the geometry on read. The field is inert on read. **Shifted 2026-10-12**
    (geometry-widening design §3.5): the geometry still wins, always, but the
    field is now derived from the geometry on write via `core::typeOf()`, so
    writer and reader cannot disagree about it. It remains in the file for the
    human reading it in an editor. The `static_assert` that once forced this to
    be revisited is gone; the second geometry type has been added.

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
  time someone dragged an ellipse. **Closed 2026-10-12** by the polygon and
  freehand widening; the `static_assert`s are gone, replaced by real dispatch.
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
