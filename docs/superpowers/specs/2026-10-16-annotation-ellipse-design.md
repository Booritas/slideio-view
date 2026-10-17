# Annotation geometry widening — ellipse

**Sub-project A, second widening round.** Written 2026-10-16 against `main` @ `3ba835a`.

Adds `EllipseGeometry` to `core::AnnotationGeometry`, the drawing tool that
produces it, and the Shift and Alt drag modifiers both it and the rectangle
have always been specified to have.

## 1. Intent

`2026-10-12-annotation-geometry-widening-design.md` §1.1 deferred the ellipse
deliberately: it is two points like a rectangle, needs none of the multi-click
machinery, and taking the hard interaction problem first meant the freehand
line fell out of the same work. That machinery now exists. The ellipse is what
is left of FR-ANN-02, a **Must**, and it is the last closed shape that fits the
model without changing it.

### 1.1 Success criteria

1. A pathologist drags an ellipse the way they drag a rectangle, and sees the
   shape they drew.
2. Shift constrains it to a circle; Alt draws it from the centre.
3. An inverted drag — bottom-right to top-left — normalises.
4. A click inside the corner of its bounding box but outside the curve does
   **not** select it.
5. The area shown in the properties panel agrees with the region shaded on
   screen.
6. The shape survives a save, a slide close, a reopen and an ICC profile
   change, unchanged.
7. A build of the viewer predating this work, shown a file containing an
   ellipse, refuses that file **by name** and leaves every byte intact.

### 1.2 Scope

**In:** `EllipseGeometry` and its five dispatch overloads; the `ellipse` JSON
type; `AnnotationTool::Ellipse` and its toolbar action; the Shift and Alt
modifiers, applied to the rectangle tool as well; the convexity short-circuit
in `measure()`.

**Out:** rotation; resize and vertex handles (FR-ANN-17, its own sub-project);
every other unbuilt shape — point, arrow, text, ruler, angle; adaptive outline
density and display decimation.

---

## 2. Domain model

```cpp
struct EllipseGeometry
{
    PointF topLeft;
    PointF bottomRight;
};
```

`AnnotationType::Ellipse` joins the enum. The type stays **derived** from the
geometry through `typeOf()`, never stored beside it, exactly as §2.2 of the
first widening spec established.

### 2.1 The bounding box, not centre and radii

The two forms carry the same information. The box is chosen because it mirrors
`RectangleGeometry` field for field, and three pieces of existing machinery
then apply unchanged rather than through conversions:

- `boundingBox()`'s normalisation of an inverted drag — the "always
  positive-extent" guarantee in `AnnotationGeometry.h`;
- `ui::dragExceedsMinimumSize`, which takes a `RectF`;
- `ui::withMinimumExtent`, which widens a degenerate axis and also takes a
  `RectF`.

A centre-and-radii form would convert at each of those sites and at the
serializer, for no gain. It also keeps the written file legible beside a
rectangle's — the format was chosen to be read in a text editor, and
`topLeft`/`bottomRight` is already what a reader of this file has learned.

Centre and radii are derived where they are needed, in `outline()`, from the
normalised box: `cx = (left + right) / 2`, `a = (right - left) / 2`, and
likewise for `cy` and `b`.

### 2.2 Degenerate extents

`withMinimumExtent` runs before commit, so the application cannot create a
zero-extent ellipse. A hand-edited file can still contain one. It is
**accepted, not rejected**, exactly as a degenerate rectangle is: the outline
collapses to a line, the area is zero, and the tolerance band still makes the
shape clickable. Rejecting it would make the whole document unloadable — and
by the governing rule, never again writable — over a shape the user can see and
fix.

---

## 3. The ellipse is its inscribed polygon

`outline()` returns `kEllipseOutlineVertices` points, closed:

```
theta_i  = 2*pi*i / N,   i in [0, N)
vertex_i = ( cx + a*cos(theta_i), cy + b*sin(theta_i) )
```

The first vertex is the rightmost point of the curve. Because slide
coordinates increase downward, the sequence runs clockwise on screen; neither
the even-odd fill, the even-odd hit test nor the shoelace area magnitude cares
about the winding direction.

**That polygon is the definition of the shape**, not an approximation of it.
The painter strokes it, `hitTest` ray-casts it, and `measure()` integrates it.
There is one source, so the three cannot disagree — which is the property §4.3
of the first widening spec exists to protect, and the reason this form was
chosen over exact ellipse mathematics with a separately drawn curve.

### 3.1 Why N is fixed

```cpp
constexpr std::size_t kEllipseOutlineVertices = 512;
```

