# Annotation foundation — deferred findings and follow-ups

**Date:** 2026-10-10
**Status:** Open
**Covers:** `main` @ `3a621c5` (merge of `annotation-foundation`)
**Spec:** `2026-10-08-annotation-foundation-design.md`

Every finding raised during the annotation foundation work that was deliberately
*not* fixed, with the reason and who should pick it up. Extracted from the
execution ledger before that scratch workspace was discarded; the ledger itself
is gone, so this is the record.

Findings that were fixed are not listed — they are in the commit messages on the
branch. Report-only inaccuracies in agent reports are omitted as irrelevant once
the reports are gone.

---

## Must be fixed before the next geometry type (sub-project C)

These are guarded by `static_assert(std::variant_size_v<core::AnnotationGeometry> == 1)`
at both sites, so **widening the variant fails to compile until they are addressed.**
That is deliberate: the alternative was silent data corruption.

1. **The move handler rebuilds every annotation as a `RectangleGeometry` from its
   bounding box.** `ViewportWidget.cpp`, `mouseMoveEvent`, `MoveAnnotation` branch.
   Correct with one geometry type; would turn an ellipse or polygon into a
   rectangle the first time a user dragged it.

2. **`paintAnnotations` has no per-type dispatch** — it always draws
   `boundingBox()` as a rectangle. `ViewportWidget.cpp`.

3. **`AnnotationType` and the geometry variant are independent constructor
   arguments**, so they can disagree. `core::Annotation`. Harmless with one type.
   Either assert agreement, or derive the type from the variant and drop the
   parameter.

---

## Must be decided in persistence (sub-project E)

4. **An ICC colour-profile change wipes every annotation on the slide.**
   Verified in code: `reopenCurrentScene()` is called from `MainWindow.cpp:409`,
   `:1246` and `:1259` — the colour-management toggle and both per-slide profile
   handlers — and reopens through `openScene`, which calls `closeSlide()`, which
   calls `resetAnnotationState()`.

   Costs nothing today because nothing persists. **Once annotations persist,
   adjusting a colour profile destroys them and the next autosave overwrites the
   saved file with an empty one.** Either `reopenCurrentScene` must preserve the
   model, or the persistence layer must treat "same slide, re-opened" as a
   non-reset. This is the single most consequential item in this document.

5. **Annotations carry no author.** `AnnotationMetadata::author` exists and is
   always empty. FR-ANN-12 requires an author and FR-USER-01 blocks annotation
   creation until a user identity is configured, but user identity is Stage 4
   item 8. Either a slice of Stage 4 moves forward or E defines an interim
   policy. Cannot be deferred past E.

6. **Rectangles are not clamped to the slide extent.** A drag in the dark area
   outside the slide creates an annotation at out-of-bounds coordinates.
   Recorded as a deliberate decision: slide-space geometry outside the extent is
   valid data, and clamping policy belongs with persistence or export rather
   than with drawing. Decide it here, and note it also closes item 9.

---

## Matters for the panels (sub-project F)

7. **`ViewportWidget` never connects to `AnnotationModel`'s signals.** It repaints
   because it is the only mutator. The moment a panel edits an annotation, the
   viewport will not repaint. `ViewportWidget.cpp` constructor.

8. **The app-layer model is owned by a presentation widget.** `ViewportWidget`
   owns the `AnnotationModel`, mirroring how it owns `ViewportController`. A
   deliberate choice, recorded so it is a decision rather than an accident — if
   panels and persistence both need the model, ownership probably moves up to
   `MainWindow`.

---

## Smaller, unowned

9. **Annotations and the draw preview are skipped when no tile is visible.**
   `ViewportWidget.cpp:3004`. Pan far enough off-slide and annotations vanish.
   Closed by item 6 if rectangles get clamped.

10. **Space held plus a motionless left click clears the selection.** Space
    yields a `Pan` drag owner, so the selection-click path runs. Harmless, but
    inconsistent with a plain pan.

11. **`AnnotationModel::remove()` emits `selectionChanged` before
    `annotationRemoved`.** A listener keyed on the removed id sees the selection
    change first. No consumer cares today.

12. **Tolerance on a degenerate rectangle is untested.** `hitTest` with a
    zero-extent box and a non-zero tolerance. Add in C.

13. **The preview rectangle repeats the committed path's screen-rect
    conversion.** `ViewportWidget.cpp`. Duplication, not a defect.

---

## Pre-existing, outside this work

14. **The window title still names the closed slide after File ▸ Close.**
    Confirmed pre-existing — no commit in this work touches `setWindowTitle`.

15. **Disabled menu items are not visually greyed.** The global stylesheet at
    `MainWindow.cpp:883` renders disabled items at full brightness, so
    `rectangleToolAction` is genuinely disabled with no slide open but does not
    look it. Affects every disabled action in the application, including the
    five colour-management ones — and a disabled item that does not look
    disabled is close to the confusion that prompted the "Clear ICC profile does
    nothing" report.

---

## Ruled unreachable, no action

- **NaN geometry inputs.** `screenToleranceToSlide` guards a non-positive scale
  and `Viewport::screenToSlide` is finite for finite input, so NaN cannot reach
  the geometry functions through the UI.

---

## Defects in the requirements, not the code

Worth fixing at source before an implementer reads those sections:

- **`FR-ANN-27` is assigned to two different requirements** —
  "jump to next/previous annotation" in `01-software-requirements-specification.md`
  §5.4, and "annotations shall be stored in JSON format" in §5.5. A duplicated
  requirement ID undermines traceability.
- **Architecture §10.4 puts `QSize` in `ISlideSource`**, inside the layer required
  to be Qt-free. The shipped header correctly does not, so it is a documentation
  defect — but it sits in the section whoever implements persistence will read.
