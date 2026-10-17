# Annotation geometry widening — polygon and freehand line

**Date:** 2026-10-12
**Status:** Approved, not implemented
**Covers:** `main` @ `e86150c`
**Sub-projects:** A (domain model), B (overlay rendering), C (drawing tools and interaction)
**Predecessors:** `2026-10-08-annotation-foundation-design.md`,
`2026-10-10-annotation-persistence-design.md`
**Open items this closes or moves:** `2026-10-10-annotation-foundation-followups.md`
items 1, 2, 3 and 12; `2026-10-11-annotation-persistence-followups.md` item 16

---

## 1. Intent

Pathologists outline irregular tissue. A rectangle cannot describe a tumour
boundary, a region of necrosis or an invasive front, so every annotation drawn
today is an approximation of something the user actually meant to trace. This
increment adds the N-point shapes — **polygon** (FR-ANN-03) and **freehand
line** (FR-ANN-04) — which is the point of the annotation subsystem rather than
an enhancement to it.

Rectangles shipped first because they prove the transform, the persistence
round trip and the selection model with the simplest possible geometry. That
work is done and verified. What it left behind is a deliberate tripwire: five
`static_assert(std::variant_size_v<AnnotationGeometry> == 1)` sites across three
files, placed so that adding a geometry type **fails to compile** until every
place that fakes per-type behaviour is fixed. This design retires all five by
replacing them with real dispatch.

### 1.1 Why polygon and not ellipse

Ellipse is less work — it is two points, like a rectangle, and needs none of
§6. It is also not what the clinical need asks for. Taking the multi-click
interaction head-on is the expensive part of this subsystem, and doing it now
means the freehand line falls out of the same machinery for almost nothing:
the same geometry type, open instead of closed.

### 1.2 Success criteria

1. A pathologist can trace an irregular tissue boundary, close it, and see the
   shape they drew.
2. That shape survives a save, a slide close, a reopen and an ICC profile
   change, unchanged.
3. Moving it translates it; it does not become a rectangle.
4. Clicking inside a concave region that lies outside the polygon does not
   select it.
5. A build of the viewer that predates this work, shown a file containing a
   polygon, refuses to load it and **leaves it untouched on disk**.

---

## 2. Domain model (sub-project A)

### 2.1 One geometry type for both shapes

```cpp
struct PolygonGeometry
{
    std::vector<PointF> vertices;
    bool closed = true;    // false: an open path (FR-ANN-04 freehand line)
};

using AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry>;
```

A polygon and a freehand line differ only in whether the path closes. The
vertices, the bounding box, the translation and the edge hit-test are
identical; only the closing segment, the fill and the interior test differ.
Two variant alternatives would duplicate all of that to express one boolean.

### 2.2 `AnnotationType` is derived, not supplied

```cpp
[[nodiscard]] AnnotationType typeOf(const AnnotationGeometry& geom);
```

`Annotation`'s constructors lose their `AnnotationType` parameter; `type()`
delegates to `typeOf(m_geometry)` or caches it at construction. `AnnotationType`
gains `Polygon` and `Polyline`.

This closes foundation follow-up item 3, which proposed exactly this and noted
the disagreement was harmless with one geometry type. It stops being harmless
here. Without it the same fact would live in three places — the variant
alternative, the `closed` flag, and `AnnotationType::Polygon` versus
`::Polyline` — giving two independent ways for an annotation to lie about what
it is.

`AnnotationModel::add` loses the parameter with it:

```cpp
std::string add(core::AnnotationGeometry geometry);   // was (AnnotationType, AnnotationGeometry)
```

Its two call sites are the rectangle release handler and §6.5's commit. The
loading path, `replaceAll()`, is unaffected — it carries whole `Annotation`
objects.

It also makes an existing comment true. `AnnotationSerialization.cpp:348`
already reads:

> The type is derived from the geometry rather than read: the two are
> independent in the constructor and a file must not be able to make them
> disagree.

The line below it passes a hardcoded `AnnotationType::Rectangle`. The comment
describes an intent the constructor cannot currently express.

### 2.3 Validity rules

Enforced in one place, shared by the writer and the reader:

| Rule | Value |
|---|---|
| Minimum vertices, closed | 3 (`kMinPolygonVertices`) |
| Minimum vertices, open | 2 (`kMinPolylineVertices`) |
| Every vertex finite | `std::isfinite` on both components |

