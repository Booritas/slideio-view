# Annotation persistence (E1) — deferred findings and follow-ups

**Date:** 2026-10-11
**Updated:** 2026-10-12 — item 0 carries the results of the P1-P17 verification run
against `main` @ `bc3aeb6`; items 18 and 19 are new, found during it. Item 19 is
fixed in both halves (status-bar indicator, and dimmed disabled menu text app-wide).
Item 18 is fixed, 2026-10-13, widened to cover save failures and the mismatch
resolution; item 0's older check 3 is covered as a side effect. P11 and older
checks 1, 5 and 6 were verified by the project owner on 2026-10-13, leaving
only check 4 and the dialog half of check 2 unverified in item 0.
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

0. **Parts of this sub-project have still not been run against a real slide.** No
   test suite creates a `QApplication`, so no widget can be constructed in a
   test, and every implementer was barred from launching the viewer. The
   subsystem's value rests on behaviours only the running application
   exercises.

   **Verified by the user, 2026-10-11:** annotation creation and saving work.

   **Verified 2026-10-12:** the whole geometry-widening list, P1-P17 —
   **17 pass, 0 fail** once the project owner completed P11 by hand on
   2026-10-13. See "Results" at the end of this item. The six numbered checks
   immediately below are an older, separate list and are **not** covered by
   that run, except for the parts noted there.

   **Verified by the project owner, 2026-10-13:** check 1 below — the ICC
   profile change — on an Olympus VSI slide. That is the defect the whole
   design exists to prevent, and it had been the single most important
   outstanding check since this document was written. Also check 6, quitting
   without closing the slide, which exercises `closeEvent` rather than autosave
   or `closeSlide()`, and the drag-and-Delete half of check 5.

   **Still open from the six:** check 4, and the dialog half of check 2 — its
   file-unchanged half, which is the one that governs data safety, is verified.
   Checks 1, 3, 5 and 6 are now covered in full.

   Still unverified, in the order they matter:

   1. ~~**With rectangles on screen, change the slide's ICC profile.**~~ This was
      the defect that justified the whole design (foundation follow-up item 4):
      the slide reopens through `closeSlide()`, and the annotations must still
      be there. **Verified by the project owner on an Olympus VSI slide,
      2026-10-13 — passed.** The flush-before-reset ordering in `closeSlide()`
      holds in the running application, which until now had only ever been
      argued. No longer outstanding.
   2. Hand-break a saved file's JSON, reopen: a dialog names the file and the
      problem, the tool is disabled, and **the broken file is unchanged on disk
      afterwards**. This is the governing rule of the whole design, and the one
      failure that would cost a clinician their work.
   3. Change a saved file's `slideId`, reopen: the three-way mismatch dialog
      appears, and each of its three outcomes behaves as §7 describes. Note
      that "Use for This Slide" must survive a close *without drawing* — that
      was a late fix and is the shape of bug that reappears.
   4. Open an associated image (label or macro). The Rectangle tool is disabled.
   5. ~~On a read-only slide (resolve a mismatch as "Open Read-Only"), try to drag
      an annotation and to press Delete.~~ Neither must change anything, and the
      status bar should say why. **Fully covered:** the tool-gating half by
      check P12 on 2026-10-12, and the drag-and-Delete half by the project
      owner on 2026-10-13 — **passed.** This was the last path by which a user
      could still have altered a document the application had declared
      unwritable.
   6. ~~Draw, then quit the application **without closing the slide**.~~ The work
      must be on disk — that path is `closeEvent`, not autosave. **Verified by
      the project owner, 2026-10-13 — passed.** The exit path writes, and it is
      a distinct route from the autosave and `closeSlide()` paths everything
      else here exercised.

   **Polygon and freehand line, added 2026-10-12. All of P1-P17 have since been
   run — see "Results" at the end of this item; what follows describes the list as
   written, before it was executed.** Nobody had run the viewer at any point across
   the geometry-widening work
   (`2026-10-12-annotation-geometry-widening-design.md`); every implementer was
   barred from it, and the integration lives inside a widget no suite can
   construct. The run below was therefore the first execution of any of this code,
   and the list is
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

   Run once, on one slide, in this order: **P1, P2, P3, P10, P17, P9, P16, P5, P8, P15, P7, P11,
   P6, then P4, then P12, P13, P14.**

   P17 sits beside P10 because both ask what a close gesture does, and P16 after P9 because
   both end a trace deliberately — P16 last of that group, since switching tools is the one
   that throws the pending shape away.

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

   **Results — P1-P17 run in full, 2026-10-12.** 17 pass, 0 fail (P11 completed by hand
   on 2026-10-13; see its row). This was
   the first execution of any of this code. Driven through the GUI with Win32
   automation against the installed build of `main` @ `bc3aeb6`, on
   `S1303802-11-HE-DX1.svs` (brightfield H&E, as the note above requires), with a
   Zeiss CZI for the multi-scene pass and a worktree build at `e86150c` for P14. The
   saved JSON was the oracle wherever a check could be settled by it; screenshots
   only for the visual ones. No error, critical, fatal or exception line appeared in
   the session log across the whole campaign, and nothing crashed.

   | Check | Verdict | Observed |
   |-------|---------|----------|
   | P1 | pass | 2 clicks + 1 double-click produced **exactly 3 vertices**. Qt delivers `MouseButtonDblClick` in place of the second press. |
   | P2 | pass | Every vertex moved by an identical delta; no rectangle conversion. |
   | P3 | pass | Notch selects nothing, solid part selects; outline traces the edges, not a box. |
   | P4 | pass | All 5 shapes returned unchanged, including the 32-vertex polyline — exercising the hand-maintained `polyline` reader branch. |
   | P5 | pass | Focus verifiably stolen by another window; preview identical before and after. |
   | P6 | pass | Saved as `polyline`, drawn open and unfilled. |
   | P7 | pass | Click count equalled vertex count in all five trials; no jitter-trailed extras. |
   | P8 | pass | Pressing `R` mid-drag then releasing selected and deselected nothing, as §6.6 requires. |
   | P9 | pass | Both halves: Escape and Backspace. |
   | P10 | pass | All three close gestures: click vertex 1, double-click, Enter. |
   | P11 | pass | 40 drag steps produced 32 vertices, proportional and smooth, but a synthetic drag has none of a real hand's jitter — so this stood at partial until **the project owner traced it by hand, 2026-10-13, and it passed.** That hand trace is the only real evidence for this check; the automated half could not settle it. |
   | P12 | pass | See below. |
   | P13 | pass | Hint observed for both tools, and its transience observed. |
   | P14 | pass | See below. |
   | P15 | pass | Move completed; nothing beyond it. |
   | P16 | pass | 410 preview pixels cleared the instant `V` was pressed, cursor unmoved. |
   | P17 | pass | Below-minimum double-click **and** below-minimum Enter both leave the trace alive. |
   | multi-scene extra | pass | Separate `.s0`/`.s1` files, correct `sceneIndex`, per-scene dimensions. **The most clinically serious failure on this list does not occur.** |
   | backward-compat (unplanned) | pass | The CZI's `.s0` file held two rectangles written 2026-10-11 by a pre-branch build; the new build read and appended to it without damage. |

   **P1 settles the §6.3 double-click question empirically** — the claim had been
   reasoned, not observed. Had the original `pop_back` survived, that triangle would
   have fallen to two vertices and vanished silently at commit.

   **P14 in detail.** The pre-branch build refused the file with
   `unknown geometry type 'polygon'`, naming the file and stating "The file has not
   been changed. Annotations are disabled for this slide so it cannot be overwritten."
   It then wrote nothing: after selecting the Rectangle tool, dragging, waiting past
   the 2s debounce and closing (which flushes), all four annotation files were
   byte-identical by SHA-256 with unchanged mtimes. **This validates the decision not
   to bump `schemaVersion`** — the old build fails on the shape alone, not the
   version, so a staggered rollout degrades per-shape instead of refusing every
   rectangle-only file. The gate is stronger than "the service is inactive so it never
   writes": `annotationsAvailableChanged(false)` disables the tool outright, so the
   user cannot begin work that would be lost.

   **P12 in detail.** Reached the real way — the stored document's `slideId` was
   changed to a foreign value so it no longer matched the key it was filed under, the
   state a re-exported slide produces; no on-disk read-only simulation. The three-way
   dialog appeared and was resolved as "Open Read-Only"; all 9 annotations loaded and
   rendered, so this is the genuine read-only path and not an empty model. Clicking
   `Polygon`, `Freehand Line` and `Rectangle` each at its own screen coordinates left
   the checkmark on Navigate every time — **Polygon and Freehand are gated exactly as
   Rectangle is**, which the code confirms (all three get `setEnabled(available)` in
   the `annotationsAvailableChanged` handler). The break condition never occurred: no
   drawing tool could be selected at all, so no trace was possible and the inner
   "cannot be changed" guard was never reached. Read-only also wrote nothing — the
   foreign `slideId` was still on disk afterwards, never silently re-associated.

   **Caveats on method, so the next reader does not over-trust this.**

   - **P11 is the one genuinely incomplete check.** Everything else was settled by the
     saved JSON or by an unambiguous behavioural oracle.
   - **A disabled menu item is indistinguishable from an enabled one — in the
     application, not merely in a screenshot.** `Rectangle`, `Polygon`, `Freehand`
     and the definitely-enabled `Preferences...` all render at identical peak luma
     (204) whether enabled or disabled. Two attempts to measure the difference were
     dead ends, and this was first written up as a limitation of reading
     screenshots; **the project owner confirmed it is a property of the running
     application.** It is therefore a product observation, not a method note — see
     item 19. P12's verdict rests on clicking each item and reading the checkmark,
     never on appearance; anyone re-running this check must do the same, because
     looking at the menu cannot answer the question.
   - Two mid-run observations were **automation artifacts, not app faults**: an early
     Escape that appeared not to work (the harness had lost viewport focus; tool
     shortcuts are `Qt::WidgetWithChildrenShortcut` and need it, while `Ctrl+0` is
     window-level), and a "no new file" scene-0 alarm (the file already existed from a
     previous session, so "new file" was simply the wrong oracle — it had grown
     1750→2571 bytes). One trace attempt was discarded outright after the view panned
     off-slide.
   - All test annotations written during the campaign were left in place and every
     file touched was restored to its original SHA-256.

   **Still unverified after this campaign** — the numbered checks 1-6 above are a
   separate, older list and were *not* covered, except in part:

   - Checks 1, 2, 4 and 6: untouched.
   - Check 3: **now covered** (2026-10-13, while verifying the mismatch logging in
     item 18). The dialog appears and all three outcomes were driven against a
     crafted mismatch: "Open Read-Only" loads the annotations unwritable, "Ignore
     Them" leaves the file untouched, and "Use for This Slide" re-associates —
     **and survived a close with no drawing**, which is the late fix this check
     calls the shape of bug that reappears. Exercised in a scratch workspace
     rather than against a real stored file.
   - Check 5: only the tool-gating half. **Dragging an annotation and pressing Delete
     on a read-only slide were not tried**, and those are what check 5 is actually for.

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

