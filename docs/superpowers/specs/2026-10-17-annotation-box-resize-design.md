# Resize handles for box-shaped annotations

**FR-ANN-17, "resize" clause.** Written 2026-10-17 against `main` @ `539ceb4`.

Lets a pathologist resize a rectangle or an ellipse by dragging a handle. This
is the last clause of FR-ANN-17: move shipped with the foundation, reshape
shipped earlier today for traced shapes, resize is what remains.

## 1. Intent

A region drawn slightly too small is currently redrawn from scratch. Eight
handles on the selected shape make it an adjustment instead.

### 1.1 Success criteria

1. Selecting a rectangle or ellipse shows eight handles; dragging one resizes
   the shape, and the committed shape is the one shown during the drag.
2. A resize is **one** undo step.
3. **No route out of the drag loses that step** — §6 enumerates all six.
4. Escape returns the shape to its pre-drag size without recording anything and
   without restamping `modifiedAt`.
5. The resized shape survives save, close and reopen unchanged.
6. A read-only slide refuses the resize and says why, exactly as it refuses a
   move.

### 1.2 Scope

**In:** eight resize handles on `RectangleGeometry` and `EllipseGeometry`, live
on selection; the drag, its undo integration and all six of its exits.

**Out:** rotation; multi-select resize; resizing a polygon by its bounding box;
Shift and Alt modifiers (§8); any change to vertex editing.

---

## 2. Handles live on selection, with no mode

A selected rectangle or ellipse paints eight handles — four corners and four
edge midpoints. A press on a handle resizes, a press on the body moves, a press
away deselects. There is no mode to enter or leave.

### 2.1 Why no mode here, when traced shapes needed one

The vertex-editing mode exists for one reason: a 400-vertex trace would paint
400 handles over the tissue and make the whole-shape move drag unreachable,
because almost any press would land within the grab tolerance of some vertex. A
box has eight handles. That argument does not transfer, and nothing else
justified the mode.

The mode also carried a real cost, paid in full today: it is a second piece of
state with its own exits, and **three independent reviews each found a
different exit that lost the undo record**. Not having a mode removes that
entire class of defect by construction rather than by vigilance.

### 2.2 The inconsistency, owned

A polygon needs a double-click before its handles appear; a rectangle does not.
Two shapes, two gestures. That is a genuine wart and it is accepted knowingly:
the alternative is either a mode a box never needed, or handles-on-selection
for traced shapes, which reintroduces the density problem the mode was built to
solve and would rework code that shipped and passed twenty-six manual checks
today.

### 2.3 Small shapes

On a shape only a few handle-widths across, the handles cover the body and the
move drag becomes hard to start. This is accepted rather than mitigated: the
same zoom that makes a shape that small also makes it too small to annotate
precisely, and the user zooms in. Named here so it is a decision rather than a
discovery.

---

## 3. The core operations

A new `core/BoxResize.h` and `.cpp`. Three of the four functions take a plain
`RectF` and know nothing about which shape they serve, so the same code resizes
a rectangle and an ellipse.

```cpp
enum class BoxHandle
{
    TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left,
};

/// The eight handle centres of `box`, in the enumerator order above.
[[nodiscard]] std::array<PointF, 8> boxHandlePositions(const RectF& box);

/// The handle within `toleranceSlideUnits` of `point`, nearest first; absent
/// when none is in range. Nearest-wins, so adjacent handles on a small shape
/// resolve deterministically.
[[nodiscard]] std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point,
                                                        double toleranceSlideUnits);

/// `box` with `handle` moved to `position`. A corner moves two edges, an edge
/// midpoint moves one, and the opposite edges do not move. The result may be
/// inverted -- see §6.4.
[[nodiscard]] RectF withBoxHandleAt(const RectF& box, BoxHandle handle, PointF position);
```

### 3.1 One guarded dispatch site

Writing the resized box back needs to know which shape it is:

```cpp
/// `geom` rebuilt around `box`, preserving its type. Absent for a shape that
/// is not box-defined.
[[nodiscard]] std::optional<AnnotationGeometry> withBoxReplaced(const AnnotationGeometry& geom,
                                                                const RectF& box);
```

Implemented with `std::visit` onto an overload set — **not** `std::get_if`.
A `get_if` chain would let a future box-shaped type fall silently through to
"not resizable", which is the shape of the `readGeometry` trap `CLAUDE.md`
warns about. With `std::visit` the compiler forces every shape to answer:
rectangle and ellipse rebuild themselves, polygon returns `nullopt`.

This is the only site in this work that dispatches over the variant.

### 3.2 Why not reuse `applyDragConstraints`

That function exists and takes a box, but it was written for a creation drag
anchored at a corner: its Shift squares about that anchor and its Alt doubles
the box. Neither is what a resize means. §8 keeps modifiers out of this round
rather than overloading a function whose semantics do not fit.

---

## 4. Routing a press

A corner handle sits on the shape's outline, so a press there already satisfies
`pressHitAnnotation` and would otherwise become a move. Resize is therefore
tested **before** `resolveDragOwner`, the same ordering the vertex router uses.