Fixed, never adaptive, and never derived from the shape's size or the current
zoom. An adaptive count would make a stored annotation's measured area depend
on the zoom it happened to be measured at. Sub-project G exists to make
measurements defensible and reproducible; a number that moves when the user
scrolls is neither.

### 3.2 What 512 costs

Against the true ellipse:

| Quantity | Error at N = 512 |
|---|---|
| Area — `(N/2)·a·b·sin(2π/N)` against `π·a·b` | ~0.0025% low |
| Perimeter | under 0.001% low |
| Facet bulge on a shape 20 000 screen px wide | ~0.19 px |

The area error is deterministic, documented here, and two orders of magnitude
inside the uncertainty of the scanner's own microns-per-pixel calibration. It
is reproducible across builds and machines, and it agrees with what is painted
— which is the standard sub-project G set.

N is a single constant. If faceting is ever reported at extreme zoom, raising
it is a one-line change with a known effect on both columns above.

### 3.3 Runtime cost, accepted

`outline()` allocates 512 points per call, and it is called per annotation per
paint. This is the same shape of cost `PolygonGeometry` already carries — a
freehand trace of a tissue boundary routinely exceeds 512 vertices — so the
ellipse adds no new category of expense. Section 4 names the one place where
it would have, and closes it.

### 3.4 The five dispatch sites

| Site | Ellipse behaviour |
|---|---|
| `typeOf` | `AnnotationType::Ellipse` |
| `boundingBox` | the normalised box itself — exact, not the polygon's hull |
| `translated` | both points shifted; extents, and so the radii, preserved |
| `outline` | §3 above |
| `hitTest` | delegates to the polygon path: even-odd ray cast over the outline, plus the existing segment-distance tolerance band |

`hitTest` dispatches through `std::visit` onto the `hitTestShape` overload set,
and `geometryToJson` through the `shapeToJson` overload set; `geometryTypeName`
carries an explicit `static_assert`. Omitting any of these is a **compile
error naming the site**. One site is not protected that way — §5.3.

**Corrected during implementation, 2026-10-17.** The table above lists five
sites and this paragraph names three more. The real count is **ten**. Two were
not anticipated here and were found only because the compiler refused to
build: `isRepresentableShape` in `AnnotationSerialization.cpp` — the "refusing
to write a shape we would refuse to read" guard, which needed a
finite-corners-only overload matching the rectangle's — and `ShapeNameVisitor`
in `src/ui/src/AnnotationShapeName.cpp`, which produces the user-facing shape
name in the annotation list and properties panel and has a `switch` over
`AnnotationType` beside it. The sixth core overload set, `convexShape`, is
added by §4.1. The lesson for the next shape is that the count in a spec is
not to be trusted — let the compiler enumerate the sites.

`boundingBox` returning the exact box while `outline` is inscribed means the
box is very slightly larger than the painted shape. That direction is the safe
one: the box is used for culling and for the selection rectangle, where
including a little extra is correct and excluding anything is not.

---

## 4. Measurement

An ellipse inherits area and perimeter from `core::measure()` with no new code
— it is a closed shape, and the shoelace formula over its outline is exactly
what the properties panel already shows for a polygon.

### 4.1 The convexity short-circuit

`measure()` currently calls `isSelfIntersecting()`, which is **O(n²)**. At 512
vertices that is roughly 131 000 segment-pair tests, to answer a question whose
answer is structurally always *no*: **an ellipse is convex and cannot
self-intersect.**

`measure()` therefore gains a cheap predicate — a geometry that is convex by
construction skips the test and calls `measureKnown(geometry, pixelSize,
false)` directly. `measureKnown` already exists for precisely this purpose: a
caller that can prove the answer supplies it rather than paying for it.

Without this, selecting an ellipse would run six figures of arithmetic to
produce a constant. This is the one place where choosing the polygonal
definition imposes a cost that must be *paid* rather than accepted, and it is
cheap to pay.

`ShapeMetrics::selfIntersecting` is `false` for an ellipse, which is true
rather than merely convenient — the field documents why an area is missing, and
an ellipse's area is never missing.

---

## 5. File format

### 5.1 JSON shape

```json
{
  "type": "ellipse",
  "topLeft":     { "x": 1200.0, "y": 800.0 },
  "bottomRight": { "x": 1800.0, "y": 1100.0 }
}
```

Field for field a rectangle's, with one type name changed. `readPoint` already
requires both coordinates to be finite, so a non-finite ellipse cannot be read.

The annotation-level `"type"` field is written as `"ellipse"` from the geometry
and ignored on read, as §3.5 of the first widening spec settled.

### 5.2 `schemaVersion` stays at 1