This follows the pattern `kMinLineWidth`/`kMaxLineWidth` already establishes in
`AnnotationSerialization.cpp`, written because a value one side accepts and the
other rejects means the application produced a file it cannot open.

**Self-intersecting polygons are valid.** Pathologists produce them by accident
when tracing quickly. Silently rejecting or repairing someone's outline is
worse than rendering what they drew; even-odd fill handles it (§4.3).

With the vertex minimums above, an empty vertex vector is unrepresentable, so
`boundingBox` needs no degenerate-case rule.

### 2.4 The free functions

All dispatch by `std::visit` and all live in `AnnotationGeometry.h`, beside the
existing pair:

```cpp
[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);
[[nodiscard]] bool hitTest(const AnnotationGeometry& geom, PointF point,
                           double toleranceSlideUnits);
[[nodiscard]] AnnotationType typeOf(const AnnotationGeometry& geom);
[[nodiscard]] Outline outline(const AnnotationGeometry& geom);          // §4.1
[[nodiscard]] AnnotationGeometry translated(const AnnotationGeometry& geom,
                                            double dx, double dy);      // §5.1
```

`boundingBox` over a polygon is min/max across vertices.

`hitTest`:

- **Rectangle** — unchanged.
- **Closed polygon** — point-in-polygon by ray casting with the even-odd rule,
  *or* within `toleranceSlideUnits` of any edge. Both, so that a thin outline
  stays clickable and a filled interior is clickable too, which is what the
  existing doc comment requires.
- **Open path** — edge proximity only. A polyline has no interior.

Even-odd is chosen over winding because §4.3 must paint the same interior the
hit-test reports, and `Qt::OddEvenFill` is what `QPainterPath` gives by default.

---

## 3. File format

### 3.1 JSON shape

```json
{ "type": "polygon",  "vertices": [ {"x": 1.5, "y": 2.0}, ... ] }
{ "type": "polyline", "vertices": [ {"x": 1.5, "y": 2.0}, ... ] }
```

Two geometry type names rather than a `"closed"` field. `readGeometry` already
dispatches on a type string, so two names keep that a single switch and keep
the file self-describing for the text-editor reader this format was chosen for.
The `closed` bool exists in memory because the variant cannot hold two
alternatives of identical layout; it does not need to exist on disk.

### 3.2 `schemaVersion` stays at 1

This is a decision, and the reflex to bump it is wrong here.

Bumping costs something specific. §3.4 of the persistence design refuses a
higher version **outright**, so the moment one workstation updates, every
shipped build refuses *every* file that workstation touches — including files
containing nothing but rectangles. Across a department on a staggered rollout
that is a large number of unopenable annotation files, for a change that
affected none of their contents.

Not bumping degrades precisely instead. An old build meets a polygon,
`readGeometry` fails, the document refuses to load, and — by the governing rule
— is never overwritten. The work is intact; the old build simply cannot display
it. Files of rectangles keep loading everywhere.

Forward compatibility was never promised. Backward compatibility is what
`schemaVersion` protects, and nothing here breaks it.

**Amendment:** the failure message must earn its keep. `unknown geometry type
'polygon'` is a puzzle; `unknown geometry type 'polygon' — this file may have
been written by a newer version of the viewer` is an instruction. One string.

### 3.3 Validity on read and write

`isRepresentable(const AnnotationGeometry&)` gains a `std::visit` arm applying
§2.3 exactly: vertex count at or above the minimum for the path's closedness,
every vertex finite. The parser applies the same constants.

### 3.4 No maximum vertex count

A dense freehand trace is legitimate data, and a cap in the serializer would
reject a clinician's outline for being careful. Density control belongs in the
drawing tool as a minimum-distance threshold between dropped points (§6.4).

This follows the ruling already made on clamping (foundation item 6): geometry
policy lives with the tool, not the serializer.

"Could a hostile file claim ten million vertices?" is a fair question and the
answer is that nlohmann has already parsed and allocated them before
`readGeometry` is reached. A cap here would not be the place that defends
against it.

### 3.5 The annotation-level `"type"` field

Persistence follow-up item 16 noted this field is written but never validated
against the geometry on read. That does not change: the geometry still wins,
always. What changes is that the field is now genuinely *derived* from the
geometry on write (§2.2), so writer and reader can no longer disagree about it.
The field remains for the human reading the file in an editor.