```cpp
enum class BoxResizeAction
{
    None,
    Resize,
    PassThrough,
};

[[nodiscard]] BoxResizeAction resolveBoxResizePress(Qt::MouseButton button, bool spaceHeld,
                                                    bool hitHandle);
```

Rules: a non-left button is `None`; held space is `PassThrough`, because space
pans whatever else is happening and that promise predates every mode in this
widget; a left press on a handle is `Resize`; anything else is `PassThrough`.

Pure, so `ui-tests` covers every combination without a `QApplication`.

### 4.1 A read-only slide

A resize on a slide whose annotations are not editable is refused by the same
`annotationsEditable()` gate that refuses a move, emitting
`annotationEditBlocked` so the status bar says why. Selecting still works. The
handles are still painted — they show what is selected, and hiding them would
make a read-only slide look like a different application.

---

## 5. Rendering

Eight squares at `boxHandlePositions`, painted after the selection outline,
reusing the vertex-handle code unchanged: `ui::kVertexHandleScreenPixels` for
the size, and black underlay beneath white fill, for the reason already
recorded there — brightfield H&E glass and tissue are near-white, and `paintGL`
clears the slide rect to white, so a white handle alone would vanish on the
commonest slide type.

Handles are painted only for a selected `RectangleGeometry` or
`EllipseGeometry`. A polygon's selection is unchanged.

---

## 6. The drag, its undo step, and all six exits

### 6.1 The lesson this section exists for

The drag writes the resized geometry into the model on **every mouse-move**, so
by the time any exit is reached the edit is already committed, `modifiedAt` is
already restamped and autosave will write it. Every route out is therefore a
place the undo record can be lost — and a lost record is worse than a lost
edit, because the user's next Ctrl+Z silently undoes something unrelated.

Vertex editing learned this the expensive way: three independent reviews each
found a different unhandled exit, two of them in code already "fixed" for
exactly that. The exits are enumerated here before any code is written.

### 6.2 The six exits

| Exit | What must happen |
|---|---|
| Normal release | Record the step, clear the drag state |
| Focus loss (`focusOutEvent`) | Record, then let the existing teardown run — Alt-tab and the save-failure dialog both reach this |
| Escape | Restore the pre-drag annotation, record **nothing** |
| Undo taken mid-drag | The geometry changed underneath; stop the drag rather than resize from a stale box |
| Tool change | `endToolDrag()` returns early unless `dragOwner == Tool`, so it will **not** clean this up — `setActiveTool` must |
| Slide close | `resetAnnotationState()` must clear the new members, or an annotation from one slide is restored onto the next |

All six call one `recordBoxResize()`, written that way from the start rather
than discovered one review at a time.

### 6.3 One gesture, one step

`app::AnnotationEditCommand` unchanged — its header already says property
changes and reshaping fit it. The press captures the whole annotation, the
release pushes one command through `ViewportWidget::pushGeometryCommand`, which
vertex editing added, with the description `Resize`.

A resize that ended where it began records nothing. The test for that is
`ui::moveEdit`'s `nullopt`, reused here but **not** for its return value: it
compares bounding boxes, which is exact for this gesture because a resize is
precisely a change of box. That is the opposite of vertex editing, where
`moveEdit` was the wrong test because an interior vertex can move inside an
unchanged bounding box — `core::sameVertices` exists for that case and is not
needed here.

Escape restores through `AnnotationModel::restore`, never `setGeometry`:
`setGeometry` restamps `modifiedAt`, which would leave the file claiming the
mark was last touched by a gesture whose whole purpose was to change nothing.

### 6.4 Flipping and the minimum extent

Dragging an edge past its opposite inverts the box and the resize continues —
the same freedom a creation drag has, and `core::boundingBox` already
normalises an inverted box so nothing downstream notices.

On release, `ui::withMinimumExtent` widens a collapsed axis to one screen
pixel, which is exactly what a newly drawn shape gets. Resize and creation
therefore behave identically, which is the point: both turn a drag into a box.

---

## 7. Testing

### 7.1 `core-tests`

- `boxHandlePositions`: all eight at the right places for a normalised box, and
  for one built from an inverted pair.
- `nearestBoxHandle`: finds a handle; returns the nearest when two are in
  range; empty off the handles; a negative tolerance behaves as zero.
- `withBoxHandleAt`: each of the eight handles moves the edges it owns and
  leaves the opposite ones untouched — eight cases, not one.
- Dragging a handle past the opposite edge produces an inverted box whose
  `boundingBox` is the expected normalised rectangle.
- `withBoxReplaced`: preserves `RectangleGeometry` as a rectangle and
  `EllipseGeometry` as an ellipse, and returns `nullopt` for a polygon.
- A resized rectangle and a resized ellipse each round-trip through the
  serializer unchanged.

### 7.2 `ui-tests`

- `resolveBoxResizePress` across left/middle/right, space held and not, handle
  hit and not.

### 7.3 Manual — group AA

No test binary creates a `QApplication`, so the handles, the gestures and every
exit in §6.2 are unreachable from any suite.

