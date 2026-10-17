# Vertex editing for traced shapes

**FR-ANN-17, "reshape" clause.** Written 2026-10-17 against `main` @ `3d0be06`.

Lets a pathologist drag, insert and remove a single vertex of a polygon or
freehand line without redrawing the outline.

## 1. Intent

`2026-10-12-annotation-geometry-widening-design.md` §9 deferred this with the
clearest statement of why it matters in the project's documents:

> Vertex editing and resize handles. A drag moves the whole shape, as it does
> today. This is the one a pathologist will ask for first — having traced 400
> points, being unable to nudge one is a real gap.

Today the only remedy for one misplaced point in a 400-point trace is to delete
the annotation and trace it again. That is the gap this closes.

### 1.1 Success criteria

1. A vertex can be dragged, and the committed shape is the one shown during the
   drag.
2. A vertex can be inserted on an edge and removed from the outline.
3. One completed gesture is **one** undo step, not one per mouse-move.
4. A removal that would take the shape below what the serializer accepts is
   refused with a reason the user can read, and the shape is left alone.
5. Escape during a drag puts the vertex back without recording an edit, and
   without restamping `modifiedAt`.
6. The edited shape survives a save, a slide close and a reopen unchanged.

### 1.2 Scope

**In:** `PolygonGeometry` only — the closed polygon and the open freehand line.
An explicit editing mode, handle rendering, vertex drag, insert and remove, and
the undo integration.

**Out:** resize handles on `RectangleGeometry` and `EllipseGeometry`, which is
the agreed next round (§8); multi-vertex selection; snapping; editing an
unfinished trace; a context menu.

---

## 2. A mode, not a property of selection

`ViewportWidget` gains a vertex-editing mode holding one annotation id. While
it is set, that annotation's handles are painted and presses route through §4.
Outside it, nothing about selection or the whole-shape move drag changes.

**Entered** by double-clicking a committed polygon or polyline. **Left** by
Escape (§6.3), by a press that hits neither a handle nor the outline, by
selecting a different annotation, by a tool change, and by the slide closing.

Two cases where entry is declined, both silently:

- **A rectangle or an ellipse.** `std::get_if<PolygonGeometry>` returns null and
  the double-click does nothing. A message would be noise — the shape has no
  vertices to edit, and the user will find out by the handles not appearing.
- **A slide whose annotations are not editable.** The mode is refused by the
  same `annotationsEditable()` gate that already blocks `DragOwner::MoveAnnotation`
  (`ViewportWidget.cpp`, the press handler). A read-only slide must not offer an
  edit it will then refuse, and the existing `annotationEditBlocked` path already
  tells the user why they cannot change annotations here.

### 2.1 Why a mode rather than handles on selection

Handles live on every selected annotation would make the whole-shape move drag
nearly unreachable on a dense trace: almost any press would land within the
six-pixel grab tolerance of some vertex. The mode keeps "move the shape" and
"reshape the shape" separable gestures, and `documents/02-user-interface-design.md`
already specifies the entry gesture — "double-click the polygon to enter vertex
editing mode (add, remove, or drag vertices)".

### 2.2 The double-click collision is not one

Double-click already closes a pending multi-click shape
(`ViewportWidget::mouseDoubleClickEvent`). That path is guarded by
`m_impl->pending.active`, which is true only while a trace is unfinished.
Entering vertex editing applies only to a **committed** annotation, so the two
never contend: a double-click with a pending shape closes it, a double-click
without one may enter the mode. No gesture is overloaded and no precedence rule
is needed.

---

## 3. The core operations

Five pure functions in `core`, over `PolygonGeometry` **directly** rather than
over `AnnotationGeometry`.

```cpp
/// Index of the vertex within `toleranceSlideUnits` of `point`. When several
/// are in range the NEAREST wins, which is what makes a dense trace editable
/// at any zoom without hiding anything. Absent when none is in range.
[[nodiscard]] std::optional<std::size_t> nearestVertex(const PolygonGeometry& poly,
                                                       PointF point,
                                                       double toleranceSlideUnits);

/// Where an insertion would go: the index of the segment's FIRST vertex, and
/// the point on that segment nearest `point`. The new vertex belongs at
/// afterIndex + 1. For a closed shape the closing segment is included.
struct EdgeHit
{
    std::size_t afterIndex;
    PointF position;
};
[[nodiscard]] std::optional<EdgeHit> nearestEdge(const PolygonGeometry& poly, PointF point,
                                                 double toleranceSlideUnits);

[[nodiscard]] PolygonGeometry withVertexMoved(const PolygonGeometry& poly, std::size_t index,
                                              PointF position);

[[nodiscard]] PolygonGeometry withVertexInserted(const PolygonGeometry& poly,
                                                 std::size_t afterIndex, PointF position);

/// Absent when removal would take the shape below the serializer's minimum.
[[nodiscard]] std::optional<PolygonGeometry> withVertexRemoved(const PolygonGeometry& poly,
                                                               std::size_t index);
```