Unchanged, and for the same reason as last time: a higher version is refused
outright, so bumping it would make every shipped build refuse files that
contain only rectangles and polygons during a staggered rollout. An old build
meeting an ellipse must fail on **that shape alone**, name it, and leave the
file intact. Check P14 observed exactly that behaviour for the polygon; group Y
repeats it for the ellipse.

### 5.3 The site with no compile-time guard

`readGeometry` in `src/core/src/AnnotationSerialization.cpp` is a
string-comparison chain with no link to the variant. Forget its branch and the
application writes files it then refuses to open, breaking the invariant that a
file we cannot parse is never a file we write to.

Nothing in the compiler can force that branch to exist. **The serializer
round-trip test is what forces it**, and it is therefore not optional: an
ellipse must be written, read back, and compared, in `core-tests`, in the same
commit that adds the variant member.

---

## 6. Drawing

### 6.1 The tool reuses the rectangle's path

`AnnotationTool::Ellipse` joins the enum. `isNPointTool` returns **false** for
it, which is the whole of what routes it down the single-drag path rather than
the multi-click one. It then reuses, unchanged: `resolveDragOwner`, the
minimum-size guard, the degenerate-axis widening, the press-time original that
sub-project D's undo stack records, and the preview-during-drag.

The preview strokes the pending ellipse's outline rather than a box, so what is
shown during the drag is what is committed on release.

### 6.2 The modifiers

A new pure function in `ui`, testable without a `QApplication`:

```cpp
[[nodiscard]] core::RectF applyDragConstraints(core::RectF box,
                                               core::PointF anchor,
                                               Qt::KeyboardModifiers mods);
```

- **Shift** squares the box about `anchor`, taking the larger of the two
  extents so the shape never shrinks under the cursor.
- **Alt** re-centres the box on `anchor`, so the press point becomes the centre
  and the drag defines a radius.
- Both compose: Shift then Alt yields a circle centred on the press.
- Neither modifier held returns `box` unchanged.

`anchor` is the press point in slide units. The function is shape-agnostic — it
constrains a box, and knows nothing about what will be built from it.

### 6.3 The rectangle gets them too

`documents/02-user-interface-design.md` §6.6 specifies Shift and Alt as general
annotation modifiers, and FR-ANN-01 has always said "Shift constrains to
square". Neither is built. Because `applyDragConstraints` takes a box and not a
shape, pointing the rectangle tool at it closes that clause of FR-ANN-01 for no
additional logic and one additional call site.

**This is the one piece of scope that is not FR-ANN-02.** It is included
because leaving it out would mean shipping a Shift modifier that works on one
of two bounding-box tools, which is harder to explain than either doing it or
not. It is marked here so it can be cut cleanly if that judgement is wrong.

### 6.4 Toolbar and shortcut

An `ellipseToolAction` beside the rectangle, polygon and freehand actions:
checkable, in the same exclusive group, with `Qt::WidgetWithChildrenShortcut`
like its neighbours, and the shortcut **E** — which
`documents/02-user-interface-design.md` §6.2 already assigns to Ellipse and
which no action currently uses.

**Noted, not fixed:** that same table assigns **P** to Point Marker, while the
code binds P to the Polygon tool. Point Marker is not in this round. The
conflict is recorded here so it is a decision when that shape lands, rather
than a discovery.

---

## 7. Testing

### 7.1 `core-tests`

- `outline()` returns exactly `kEllipseOutlineVertices` points, `closed` true,
  and every point satisfies the ellipse equation to within a tight epsilon.
- An inverted drag — `topLeft` below and right of `bottomRight` — produces the
  same outline, bounding box and area as the normalised one.
- `typeOf()` is `Ellipse`; the annotation-level type string is `"ellipse"`.
- `translated()` preserves both extents and moves the centre by the delta.
- `hitTest`: a point at the centre hits; a point in the corner of the bounding
  box but outside the curve **misses**; a point just outside the curve within
  the tolerance band hits; a negative tolerance behaves as zero.
- `measure()`: the area equals the inscribed polygon's exact area
  `(N/2)·a·b·sin(2π/N)` to within floating-point epsilon, **and** lies within
  0.01% of `π·a·b` — the second assertion documents the relationship §3.2
  claims, and fails if N ever changes without this spec changing.
- `measure()` on an ellipse never reports `selfIntersecting`.
- A circle — equal extents — has area within 0.01% of `π·r²`.
- **Serializer round-trip:** an ellipse written and read back is identical.
  §5.3 — this test is what forces the `readGeometry` branch to exist.
- `readGeometry` on an unknown type still produces the message naming the type
  and the newer-version hint.

### 7.2 `ui-tests`

- `applyDragConstraints` with no modifier returns the box unchanged.
- Shift squares about the anchor, taking the larger extent, for a drag in each
  of the four quadrants.