18. **A refused annotation file leaves no trace in the session log.** Found
    while running P14, 2026-10-12. When a file is declined — a parse failure, an
    unsupported version, or an unknown geometry type — the refusal reaches the
    user as a `QMessageBox` and nothing else. `AnnotationPersistenceService` has
    no logging at all, and `MainWindow`'s `loadFailed` handler only raises the
    dialog. Once the user clicks OK, the fact that the viewer declined to load
    clinical annotation data is gone. The same is true on both sides of the
    geometry-widening branch, so this is pre-existing and not a regression.

    Listed here rather than under "User-visible" because the behaviour the user
    sees is correct and well-worded; what is missing is the record. **It may
    deserve promotion:** for a device under ISO 13485 and EU MDR, a silent-to-
    the-log refusal to load clinical data is a traceability gap, not a cosmetic
    one. One `spdlog` warn line naming the path and the parser's `problem`
    string would close it, and both values are already in hand at the emit site.

    **Closed, 2026-10-13 — and wider than written.** Four sites in `MainWindow`,
    beside the dialogs they accompany:

    | signal | level | records |
    |--------|-------|---------|
    | `loadFailed` | `warn` | file refused, left unchanged |
    | `saveFailed` | `warn` | attempt failed, retrying |
    | `saved` | `info` | saving recovered (only after a reported failure) |
    | `saveAbandoned` | **`error`** | annotations discarded |

    This item named only the refusal. `saveAbandoned` is `endSlide()` exhausting
    its retries and **dropping the annotations** — work destroyed, where a
    refusal is explicitly non-destructive — so closing the refusal alone would
    have fixed the lesser half.

    **In the ui layer, not the service.** `slideio-viewer-app` links only core
    and `Qt6::Core`, so logging where the facts originate would mean adding
    spdlog to the app layer and amending the layering table in `CLAUDE.md` — a
    structural change to add a log line. Logging beside the dialog also records
    what the user was actually told, which is the more faithful entry.

    **`saveFailed` logs inside the existing dialog latch.** Autosave retries
    every couple of seconds, so a line per attempt would bury the log on a dead
    share. One line opens the run of failures and the `saved` line closes it;
    without the latter the log ends on "save failed" and never says the writes
    came back, which reads as unsaved work long after it reached disk.

    Verified by driving the viewer (no suite can construct a widget). A
    truncated file produced `Annotations refused … (the file is not valid JSON);
    file left unchanged` and was byte-identical afterwards — which also settles
    the file-unchanged half of check 2 in item 0's older list. Against an
    unwritable workspace in a scratch directory: failed, recovered once the
    block was removed (the file was confirmed on disk), failed again — logged
    again, so the latch resets — then discarded at `error`.

    **The mismatch resolution was left out of that change as a decision rather
    than a failure, and added straight after, 2026-10-13.** Three sites in the
    resolver, each naming both slides the user judged by, the stored id, and how
    many annotations the choice decided the fate of:

    | choice | level | records |
    |--------|-------|---------|
    | `Use for This Slide` | `warn` | re-associated to this slide |
    | `Open Read-Only` | `info` | shown, not writable |
    | `Ignore Them` | `info` | not shown; file left unchanged |

    `warn` for re-association because the stored file adopts this slide's id on
    the next save and its original association is then gone — the one action in
    this subsystem that rewrites an existing file's provenance. The
    Open-Read-Only line also covers Esc and the title-bar X, which the dialog
    deliberately routes to the safe option, so a dialog that was dismissed
    rather than answered is still recorded.

    All three verified by driving the buttons against a crafted mismatch in a
    scratch workspace. **Re-association additionally survived a close with no
    drawing** — the stored id became the open slide's, the filename updated, and
    all 9 annotations were kept. That is the late fix check 3 in item 0's older
    list calls "the shape of bug that reappears", and it is now observed.