---

## 4. Overlay rendering (sub-project B)

Closes foundation follow-up item 2.

### 4.1 The dispatch lives in `core`

```cpp
struct Outline
{
    std::vector<PointF> points;
    bool closed;
};
[[nodiscard]] Outline outline(const AnnotationGeometry& geom);
```

Rectangle → four corners, closed. Polygon → its vertices, closed. Polyline →
its vertices, open. `paintAnnotations` builds a `QPainterPath` from an `Outline`
in one loop and never names a geometry type.

This is the pattern CLAUDE.md records for `ColorProfilePolicy`,
`AnnotationInteraction` and `DriverFilters`, and here it earns its keep twice:
no test suite creates a `QApplication`, so **anything left inside
`ViewportWidget` cannot be tested at all**, while `core-tests` is the one suite
safe to run directly.

### 4.2 The selection outline follows the shape

`paintAnnotations` draws `screenRect(annotation.boundingBox())` in **two**
places — the annotation loop and the selection highlight. Both become path
draws.

A bounding box is correct for a rectangle by coincidence. Around a traced
polygon it says "something in this region is selected"; around a thin diagonal
polyline it is a large box containing a line. The selection highlight keeps its
existing two-tone pen — black 3.0 solid beneath white 1.0 dashed — which the
code comment records was chosen because brightfield H&E glass and tissue are
near-white and a plain white dash was invisible on the commonest slide type.
That reasoning is unaffected.

### 4.3 Fill rule must match `hitTest`

§2.4 hit-tests a closed polygon by even-odd ray casting. The painted path must
use `Qt::OddEvenFill` or a self-intersecting trace will be *painted* solid
where it *tests* as outside. This is `QPainterPath`'s default, so it costs one
explicit `setFillRule` call and a comment — insurance against the two drifting
apart, which is the kind of divergence nobody notices until a user clicks
inside a shape and nothing selects.

### 4.4 An open path is never filled

Whatever `fillOpacity` says. Qt would implicitly close the path to fill it,
inventing a segment the user never drew. `fillOpacity` is inapplicable to a
polyline, not an error — it is neither rejected on load nor reset on save.

### 4.5 Round joins and caps

A hand trace is hundreds of short segments meeting at sharp angles, and the
default miter join throws long spikes at acute corners. `Qt::RoundJoin` and
`Qt::RoundCap`.

### 4.6 One performance measure

Skip an annotation whose screen bounding box does not intersect the widget
rect: two transforms to decide, against transforming every vertex of a trace
that is not visible. A pathologist at 40× on a slide carrying a hundred
outlines is the normal case, not the stress case.

Nothing further. Decimating dense traces when zoomed out is the obvious next
lever and `outline()` is the single place it would go, but there is no profile
to justify it yet.

This is per-annotation culling and is **not** foundation item 9 — the separate
bug where annotations vanish when no tile is visible — which remains open.

### 4.7 Scope

§4 covers committed annotations. The in-progress preview block at the end of
`paintAnnotations` is interaction state and is specified in §6.7.

---

## 5. Moving an annotation (sub-project C)

Closes foundation follow-up item 1.

### 5.1 The translation moves to `core`

```cpp
[[nodiscard]] AnnotationGeometry translated(const AnnotationGeometry& geom,
                                            double dx, double dy);
```

`std::visit`: a rectangle translates both corners; a polygon translates every
vertex and preserves `closed`.

`translatedBox` is **removed** from `AnnotationInteraction.h`, taking with it
the only place the UI layer manufactures a geometry out of a bounding box —
which is precisely the defect. Its tests move to `core-tests`.

### 5.2 The drag stops accumulating

Today `mouseMoveEvent` takes an incremental delta from `moveLastSlide`, rewrites
that member each event, and mutates the stored geometry in place. With one
geometry rebuilt from a box this is harmless. With a 3000-vertex trace, every
mouse-move event reads 3000 vertices, adds to each and writes them back — from
a source that is itself the previous event's output.

`moveOriginalGeometry` is already captured at press
(`ViewportWidget.cpp:3496`) and is already what Escape restores (`:3723`). The
handler should use it:

```cpp
const double dx = slideX - m_impl->movePressSlide.x;   // press, not last
const double dy = slideY - m_impl->movePressSlide.y;
m_impl->annotations->setGeometry(m_impl->movingId,
                                 core::translated(m_impl->moveOriginalGeometry, dx, dy));
```

`moveLastSlide` becomes `movePressSlide` and is written once, at press. Every
frame is then one translation applied to a pristine original rather than the
five-hundredth in a chain; the drag path and the Escape path read the same
single source of truth; and the state sub-project D's undo stack will want is
already the state the drag uses.

### 5.3 Cost, accepted

This remains O(vertices) per mouse-move event. A cheaper design — hold an
offset and apply it once at release — would make `setGeometry` stop reflecting
what is on screen mid-drag, and §4 paints from the model. Buying frames by
letting the model lie is the wrong trade in a subsystem whose governing rule is
that we do not write what we cannot read back.

### 5.4 Unchanged

`dragExceedsMinimumSize` and `withMinimumExtent` stay rectangle-shaped. They
answer "was this drag a deliberate shape", which for a multi-click polygon is a
different question with a different answer (§6.5).

---

## 6. Multi-click interaction (sub-project C)

### 6.1 The structural break

Every piece of in-flight drawing state today is bounded by one press/release
pair: `mousePressEvent` early-returns while `dragOwner != None`, and `drawing`
cannot outlive a release. A polygon lives across *many* press/release pairs,
with `dragOwner` back at `None` between them. The pending shape must therefore
be state in its own right:

```cpp
struct PendingShape
{
    std::vector<core::PointF> vertices;   // placed so far
    core::PointF cursorSlide;             // the rubber-band end
    bool closed = true;                   // polygon vs freehand line
    bool active = false;
};
```

### 6.2 Added beside the rectangle path, not unified with it

A rectangle drag could be expressed as a two-vertex pending shape, and that
would be tidier. The release handler carries `dragExceedsMinimumSize`,
`withMinimumExtent` and the misclick-falls-through-to-selection rule, each of
which exists because of a specific thing that went wrong in the running
application. Refactoring working, hard-won behaviour to make room for new
behaviour risks both at once.

`AnnotationTool` gains `Polygon` and `Freehand`. The press, move and release
handlers branch on tool family. The rectangle code is untouched.

The cost is two in-progress mechanisms coexisting — `drawing` and
`pending.active` — and two preview branches in `paintAnnotations`. That is
accepted and should be commented rather than tidied.

### 6.3 Gestures

| Input | Effect |
|---|---|
| Left click | Place a vertex; the first click starts the shape |
| Press and drag | Trail points continuously while held (the fast trace) |
| Double-click, or `Return`/`Enter` | Close and commit |
| Click within tolerance of vertex 1 | Close and commit (polygon only) |
| Backspace | Drop the last vertex |
| Escape | Discard the whole pending shape |

A press places a vertex immediately; trailing appends further vertices from the
same gesture if the cursor then moves. Releasing after a trailing drag does
**not** commit — the shape stays pending, so the user can keep tracing or
switch to clicking individual vertices.

Click-to-close is the discoverable termination; double-click is the one people
try first. Both are cheap, so both are supported. Both `Qt::Key_Return` and
`Qt::Key_Enter` bind, since the numeric keypad delivers the latter.
Right-click stays unbound — a context menu will want it.

**Double-click keeps the vertex it placed.** Qt delivers `press`, `release`,
`doubleClick`, `release`, and the `doubleClick` is sent *in place of* the second
press, not in addition to it. Qt's own default
`QWidget::mouseDoubleClickEvent` is `mousePressEvent(event)`; that forwarding
exists because no second press arrives, and would double-count if one did.
`ViewportWidget`'s override does not forward, so the pair appends exactly one
vertex, placed by its first click, and that is the point the user
double-clicked to position. The handler commits without removing anything.
Dropping the last vertex would lose a real point, and on a triangle would leave
2 vertices, fail the §6.5 minimum and discard the shape in silence.

> **Correction, 2026-10-12.** The original text of this paragraph claimed that
> the second press had already appended a vertex and that the handler must
> `pop_back` before committing. That was wrong. It was implemented as written
> and caught in review, and the implementation was corrected afterwards. The
> text above is current; a quotation of the earlier claim is out of date. The
> empirical check is still manual: count the vertices in the saved file (item 0
> of the persistence follow-ups).