### 3.1 Why not over the variant

Vertex editing is meaningful only for a traced shape. Dispatching these over
`AnnotationGeometry` would fire the ten compile-guarded sites recorded in
`CLAUDE.md` and force a rectangle and an ellipse to answer questions that have
no answer for them. Taking `PolygonGeometry` directly keeps the variant
untouched by this work; a caller reaches it with
`std::get_if<PolygonGeometry>`, and a null result means the annotation is not
one this mode applies to.

### 3.2 These read `vertices`, never `outline()`

**This is the trap in this design.** `core::outline()` drops consecutive
duplicate vertices as of 2026-10-17 (see the geometry-widening spec §6.11), so
an outline index no longer corresponds to a stored index. Every function above
addresses `poly.vertices`, and §5 paints handles from `poly.vertices` too.
Painting from the outline while editing stored indices would move the wrong
point on any shape that still carries a duplicate — including every polygon
saved before that fix.

### 3.3 The removal floor

`withVertexRemoved` refuses below `kMinPolygonVertices` for a closed shape and
`kMinPolylineVertices` for an open one — the same constants the serializer
enforces on read and write. The tool therefore cannot produce a shape the file
format would reject, which is the rule `ui::pendingShapeCommits` already
follows for the drawing tools.

### 3.4 A vertex press beats an edge press

When a press is within tolerance of both a vertex and an edge, the vertex wins:
the user aimed at a handle they can see. §4 tests `nearestVertex` first and
only asks `nearestEdge` when it comes back empty.

---

## 4. Routing a press while editing

One pure function in `ui`, beside the existing routing predicates:

```cpp
enum class VertexEditAction
{
    None,
    DragVertex,
    InsertAndDrag,
    RemoveVertex,
    Exit,
};

[[nodiscard]] VertexEditAction resolveVertexEditPress(Qt::MouseButton button,
                                                      Qt::KeyboardModifiers modifiers,
                                                      bool hitVertex, bool hitEdge);
```

Separate from `resolveDragOwner` rather than added to it. That function decides
who owns a drag in the ordinary viewport; vertex editing is a distinct state
with its own rules, and widening the existing signature would make one set of
rules serve two unrelated situations. Both stay pure, so both stay covered by
`ui-tests`.

Rules, in order: a non-left button owns nothing; Ctrl with a vertex hit
removes; a vertex hit drags; an edge hit inserts and drags the new vertex; a
press that hits neither leaves the mode.

### 4.1 The gestures

| Gesture | Effect |
|---|---|
| Drag a handle | Moves that vertex |
| Press on the outline between handles | Inserts a vertex there and drags it immediately |
| **Ctrl**+click a handle | Removes that vertex |
| Escape | Cancels a live drag; with no drag live, leaves the mode |
| Press away from the shape | Leaves the mode |

**Insert-and-drag rather than insert-then-release.** The user pressed at a
point on the edge because that is roughly where they want the new vertex; going
straight into a drag lets them place it exactly in the same gesture, and
releasing without moving simply leaves it where it was inserted.

**Ctrl, not Alt.** Alt is the draw-from-centre modifier, and — established
during the ellipse round's group Y — a lone Alt may hand keyboard focus to the
menu bar on Windows. Binding a destructive edit to Alt+click would walk into
that. Right-click was the alternative and is deliberately left free for a
future context menu. `documents/02-user-interface-design.md` §6.6 reserves
Ctrl+**drag** for duplicate-and-move, which does not collide with Ctrl+click.

### 4.2 Refusing a removal

A removal `withVertexRemoved` declines produces a transient status-bar message
naming the minimum — `StatusBarManager::showTransientMessage`, the mechanism
the cleared-profile acknowledgements already use. Not a modal: this is a
frequent, cheap, non-destructive action, and a dialog for it would be
punishment. The shape is left exactly as it was.

---

## 5. Rendering

Handles are small squares centred on each vertex of `poly.vertices`, at a fixed
screen size so they stay grabbable at every zoom, **culled to the visible
viewport**.

Two-tone, black underlay beneath white, exactly as the selection outline is and
for the same reason recorded there: brightfield H&E glass and tissue are
near-white and `paintGL` clears the slide rect to white, so a white handle alone
would vanish on the commonest slide type.

### 5.1 Density is shown, not hidden