- Alt re-centres on the anchor.
- Shift and Alt together produce a square centred on the anchor.
- `isNPointTool(AnnotationTool::Ellipse)` is false.

### 7.3 Manual — group Y

No test binary creates a `QApplication`, so nothing below is reachable from any
suite. Group Y, to be run against the installed build and recorded in this file
the way groups U, V, W and X were.

**Not yet run.** Group Y is the list to work through against the installed
build; results are recorded here afterwards.

| | Check |
|---|---|
| Y1 | Press E; the ellipse tool activates and the toolbar shows it checked. |
| Y2 | Drag an ellipse; the preview follows the cursor and the committed shape matches the preview. |
| Y3 | Drag bottom-right to top-left; the ellipse is correct, not inverted or absent. |
| Y4 | Hold Shift while dragging; the shape is a circle. |
| Y5 | Hold Alt; the ellipse grows from the press point. |
| Y6 | Hold Shift and Alt; a circle centred on the press point. |
| Y7 | Hold Shift with the **rectangle** tool; the shape is a square. |
| Y8 | Click inside the ellipse: selected. Click in the corner of its bounding box, outside the curve: not selected. |
| Y9 | The properties panel shows an area and a perimeter, and no self-intersection warning. |
| Y10 | Drag the ellipse; it translates and stays an ellipse. |
| Y11 | Ctrl+Z undoes the creation; Ctrl+Y restores it. |
| Y12 | Save, close the slide, reopen: the ellipse returns unchanged. |
| Y13 | A tiny drag below the minimum size produces no annotation, as with a rectangle. |
| Y14 | A build predating this work, shown a file containing an ellipse, refuses it naming `'ellipse'` and leaves the file byte-identical. |
| Y15 | Zoom deeply into a large ellipse; the curve shows no visible faceting. |
| Y16 | Hold Shift, drag out a circle, **let go of Shift without moving the mouse**, then release the button. The committed shape must match the circle last previewed, not the unconstrained drag. (The inverse too: press Shift after the drag stops moving, release the button — the shape must stay the ellipse that was on screen.) |
| Y17 | With Alt held, make a tiny drag of a few pixels. It must be rejected as a misclick exactly as the same drag without Alt is — no annotation. |
| Y18 | **Known risk, unverified.** Drag an Alt-centred ellipse, release **Alt** before the mouse button, then release the button. Qt gives the menu bar focus when Alt is pressed and released alone; `ViewportWidget::focusOutEvent` clears `drawing` and `dragOwner`, and `mouseReleaseEvent`'s first guard then swallows the release — so the shape would vanish with no annotation and no message. If this reproduces it needs its own fix, designed against what is observed here: the teardown in `focusOutEvent` exists so a drag whose release went to another window cannot swallow every later press, so it cannot simply be removed. Try the reverse order too (button first, then Alt), which should commit normally. |

---

## 8. Deliberate absences

- **Rotation.** FR-ANN-02 defines the shape by a bounding-box drag and asks for
  none. An axis-aligned box cannot express a rotated ellipse, so adding
  rotation later is a format change — noted so it is not assumed cheap.
- **Resize and vertex handles.** FR-ANN-17, still its own sub-project, still
  the thing a pathologist asks for first.
- **Adaptive outline density and display decimation.** §3.1 rules out the
  first; the second remains open from the first widening spec's §4.6 and is
  unrelated to this shape.
- **Every other unbuilt shape.** Point, arrow and text each need screen-space
  sizing, which the geometry model does not have; ruler and angle are
  measurement readouts over paths. None of them fits this model without
  changing it, which is why this round stops at the ellipse.

---

## 9. Risks and accepted trade-offs

| Risk | Disposition |
|---|---|
| Area reads ~0.0025% under the true ellipse | Accepted and documented. Deterministic, reproducible, agrees with the painted region, far inside calibration uncertainty. |
| 512-point outlines allocated per paint | Accepted. Same cost shape as an existing freehand trace; §4.1 removes the one place it would have been quadratic. |
| `readGeometry` branch forgotten | Mitigated by the mandatory round-trip test (§5.3). It is the only unguarded site and the only reason that test is non-negotiable. |
| Rotation later forces a format change | Accepted. Out of scope by requirement, named in §8 so it is not discovered. |
| P shortcut collides with Point Marker later | Recorded in §6.4, not resolved. Point Marker is not in this round. |

---

## 10. Requirements this closes

| Requirement | Status |
|---|---|
| FR-ANN-02 — Ellipse, bounding-box drag, Shift → circle, Alt → from centre | **Closed** |
| FR-ANN-01 — "Shift constrains to square" clause | **Closed** by §6.3 |
| FR-ANN-17 — resize and reshape via handles | Still open; its own sub-project |