19. **A disabled menu item looks exactly like an enabled one, so the whole
    annotation gate is invisible.** Observed while running P12, 2026-10-12, and
    **confirmed independently by the project owner**: there is no visual
    difference at all. Measured peak text luma is identical (204) for the
    disabled `Rectangle`/`Polygon`/`Freehand Line` and the enabled
    `Preferences...`.

    Not a theme the application chose. `main.cpp` constructs a plain
    `QApplication` and sets no style, palette or application-wide stylesheet;
    the per-widget `setStyleSheet` calls in the panels never touch `QMenu`. The
    rendering is Qt 6.7.3's default platform style under Windows dark mode, and
    the application does nothing to distinguish the disabled state from the
    enabled one.

    **Why this matters more than it looks.** Every gate in this subsystem —
    read-only slides, associated images, an unidentifiable slide, a file that
    failed to load — is expressed to the user *only* by disabling the tool
    actions. `annotationsAvailableChanged` does show its `reason` in the status
    bar, but that message is transient. A pathologist who opens a read-only
    slide, dismisses the dialog, works for a minute and then reaches for
    Polygon sees a normal-looking menu item, clicks it, and gets silence: no
    tool, no message, no explanation. The most likely reading of that is "the
    application is broken", and the second most likely is to keep clicking.

    Qt delivers no click for a disabled action, so the fix cannot be a message
    on the attempt. Two directions: dim disabled text explicitly (a palette
    `Disabled`/`WindowText` role, or a `QMenu::item:disabled` rule), or — better,
    because it does not depend on menu styling at all — make the state
    **persistently** visible the way zoom and `sRGB` already are in the status
    bar. The second also covers the case where the user never saw the transient
    message because a `statusTip` hover replaced it, which check P13 notes is
    easy to do.

    **Partly fixed, 2026-10-12 — the second direction only.** `StatusBarManager`
    gained `setAnnotationsBlocked(bool, reason)`, built on the same
    hidden-by-default permanent-label pattern as `setColorManaged`. It shows an
    amber **"Annotations disabled"** between the magnification and colour
    readouts, carrying the gate's existing `reason` string verbatim as its
    tooltip — so the cause stays reachable instead of expiring after four
    seconds. `MainWindow`'s `annotationsAvailableChanged` handler drives it
    alongside the three `setEnabled` calls, *before* its early return, so the
    "No slide is open." emission clears it rather than leaving it stale.
    `ViewportWidget` is untouched and the signal is unchanged; the transient
    message is kept, since it is still right at the moment of the event.

    Verified manually (no suite can construct a widget): absent on a normal
    slide; present with the correct tooltip on a read-only slide **eight seconds
    after the transient expired**; cleared by File ▸ Close. All four suites pass
    and the build is warning-free.

    **Closed, 2026-10-12 — the first direction too.** `main.cpp` now sets one
    application-wide rule:

    ```cpp
    app.setStyleSheet(QStringLiteral("QMenu::item:disabled { color: #707070; }"));
    ```

    Chosen by measurement, not assumption. A throwaway build compared three
    candidates on the same menus with no slide open, where the three drawing
    tools and File ▸ Close are genuinely disabled and `Preferences...`,
    `Open Slide...` and `Exit` are enabled references in the same popups:

    | candidate | disabled text | enabled text | outcome |
    |-----------|---------------|--------------|---------|
    | unchanged | 204 | 204 | indistinguishable |
    | `QPalette` `Disabled` roles | 204 | 204 | **ignored by the style** |
    | `QMenu::item:disabled` rule | **112** | 204 | **adopted** |
    | Fusion style | dims | — | works, but restyles the whole UI |

    The active style is `windows11`, which draws menu text through the native
    theme and ignores `QPalette`'s `Disabled` roles outright — so the palette
    route, which looks like the clean one, cannot work here at all.

    **The stylesheet's blast radius was measured, not assumed**, because the
    fear that it would restyle every menu is what had deferred it. Rendered
    menus were diffed pixel-by-pixel against the unstyled build: popup sizes
    identical (Tools 156×121, File 259×174), and differing pixels confined
    exactly to the disabled rows — Tools `y=30..84`, File `y=74..84`. Checkmark,
    highlight bar, separators, submenu arrow and every enabled item are
    byte-identical. The dock panels that set their own stylesheets
    (`ChannelMixerPanel`, Scenes, Metadata) showed **0 differing pixels across
    129,000 sampled**. Four suites pass; build is warning-free.

    **Scope of that evidence:** `windows11` style, Qt 6.7.3, Windows 11 dark
    mode. The rule is inert where a platform style already dims disabled text,
    which is the usual behaviour elsewhere, but it has not been looked at on
    macOS or Linux. `#707070` was picked to be clearly dimmer while staying
    legible; it is a judgement, not a derived value.

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