Every vertex in view gets a handle. On a 400-point trace at low zoom they
overlap into a dense band, which is honest: what is painted is exactly what
exists and exactly what can be grabbed, and `nearestVertex` makes which one
deterministic. Thinning the handles would mean either vertices that exist but
cannot be grabbed, or handles that can be grabbed but cannot be seen — both
break the rule that the painted and the interactive must agree. A user who
wants to edit precisely zooms in, which is what they do anyway.

---

## 6. Undo, cancel and autosave

### 6.1 One gesture, one command

`app::AnnotationEditCommand` takes the whole annotation before and after. Its
own header already anticipates this work — "Property changes, reshaping and
vertex editing fit it unchanged when they arrive" — so no new command type is
needed.

The press captures the original annotation; the release pushes one command.
**Never one per mouse-move**: a single nudge would otherwise bury fifty steps
of history and Ctrl+Z would appear not to work. This is the same press-captures,
release-records shape the whole-shape move drag uses.

An insert-and-drag records one command covering both the insertion and the
placement, because the user performed one gesture. A removal records one
command.

### 6.2 A drag that moved nothing records nothing

As with the move drag, a gesture that ends where it began pushes no command and
does not touch the model. Touching it would mark the slide dirty and restamp
`modifiedAt` for an edit that did not happen, and the autosave would then write
exactly that.

### 6.3 Escape

Escape joins the existing cascade in `keyPressEvent`, which already handles a
pending trace, a live draw drag and a live move drag in that order. A live
vertex drag is inserted beside the move case: the annotation goes back via
`AnnotationModel::restore`, **not** `setGeometry`.

That distinction is load-bearing and already documented at the move site:
`setGeometry` restamps `modifiedAt`, which would leave the file saying the mark
was last touched by a gesture whose whole purpose was to change nothing. With
no drag live, Escape leaves the mode instead.

### 6.4 Autosave

A completed edit changes geometry through `AnnotationModel`, which marks the
document dirty exactly as a move does. No autosave change is needed.

---

## 7. Testing

### 7.1 `core-tests`

- `nearestVertex`: finds a vertex in tolerance; returns the **nearest** when
  several are in range; empty when none is; a negative tolerance behaves as
  zero.
- `nearestEdge`: finds the segment and the projected point; includes the
  closing segment of a closed shape and excludes it for an open one; empty when
  the press is off the outline.
- A press in tolerance of both a vertex and an edge is answered by
  `nearestVertex` first (§3.4), asserted at the call-ordering level the ui
  routing uses.
- `withVertexMoved`: moves the addressed vertex and no other; preserves
  `closed` and the vertex count.
- `withVertexInserted`: the new vertex lands at `afterIndex + 1`; count grows
  by one; `closed` preserved; insertion after the last vertex of a closed shape
  works.
- `withVertexRemoved`: removes the addressed vertex; **refuses** at the floor
  for a closed shape (3) and an open one (2); preserves `closed`.
- Each operation's result round-trips through the serializer unchanged.
- A polygon still carrying a duplicate vertex — as saved files do — is
  addressed by stored index, not outline index (§3.2).

### 7.2 `ui-tests`

- `resolveVertexEditPress` across button, Ctrl, and the four combinations of
  vertex-hit and edge-hit, including the vertex-beats-edge case and the
  non-left-button case.

### 7.3 Manual — group Z

No test binary creates a `QApplication`, so the mode, the handles and the
gestures are unreachable from any suite. Group Z, to be run against the
installed build and recorded here as groups U through Y were.

**Not yet run.** Group Z is the list to work through against the installed
build; results are recorded here afterwards.

**One behaviour added during implementation, not in the design above:** held
space still pans while the mode is on. The vertex router is skipped entirely
when space is down, so a press over a handle pans rather than dragging the
vertex. `resolveDragOwner` has promised "held space pans, whatever the tool"
since before this mode existed, and it is the user's only way to move around
without leaving what they are doing. Checked by Z18.