Closing tolerance reuses the existing `kHitToleranceScreenPixels` (6.0) rather
than introducing a second screen-space constant for the same kind of question.

### 6.4 Trailing density

`kTrailMinScreenPixels = 2.0`. A point is appended only once the cursor has
moved that far from the last placed vertex. Screen-space, because every other
tolerance in this subsystem is, so a trace has the same vertex density at 1× and
at 40×. This is the vertex-count control §3.4 deliberately kept out of the
serializer.

### 6.5 Commit threshold

A commit below the §2.3 minimums — 3 vertices closed, 2 open — discards the
pending shape silently, the same way an undersized rectangle drag is discarded
today.

### 6.6 One behaviour change worth naming

**While an N-point tool is active, a left click always places a vertex and
never selects.** With the Rectangle tool, a too-small drag falls through to the
selection block. There is no equivalent here: a single click is a legitimate
first vertex, and the two are indistinguishable at the moment the click
arrives. Selection remains available through the Pan tool.

This is correct but is a real difference between the tools, recorded here so it
is a decision rather than something a user discovers.

### 6.7 Lifecycle

Four places the pending shape must be cleared:

1. **`resetAnnotationState()`.** A half-traced shape is **never** committed on
   slide close. It is not in the model, persistence never sees it, and
   materialising a partial outline into a saved file would be worse than losing
   it. This is the rule `drawing = false` already follows at
   `ViewportWidget.cpp:2796`.
2. **`setActiveTool()`.** Tool changes need not touch `drawing` today, because
   `drawing` cannot outlive a press. The pending shape can.
3. **Escape**, as the first arm of the existing cascade: pending shape, then
   `drawing`, then move, then falling back to resetting the tool to Pan.
4. **Read-only slides** — the two new actions are disabled in `MainWindow`
   exactly as `rectangleToolAction` is, so no pending shape can begin.

And one place it must **not** be cleared:

5. **`focusOutEvent` must leave it alone.** `drawing` is cleared there
   (`:3808`) because a drag whose release went to another window is
   unrecoverable. A pending polygon holds no button and is not in that
   position. Alt-tabbing to check a report mid-trace and returning to an empty
   canvas would destroy minutes of careful work for no reason.

   This is a deliberate asymmetry with the line directly above it, and exactly
   the kind that gets tidied up later unless the comment says why.

### 6.8 Preview rendering

Replaces the rectangle-only preview block at the end of `paintAnnotations`:

- the placed vertices as a path, in the existing orange dashed pen
  (`#E67E22`, 2.0, `Qt::DashLine`);
- a rubber-band segment from the last vertex to the cursor;
- for a polygon, a fainter closing segment from the cursor back to vertex 1, so
  the user sees the shape they will get;
- a small marker on vertex 1 whenever the cursor is within closing tolerance.

The marker is the only genuinely new UI in this increment and it is what makes
click-to-close findable at all.

Mouse tracking is already enabled (`ViewportWidget.cpp:1901`), so a
cursor-following preview needs no new plumbing.

### 6.9 Autosave

No changes. A pending shape is not in the model, so it produces no mutation and
triggers no save. Correct by construction rather than by a rule someone has to
remember.

### 6.10 Where the logic lives

No test suite can construct `ViewportWidget`. The decisions therefore become
pure functions in `AnnotationInteraction`:

```cpp
[[nodiscard]] bool shouldAppendTrailPoint(core::PointF last, core::PointF cursor,
                                          double viewportScale);
[[nodiscard]] bool closesOnFirstVertex(core::PointF firstVertex, core::PointF cursor,
                                       double viewportScale);
[[nodiscard]] bool pendingShapeCommits(std::size_t vertexCount, bool closed);
```

`ViewportWidget` keeps event plumbing only. This is the split `resolveDragOwner`
already represents, and it is the difference between §6 being covered by tests
and being covered by nobody.

---

### 6.11 Defect found and fixed, 2026-10-17

Closing a trace by double-click placed a vertex **on top of** the previous one.
Qt sends the first click of the pair to `mousePressEvent`, which appended the
cursor position unconditionally — the trail append is guarded by
`shouldAppendTrailPoint`'s two-screen-pixel minimum, that one never was. The
cursor has usually not moved since the last trailed point, because the user
stops in order to double-click, so the vertex coincided exactly.