**Not yet run.** Group AA is the list to work through against the installed
build; results are recorded here afterwards, as groups U through Z were.

| | Check |
|---|---|
| AA1 | Select a rectangle: eight handles appear. Select an ellipse: the same. |
| AA2 | Select a polygon: no resize handles, and double-click still enters vertex editing. |
| AA3 | Drag each of the four corner handles: two edges move, the opposite two stay. |
| AA4 | Drag each of the four edge handles: one edge moves, the other three stay. |
| AA5 | Press the body, not a handle: the shape moves as before. |
| AA6 | Drag a handle past the opposite edge: the shape inverts and keeps resizing, and looks correct on release. |
| AA7 | Collapse a shape to nothing and release: it comes back one screen pixel thick, not invisible. |
| AA8 | Release after a resize: Ctrl+Z restores the previous size in **one** step. |
| AA9 | Press a handle and release without moving: no undo step is recorded (Ctrl+Z reaches the previous operation). |
| AA10 | Escape mid-resize: the shape returns to its pre-drag size, and Ctrl+Z then reaches the operation **before** the cancelled resize. |
| AA11 | **Exit:** Alt-tab away mid-resize without releasing. The edit survives and Ctrl+Z undoes it in one step — not something older. |
| AA12 | **Exit:** press a tool shortcut (V/R/E/P/F) mid-resize. Same requirement. |
| AA13 | **Exit:** press Ctrl+Z mid-resize. The drag stops; what the undo restored stands. |
| AA14 | **Exit:** close the slide mid-resize, open another, press Escape. Nothing from the previous slide is restored. |
| AA15 | Hold space and drag from over a handle: the viewport pans, the shape does not resize. |
| AA16 | On a read-only slide, drag a handle: refused, with the status-bar message, and the shape unchanged. |
| AA17 | Resize, save, close the slide, reopen: the new size is there. |
| AA18 | Resize an ellipse and check the properties panel: area and perimeter follow the new size. |
| AA19 | During a resize the cursor shows `Qt::SizeAllCursor` — the same cursor a whole-shape move and a vertex drag use — not a crosshair or arrow. Per-handle directional cursors are out of scope (§8). |
| AA20 | Grab a handle, drag, press **Ctrl+Z** while still holding the button, then release WITHOUT moving the mouse. What the undo restored must stand, and the next Ctrl+Z must reach the operation before it — not a fabricated "Resize" step. |
| AA21 | With the **Rectangle** tool active and a rectangle already selected, draw a second shape starting near one of the first shape's handles. A new shape must be created; the first must not resize. |
| AA22 | Press a handle and release without moving. Nothing is recorded (AA9) AND the slide must not become dirty — no autosave should follow. |
| AA23 | On a read-only slide, press on a handle spot and drag: the viewport must pan (and the status bar explain), not sit dead. |

---

## 8. Deliberate absences

- **Shift and Alt do nothing.** `ui::applyDragConstraints` exists but its
  semantics belong to a creation drag anchored at a corner: Shift squares about
  that anchor, Alt doubles the box. Neither is what Shift-preserves-aspect or
  Alt-resizes-about-centre would mean here. No requirement names modifiers for
  a resize, so none are invented; §6.4's flip freedom covers the gesture the
  requirement does name.
- **Rotation.** An axis-aligned box cannot express a rotated shape, so this is
  a format change, not a handle.
- **Multi-select resize**, which needs multi-select first (FR-ANN-16).
- **Per-handle directional cursors** (a diagonal arrow on a corner, a vertical
  one on a top edge). Qt provides them, but the cursor logic is a single
  precedence-ordered switch over DragOwner and eight directions would be the
  first thing in it that depends on WHICH handle is held. Not worth that for
  this round; `Qt::SizeAllCursor` matches the move and vertex drags.
- **Resizing a polygon by its bounding box.** Traced shapes have vertex
  editing; scaling 400 vertices by a box drag is a different feature with
  different arithmetic.

---

## 9. Risks and accepted trade-offs

| Risk | Disposition |
|---|---|
| An exit that loses the undo step | The reason §6.2 exists. All six route through one `recordBoxResize()`, and AA11–AA14 check four of them by hand. |
| Two gestures for two shape families | Accepted and argued in §2.2. |
| Handles cover a small shape's body | Accepted in §2.3; the zoom that makes a shape that small also makes it unannotatable. |
| No modifiers disappoints a user expecting Shift | Accepted in §8. Adding them later is additive and breaks nothing. |
| `withBoxReplaced` forgotten for a future box type | Mitigated by `std::visit` rather than `get_if` (§3.1), so it is a compile error and not a silent `nullopt`. |

---

## 10. Requirements

| Requirement | Status |
|---|---|
| FR-ANN-17 — move, resize, reshape via drag handles | **Complete** with this round: move shipped with the foundation, reshape 2026-10-17, resize here |
| FR-ANN-01 / FR-ANN-02 — rectangle and ellipse | Unchanged; this edits what they produce |
| FR-ANN-16 — multi-select | Still open, and a precondition for multi-select resize |