| | Check |
|---|---|
| Z1 | Double-click a committed polygon: handles appear on every vertex. |
| Z2 | Double-click while tracing an unfinished shape: it closes the shape as before, and does not enter the mode. |
| Z3 | Drag a handle: that vertex follows the cursor and no other moves. |
| Z4 | Release: the shape keeps the dragged position; Ctrl+Z restores it in **one** step. |
| Z5 | Press on the outline between two handles: a vertex is inserted there and drags with the cursor. |
| Z6 | Ctrl+click a handle: that vertex is removed. |
| Z7 | Ctrl+click down to three vertices on a closed shape, then once more: refused, with a status-bar message naming the minimum, and the shape unchanged. |
| Z8 | The same on an open freehand line, where the floor is two. |
| Z9 | Escape during a vertex drag: the vertex returns to where it was picked up, and Ctrl+Z does **not** then undo an unrelated earlier edit. |
| Z10 | Escape with no drag live: the mode ends and the handles disappear. |
| Z11 | Press away from the shape: the mode ends. |
| Z12 | Enter the mode on a dense trace at low zoom: handles overlap but a grab takes the nearest vertex, and zooming in separates them. |
| Z13 | Edit, then save, close the slide and reopen: the edited shape returns unchanged. |
| Z14 | A whole-shape move still works after leaving the mode. |
| Z15 | Double-click a rectangle and an ellipse: nothing happens, no handles, no message. |
| Z16 | On a read-only slide, double-click a polygon: the mode is not entered. |
| Z17 | Enter the mode on a polygon saved **before** 2026-10-17 — one that still carries a duplicate final vertex — and drag the last few handles. The vertex that moves must be the one grabbed (§3.2). |
| Z18 | In the mode, hold **space** and drag from over a handle. The viewport must pan; the vertex must not move. Release space and the same drag must move the vertex. |
| Z19 | Start a vertex drag and **Alt-tab away without releasing**. The edit must survive AND Ctrl+Z must undo it in one step — not undo some earlier, unrelated operation. |
| Z20 | Press a handle slightly off-centre (a few pixels) and move the mouse one pixel. The vertex must move one pixel, not jump to the cursor. At low zoom especially. |
| Z21 | With the Rectangle or Ellipse tool active, double-click an existing polygon: the mode must NOT be entered, and drawing must still work near that polygon. |
| Z22 | While editing polygon A, select polygon B **in the annotation list panel**: A's handles must disappear. Repeat by holding space and clicking B in the viewport. |
| Z23 | Pan so a vertex sits just outside the viewport edge, then press near it: either a handle is visible there, or nothing is grabbed — never an invisible grab. |
| Z24 | While holding a vertex drag, press **Ctrl+Z**. The drag must stop rather than continue moving a vertex; whatever the undo restored must stand. |
| Z25 | While holding a vertex drag, press a tool shortcut (V, R, E, P or F). The edit must survive AND Ctrl+Z must undo it in one step. |
| Z26 | During a vertex drag the cursor must show the move cursor, not a crosshair or arrow. |

---

## 8. Deliberate absences

- **Resize handles for rectangle and ellipse.** The agreed next round. A box
  has corners rather than vertices, each constrained, and resizing it is a
  different interaction that should reuse this handle framework rather than
  contort it.
- **Multi-vertex selection and rubber-band vertex selection.**
- **Snapping, smoothing and simplification.** A trace is a clinical record;
  §9 of the geometry-widening spec rules out resampling someone's outline.
- **Editing an unfinished trace.** Backspace already removes the last pending
  vertex.
- **A context menu**, which is what right-click is being kept free for.

---

## 9. Risks and accepted trade-offs

**A vertex drag has no movement threshold, deliberately (decided 2026-10-17).**
`DragOwner::MoveAnnotation` carries `moveExceedsDragThreshold` so that a click
meant only to select a shape does not creep it. A vertex handle has no such
second meaning: pressing one can only mean dragging it, so there is no gesture
a threshold would protect. A threshold would also contradict check Z20, which
requires one pixel of pointer travel to move the vertex one pixel — and being
able to nudge a single point by a single pixel is the whole reason this feature
exists.

What remains is that a press carrying a pixel or two of hand tremor records a
small edit. Three things bound it: a press with no mouse-move at all writes
nothing, because `mouseMoveEvent` never fires and `sameVertices` then reports no
change; the drag applies a delta rather than snapping, so the edit is exactly
as large as the tremor; and it is one undo step away. Accepted on that basis
rather than by omission.

| Risk | Disposition |
|---|---|
| Handles obscure tissue on a dense trace | Accepted and shown rather than hidden (§5.1). The mode is entered deliberately and left with one key. |
| Ctrl+click is not a discoverable removal gesture | Accepted. Alt is unsafe (§4.1) and right-click is reserved. The status tip and the help text name it. |
| Insert-and-drag surprises a user who meant only to insert | Accepted: releasing without moving leaves the vertex where it was inserted, so the gesture degrades to a plain insert. |
| A press addresses a stored index while the painter uses an outline index | Mitigated by §3.2 and pinned by a test and by Z17. This is the one defect that would move the wrong point. |
| Mode state adds a fourth drag situation to route | Contained in one pure function (§4) rather than spread through `mousePressEvent`. |

---

## 10. Requirements

| Requirement | Status |
|---|---|
| FR-ANN-17 — move, resize, reshape via drag handles | **Reshape closed** for traced shapes; move already shipped; **resize still open** for box shapes |
| FR-ANN-03 / FR-ANN-04 — polygon and freehand line | Unchanged; this edits what they produce |