The resulting zero-length segment is invisible: it paints nothing, contributes
nothing to the shoelace area, and changes no hit test. But it makes the
segments on either side of it **share an endpoint**, and `isSelfIntersecting`
excludes only segments adjacent *by index* (`j == i + 1`, plus the closing
wrap). The shape was therefore reported self-intersecting, which by §3.3 of the
measurement design withholds the area entirely.

Observed on a real 160-vertex trace of `Philips-1.tiff`: exactly one pair fired
— segment 157 against segment 159 — and it was this, not a crossing. The user
reported it at roughly one polygon in two, which matches: it depends only on
whether the mouse drifted two pixels between the last trail point and the click.

Two guards, deliberately both:

- `ui::shouldPlaceClickVertex` refuses a click-placed vertex that exactly equals
  the previous one. Exact equality, not a distance threshold — a click is
  explicit and must not be thinned the way the trail is.
- `core::outline()` drops consecutive duplicate vertices, and a trailing vertex
  equal to the first on a closed shape. It is the one place painting,
  hit-testing and measurement all read, so this repairs annotations **already on
  disk** without rewriting a file.

One consequence worth naming: a closed outline that collapses to a single point
now reports an area of **zero** rather than none, since the duplicates that used
to pad it out are gone. That is the same reasoning the zero-perimeter branch in
`measureKnown` already applied — a shape with no extent encloses nothing,
unambiguously.

A trace that returns to exactly a previous vertex — a genuine pinch rather than
a duplicate — still counts as self-intersecting. That was kept deliberately.

---

## 7. Wiring

Two `QAction`s beside `rectangleToolAction` (`MainWindow.cpp:259`), in the same
`toolGroup`, with the same `Qt::WidgetWithChildrenShortcut` context, added to
the tools menu and to `viewportWidget`:

| Action | Shortcut | Status tip |
|---|---|---|
| `polygonToolAction` | `P` | Draw a polygon by clicking points, or by dragging to trace |
| `freehandToolAction` | `F` | Draw an open freehand line |

Gated identically to `rectangleToolAction`: `setEnabled(false)` at
construction, enabled by `annotationsAvailableChanged`, disabled at
`MainWindow.cpp:767`.

**One trap to carry forward.** `closeSlide()` now emits an unavailable signal on
every slide switch, so an availability handler that resets the active tool
drops the user back to Pan on every slide of a series. `MainWindow.cpp:659-665`
records that regression as deliberately avoided, via the `isSlideOpen()` guard
(ruling R26 of the persistence work). The two new actions must not reintroduce
it.

**Status-bar hint while a shape is pending:** *"Click to add points,
double-click or Enter to finish, Esc to cancel"*. This is the other half of
making a modal gesture discoverable, alongside §6.8's first-vertex marker. A
user who clicks once and does not know how to finish otherwise has no
affordance at all.

---

## 8. Testing

### 8.1 `core-tests`

One case matters more than the rest: **a point inside the bounding box but
outside a concave polygon must not hit.** That single test is what
distinguishes real point-in-polygon from the bounding-box approximation this
whole increment exists to remove.

Alongside it:

- even-odd behaviour on a self-intersecting trace;
- interior hit for a closed polygon, no interior hit for a polyline;
- edge proximity within and beyond tolerance;
- tolerance on a **degenerate rectangle** — zero-extent box, non-zero tolerance
  — which closes foundation follow-up item 12, filed as "add in C";
- `outline()` per type: corner order for a rectangle, `closed` preserved;
- `translated()` per type: a polygon keeps its vertex count and its `closed`;
- `typeOf()` for all three shapes;
- `boundingBox` over a polygon, including a shape degenerate on one axis;
- `pendingShapeCommits` is covered in §8.2, but the **double-click vertex
  count** of §6.3 cannot be: it is event-ordering inside the widget. It must be
  verified by counting vertices in the saved file, not by eye — a duplicate or
  a missing vertex is invisible on screen;
- serializer: round-trip polygon and polyline; reject under-minimum vertex
  counts for each; reject a non-finite vertex; reject an empty or absent
  `vertices` array; the unknown-type message text; `schemaVersion` still 1;
  writer and reader agree on every bound.

### 8.2 `ui-tests`

The new pure functions only: `shouldAppendTrailPoint` across viewport scales,
`closesOnFirstVertex`, `pendingShapeCommits` at and below each minimum, and
`resolveDragOwner` extended to `Polygon` and `Freehand`.

### 8.3 Manual checks

Appended to item 0 of `2026-10-11-annotation-persistence-followups.md`, because
nothing above can construct a widget:

1. Trace a polygon and close it three ways: click on vertex 1, double-click,
   Enter.
2. Escape mid-trace discards; Backspace drops one vertex.
3. **Alt-tab mid-trace and return — the shape is still there** (§6.7 item 5,
   the asymmetry most likely to be silently broken later).
4. Switch tools mid-trace — the shape is gone.
5. **Drag a polygon — it keeps its shape.** The regression foundation item 1
   was filed to prevent.
6. Click inside the notch of a concave polygon — nothing selects.
7. Close and reopen the slide — the polygon round-trips.
8. Trace a freehand line; confirm it is not filled.
9. A build predating this work, shown a file containing a polygon: refuses to
   load, names the type, and leaves the file unchanged on disk.

---

## 9. Deliberate absences

Not in this increment:

- **Vertex editing and resize handles.** A drag moves the whole shape, as it
  does today. This is the one a pathologist will ask for first — having traced
  400 points, being unable to nudge one is a real gap. It is out because it
  needs per-vertex hit-testing, a handle rendering pass and a drag mode of its
  own, which is comparable work to everything above.
- **Path simplification or smoothing.** A trace is a clinical record. Quietly
  resampling someone's outline changes the thing they drew. Decimation for
  *display* when zoomed out (§4.6) is a different matter and is also not built.
- **Ellipse, circle, point, arrow and text.** Each is cheap once §6 exists;
  none is needed to prove it.
- **Undo** — sub-project D, though §5.2's press-time original is the state it
  will want.
- **Snapping and magnetic tracing.**
- **Multi-select.** `resolveDragOwner`'s unused `modifiers` parameter still
  names FR-ANN-16 as the next thing to touch those rules.
- **A maximum vertex count** (§3.4) and **coordinate clamping** (foundation
  item 6, ruled).

---

## 10. Open items this work moves

| Item | Source | Disposition |
|---|---|---|
| 1 — move handler rebuilds every annotation as a rectangle | foundation | Closed by §5 |
| 2 — `paintAnnotations` has no per-type dispatch | foundation | Closed by §4 |
| 3 — `AnnotationType` and the variant can disagree | foundation | Closed by §2.2 |
| 12 — tolerance on a degenerate rectangle untested | foundation | Closed by §8.1 |
| 16 — annotation-level `"type"` never validated | persistence | Shifted by §3.5: still ignored on read, but now derived on write |
| 9 — annotations skipped when no tile is visible | foundation | **Still open.** §4.6 is unrelated per-annotation culling |

---

## 11. Risks and accepted trade-offs

1. **An old build cannot open a file containing a polygon** (§3.2). Accepted:
   it fails safely, names the cause, and leaves the file intact. The
   alternative — bumping `schemaVersion` — makes old builds refuse
   rectangle-only files too.
2. **Moving a dense trace is O(vertices) per mouse-move event** (§5.3).
   Accepted rather than letting the model disagree with the screen mid-drag.
3. **Two in-progress drawing mechanisms coexist** (§6.2). Accepted to avoid
   refactoring hard-won rectangle behaviour at the same time as adding new
   behaviour.
4. **A left click with an N-point tool never selects** (§6.6). Accepted;
   selection stays on the Pan tool.
5. **A pending shape survives focus loss but not a tool change** (§6.7). A
   defensible line, but a line — a user who switches tools to check something
   loses the trace.
6. **No profile backs §4.6.** The cull is taken on reasoning, not measurement.
   It is cheap and cannot be wrong in a way that loses data, only in a way that
   fails to help.
7. **The plan's claim of a green build at every commit did not hold.** Tasks 6
   and 7 could not be separate commits: the serializer's `std::visit` sites are
   deliberately exhaustive, so adding `PolygonGeometry` to the variant without
   its serialisation does not compile. They landed together as `dc40641`. That
   exhaustiveness is the property the design relies on — the compiler refuses a
   half-widened variant — and the price is that widening is one atomic change,
   not two. Accepted; a future geometry type should be planned as one commit
   covering the variant and every visitor over it.
