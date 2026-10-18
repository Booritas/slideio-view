# Point Marker and Arrow Annotations — Design

**Date:** 2026-10-17
**Status:** Approved, not implemented
**Requirements:** FR-ANN-05 (Point Marker), FR-ANN-06 (Arrow)
**Follows:** `2026-10-16-annotation-ellipse-design.md`,
`2026-10-17-annotation-vertex-editing-design.md`,
`2026-10-17-annotation-box-resize-design.md`

---

## 1. Purpose and scope

Two new members of `core::AnnotationGeometry`: a point marker placed by a
single click, and an arrow dragged from tail to head.

### 1.1 Where this came from

The stated purpose is **counting objects by category** — mitoses against
apoptotic bodies, positive against negative nuclei. Working back from that, the
work divides into two subsystems:

1. The two geometry types, their tools, their rendering and their format.
2. A sticky category that new annotations inherit, and per-category counts in
   the annotation panel.

They are independent. (2) has nothing to do with arrows, applies to every shape
type equally, and changes `AnnotationModel`'s creation path rather than the
geometry variant. This spec is (1) only. **(2) is the agreed next round.**

The honest limitation of shipping (1) first: `AnnotationModel::add` gives every
new annotation `core::AnnotationProperties{}` — default colour, empty
classification. Until (2) lands, counting two categories means placing a
marker, selecting it, and setting its classification in the properties panel,
once per marker. That is not a counting tool, and this spec does not pretend
otherwise. The order is deliberate: the alternative ships a sticky category
with no markers to apply it to.

### 1.2 Out of scope

Listed in full in §8.

---

## 2. Geometry

```cpp
struct PointGeometry
{
    PointF position;
};

struct ArrowGeometry
{
    PointF tail;
    PointF head;
};
```

### 2.1 Why not reuse an existing type

**A point is not a zero-extent rectangle.** A box promises two corners that
could differ; normalisation, `withBoxReplaced`, `withMinimumExtent` and the
resize handles would all apply to it, each meaninglessly. A position is what a
marker is.

**An arrow is not a two-vertex `PolygonGeometry`.** `AnnotationType` is derived
from the geometry and never stored beside it — that is what makes the two
unable to disagree — so `typeOf()` must be able to tell an arrow from a
freehand line by its geometry alone. A 2-vertex open polyline and an arrow
would be indistinguishable, and the derived type would be a coin flip.

The arrow's pair is **ordered**, and the order is the meaning: the head is the
end that carries the glyph. Normalising it, the way a rectangle's corners are
normalised, would silently reverse arrows.

### 2.2 What each dispatch answers

| | `PointGeometry` | `ArrowGeometry` |
|---|---|---|
| `boundingBox` | zero extent at `position` | normalised box of the two endpoints |
| `hitTest` | within tolerance of `position` | within tolerance of the shaft segment |
| `outline` | one point, `closed = false` | `{tail, head}`, `closed = false` |
| `translated` | position moved | both endpoints moved |
| `typeOf` | `AnnotationType::Point` | `AnnotationType::Arrow` |
| `hasWellDefinedArea` | `false` | `false` |

`hitTest` for an arrow reuses `distanceToSegment` against the shaft. The head
is decoration and is not separately hit-tested: it sits at the shaft's end and
is therefore already within tolerance of it.

`hasWellDefinedArea` is `false` for both because neither has an area at all.
The predicate promises only that a *type* always has a well-defined area; it is
not a claim about self-intersection or convexity, and two earlier names that
said otherwise were wrong. See the ellipse spec §3.

### 2.3 Measurement needs no new code

`core::measure` is driven by `outline()`. An open path's area is already
absent, and its length is already the sum of its segments. A point therefore
measures length 0 and no area; an arrow measures its tail-to-head distance as a
length, and `ShapeMetrics::closed` being false is what makes the properties
panel call it a length rather than a perimeter.

No overload of `measure`, `measureKnown` or `isSelfIntersecting` is added.

### 2.4 Degenerate bounding boxes are already handled

A point's bounding box has zero extent on both axes; a horizontal arrow's has
zero height. `ViewportWidget` has a show-annotation path that calls
`ViewportController::showRect(annotation->boundingBox(), 0.15)`.

**Verified, not assumed:** `core::Viewport::fitToRect` contributes a candidate
scale only from an axis that has extent, and when both are degenerate it keeps
the current scale while still recentring. Its comment names "a point"
explicitly. Showing a marker therefore centres the view on it without zooming,
which is the wanted behaviour. No change is required, and none should be made.

### 2.5 The dispatch sites

Adding the ellipse in 2026-10 fired **ten** compile-guarded sites. This round
fires **twelve**, because `core::withBoxReplaced` has been added since:

Six overload sets in `AnnotationGeometry.cpp` — `normalized`, `hitTestShape`,
`typeOfShape`, `outlineShape`, `translatedShape`, `wellDefinedArea`. Then
`shapeToJson`, `isRepresentableShape` and `geometryTypeName`'s `static_assert`
in `AnnotationSerialization.cpp`; `ShapeNameVisitor` in
`src/ui/src/AnnotationShapeName.cpp`, which also has a `switch` over
`AnnotationType` beside it; and now `replaceBox` in `BoxResize.cpp`.

`withBoxReplaced` uses `std::visit` onto an overload set rather than a
`get_if` chain, specifically so a new shape cannot fall silently through to
"not resizable". That decision was made one round ago and pays for itself here:
both new types are forced to answer, and both answer `std::nullopt`.

**Do not trust the count.** Let the compiler enumerate them. Each missing
overload is a compile error naming its site.

### 2.6 The thirteenth site, which the compiler will not find

`readGeometry` in `AnnotationSerialization.cpp` is a string-comparison chain
with no compile-time link to the variant. Forget a branch and the application
writes files it then refuses to open, breaking the "never write a file we
cannot read back" invariant.

Two branches must be added by hand, and **two serializer round-trip tests are
what force them to exist**. Nothing else will.

---

## 3. Screen-space decoration

### 3.1 The problem

`core::outline()` is the single input to the painter, `hitTest` and `measure`,
all in slide units. That is what keeps the painted interior and the hit-tested
interior from disagreeing, and why the Qt conversion in `paintAnnotations`
"has no idea what kind of shape it is drawing".

FR-ANN-05 requires the marker to be "always visible regardless of zoom level
(screen-space size)". A marker has no slide-space extent to give. An arrowhead
has the mirror problem: sized in slide units it vanishes when zoomed out; sized
in fixed screen pixels it outgrows its own shaft.

### 3.2 The resolution

A new Qt-free core file, `AnnotationDecoration.{h,cpp}`:

```cpp
struct ScreenDecoration
{
    enum class Kind
    {
        Disc,
        ArrowHead,
    };

    Kind kind;
    PointF anchor;      ///< slide units
    double sizePixels;  ///< screen pixels; see below for what it measures
};

[[nodiscard]] std::optional<ScreenDecoration> screenDecoration(
    const AnnotationGeometry& geom, double viewportScale, double discRadiusScreenPixels);
```

`std::nullopt` for a rectangle, an ellipse and a polygon, and for a
non-positive `viewportScale` — consistent with every other screen-space
function in this codebase, which returns a safe value rather than dividing by
a scale it cannot use.

**What `sizePixels` measures, stated once so the painter is determined:**

- For `Disc`, the **radius**, and `anchor` is the centre — which is the
  marker's position.
- For `ArrowHead`, the head's **length along the shaft**, and `anchor` is the
  arrow's head point. The barbs are half that length to either side of the
  shaft, giving a head as wide as it is long. The painter derives the shaft
  direction from the two screen points it has already converted.

`outline()` is unchanged and still yields slide-space geometry: nothing here
makes a measurement depend on zoom. The painter gains one generic branch —
draw a glyph of this kind, at this anchor, this many pixels across — and stays
shape-blind. It derives the arrowhead's direction from the two shaft points it
has already converted to screen space, which is correct because the viewport
only pans and zooms and so preserves direction. Core therefore never needs
screen coordinates, only screen sizes.

**Rejected:** making `outline()` take a scale and expanding the marker into a
slide-space circle at the current zoom. The ellipse spec forbids exactly this:
a stored annotation's measured value would then depend on the zoom it happened
to be measured at.

### 3.3 The disc radius, and why it is a parameter

The marker's drawn radius must equal the hit-test tolerance, so that **the
marker you see is the target you click** — by construction, rather than by two
numbers that happen to agree.

`kHitToleranceScreenPixels` lives in `ui/AnnotationInteraction.h`, and core
cannot depend on ui. Three ways out, and the choice matters:

- Pass the radius in as a parameter. One constant exists, in ui, and the
  caller supplies it.
- Declare a second constant in core. Two numbers that must agree, which is the
  thing being avoided.
- Move `kHitToleranceScreenPixels` down into core. Screen-space tolerances are
  ui's by convention — `screenToleranceToSlide` is there — and moving it would
  edit every existing call site for the benefit of one new one.

**Decision: the parameter.** `screenDecoration` takes
`discRadiusScreenPixels`, and `ViewportWidget` passes
`kHitToleranceScreenPixels`, the same value it passes to the hit test. There
is one constant and no coincidence to maintain. A core test covers the radius
being returned as given; the ui-level fact that it equals the hit tolerance is
covered where the call is made.

### 3.4 The arrowhead rule

```cpp
constexpr double kArrowHeadScreenPixels = 12.0;
constexpr double kArrowHeadMaxShaftFraction = 0.5;
```

The head's size is the **smaller** of `kArrowHeadScreenPixels` and
`kArrowHeadMaxShaftFraction` times the shaft's length in screen pixels. Fixed
alone means the head outgrows its shaft on a short arrow or at low zoom;
proportional alone means it disappears. This is the one piece of real
arithmetic in the round, and under the rejected painter-special-case approach
it would have lived in the only layer no test can reach.

A zero-length arrow — which the tool refuses to create, but which the parser
accepts from disk — yields a head of size 0 rather than a NaN direction.

---

## 4. Tools and interaction

`ui::AnnotationTool` gains `Point` and `Arrow`.

### 4.1 The Point tool commits on a click

Every existing tool discards a drag below `ui::dragExceedsMinimumSize` as a
misclick. The Point tool is the exact inverse: a click with no movement is the
whole gesture, and there is no size that could be insufficient. It never
consults that predicate.

**The marker is created on the RELEASE, at the PRESS position.** Both halves
matter and they are not the same choice:

- Created on the release, because every other tool commits there, and because
  a press that turns into something else — held space taking over to pan —
  must not have already left a marker behind.
- Positioned at the press, because a hand twitching during a click must not
  shift the mark, and because the press is the point the user aimed at.

### 4.2 The Arrow tool needs its own minimum

Press places the tail, release places the head. A zero-length arrow is
meaningless, so a minimum applies — but not the box one.
`dragExceedsMinimumSize` tests either dimension of a bounding box, so a
diagonal drag of 10 screen pixels presents a 7x7 box and would be rejected
against a 10-pixel threshold. The right test is the hypotenuse:

```cpp
[[nodiscard]] bool arrowExceedsMinimumLength(core::PointF tailSlide, core::PointF headSlide,
                                             double viewportScale, double minimumScreenPixels);
```

False for a non-positive scale, like every other predicate in that header.
Pure, and therefore covered by ui-tests.

### 4.3 No modifiers

`ui::applyDragConstraints` is box semantics — Shift squares about an anchor,
Alt doubles from a centre. Neither means anything for a tail-to-head drag, and
FR-ANN-06 asks for none. Adding them later is additive and breaks nothing.

### 4.4 What needs no work

- **Rapid placement.** The active tool already persists after a commit, so
  click-click-click works with no change. This is what makes a counting pass
  possible at all before the next round lands.
- **Selection and move.** Both route through `resolveDragOwner` and
  `core::translated()`, which are type-agnostic.
- **Resize handles exclude themselves.** `Impl::resizableBoxGeometry` asks
  `core::withBoxReplaced` rather than testing types — "keeps that judgement in
  one place rather than repeating a type test here, which a future box-shaped
  type would then miss". Both new types answer `nullopt`, so no handles are
  painted and no press is intercepted. No UI change.
- **Vertex editing stays polygon-only.** Double-clicking a marker or an arrow
  does nothing.
- **Read-only slides** refuse creation through the existing
  `annotationsEditable()` gate, as every other tool does.

### 4.5 Accepted limitation

The undo stack is 50 deep (FR-ANN-18's floor). A counting pass beyond 50
markers cannot be undone to its start. Coalescing consecutive placements into
one step was considered and rejected: undoing markers one at a time is what a
counter wants.

---

## 5. Serialization

```json
{ "type": "point", "position": { "x": 100.0, "y": 200.0 } }
{ "type": "arrow", "tail": { "x": 10.0, "y": 20.0 }, "head": { "x": 90.0, "y": 60.0 } }
```

`schemaVersion` **stays 1**. A higher version is refused outright, so bumping
it would make every shipped build refuse rectangle-only files during a
staggered rollout. An old build meeting one of these new shapes is designed to
fail on that shape alone, name it, and leave the file intact — observed for
the polygon in check P14.

`isRepresentableShape` answers true for both unconditionally: any finite
coordinates describe a valid marker or arrow. There is no minimum vertex count
to enforce, unlike a polygon.

`readGeometry` gains a `"point"` branch and an `"arrow"` branch. See §2.6.

---

## 6. Rendering

Reuses the existing machinery: the slide-to-screen conversion is point by
point rather than by a painter transform, because FR-ANN-11 fixes line width
in screen pixels.

- An arrow's shaft is its `outline()`, stroked like any open path.
- The glyph is painted **after** the outline, the way selection handles are.
- The disc is filled in the annotation's colour with the same black underlay
  the vertex and resize handles use, so a marker stays visible over pale and
  dark tissue alike.
- A selected point or arrow shows the ordinary selection treatment; it paints
  no handles (§4.4).

---

## 7. Testing

### 7.1 core-tests

- The six dispatches for both new types, including an arrow's normalised
  bounding box when drawn right-to-left and bottom-to-top.
- `measure` for both: a point's length 0 and absent area; an arrow's
  tail-to-head length, absent area, `closed == false`.
- `screenDecoration`: `nullopt` for the three existing types; `Disc` at the
  point's position with the radius passed in; `ArrowHead` at the head; the
  minimum rule at a long shaft (fixed size wins) and a short one (fraction
  wins); a zero-length arrow; a non-positive scale.
- Serializer **round-trip** for both — the only thing forcing `readGeometry`'s
  branches to exist.
- An unknown geometry type is still refused by name, with the file unchanged.

### 7.2 ui-tests

- `arrowExceedsMinimumLength` across lengths above and below the threshold, on
  several directions including axis-aligned and diagonal, and at a
  non-positive scale.
- `shapeName` answers for the two new `AnnotationType` values.

### 7.3 Manual — group AB

No test binary creates a `QApplication`, so placement, painting and every
routing decision in `ViewportWidget` are unreachable from any suite.

**Run 2026-10-18 against the installed build (sha256 `3471d563…`, commit
`4bb744f`): 21/23 passed, AB19 and AB23 not performable — see below.** The
binary was rebuilt and reinstalled first, and its hash compared against
`build/build/Release/slideio-viewer.exe` and matched: a running viewer turns
`cmake --install` into a silent no-op that still exits 0, so the artifact is
what was checked, not the exit code. The hash recorded here before the run
(`c539ede5…`) was wrong — the file on disk with an untouched mtime hashed
differently under two independent tools, so the recorded value, not the file,
was the error. Record a hash only from the artifact you are about to drive.

The checks were driven from a session rather than by hand: Win32 `SetCursorPos`
plus `mouse_event`, with the cursor position read back and confirmed before
every press. That confirmation is not optional — the first pass of AB2 placed
three of ten markers tens of pixels from the click because the press fired
before the move had landed, which looks exactly like a placement defect and is
not one. Where a check could be decided by a number rather than by eye it was:
marker and arrowhead sizes come from counting the annotation-coloured pixels in
a screenshot, and placement from the saved JSON, whose slide coordinates are
exact.

**AB19 and AB23 cannot be performed as written, and the setup note below was
wrong.** It claimed the active tool is not reset when a slide opens. It is:
`MainWindow::applyAnnotationAvailability` calls `setActiveTool(AnnotationTool::Pan)`
whenever a real gate applies (`MainWindow.cpp:1746`), and `m_active` on the
persistence service only changes in `beginSlide`/`endSlide`, so every transition
into non-editable is also a transition to the Pan tool. On a read-only slide all
six drawing-tool actions are disabled, the tool is forced to Pan, and the status
bar carries a persistent "Annotations disabled" indicator. Both checks were run
anyway and both produced the outcome they ask for — a click with the Point tool
and a drag with the Rectangle tool created nothing, the list stayed at 12 and
the file on disk was unchanged — but neither reached the `annotationsEditable()`
gate in `commitDrawDrag`, because nothing in the UI can. That gate is defence in
depth behind two layers that already hold, exactly as `commitPendingShape`'s is.
It is kept, not removed: it costs one branch, and the layer in front of it is a
UI decision that a later round could change.

AB19 needs a slide whose annotations are not editable. Producing one is easy and
was done the way group AA did it: point the stored `slideId` in the annotation
file at a value matching no slide, and resolve the mismatch dialog as Open
Read-Only; the file was restored afterwards from a byte-identical backup. What
is not possible is the second half — having a drawing tool active on it. This
paragraph used to say the tool survives the open; it does not, and the run above
records why.

| | Check |
|---|---|
| AB1 | Select the Point tool and click: a marker appears at the click, and the tool stays active for the next click. |
| AB2 | Place ten markers in quick succession: ten appear, the list panel shows ten, none is lost or doubled. |
| AB3 | Zoom from fit-to-slide to maximum: the marker's drawn size does not change at any zoom. |
| AB4 | Click a marker: it selects. Click 10 screen pixels away: it does not. The grab target matches the drawn disc. |
| AB5 | Drag a marker: it moves, and Ctrl+Z restores it in one step. |
| AB6 | Select the Arrow tool and drag: an arrow appears, head at the release end, tail at the press. |
| AB7 | Drag an arrow right-to-left and bottom-to-top: the head is still at the release end. The arrow is not normalised. |
| AB8 | Click without dragging using the Arrow tool: no arrow is created. |
| AB9 | Draw a long arrow, then zoom out until the shaft is a few pixels: the head shrinks with it and never exceeds half the shaft. |
| AB10 | Draw a very short arrow: the head is proportionate, not a blob covering the whole shaft. |
| AB11 | Click an arrow's shaft at its midpoint: it selects. Click well off the shaft: it does not. |
| AB12 | Select a marker and an arrow in turn: **no resize handles** appear on either. |
| AB13 | Double-click a marker and an arrow: nothing happens; no vertex-editing mode. |
| AB14 | Properties panel on a marker: length 0, no area, type reads "Point". |
| AB15 | Properties panel on an arrow: a length matching its size, no area, the word "Length" not "Perimeter", type reads "Arrow". |
| AB16 | Use the show-annotation action on a marker: the view **recentres without changing zoom**. |
| AB17 | Place a marker and an arrow, save, close the slide, reopen: both come back, the arrow's direction preserved. |
| AB18 | Open the saved file in a text editor: `schemaVersion` still reads 1. |
| AB19 | On a read-only slide, click with the Point tool: refused with the status-bar message, no marker created. |
| AB20 | Escape with the Point tool active: reverts to the Pan tool, as other tools do. |
| AB21 | Place a marker over pale background and over dark tissue: visible against both. |
| AB22 | Delete a marker, Ctrl+Z, Ctrl+Y: it comes back, then goes again, in one step each. |
| AB23 | With the read-only slide of AB19 still open, select the **Rectangle** tool and drag: refused with the status-bar message, no rectangle created. This is the pre-existing defect `commitDrawDrag`'s new gate fixes — AB19 exercises the gate through the Point tool, and nothing otherwise drives the path the rectangle and ellipse tools have been using unguarded since persistence landed. The ellipse shares that path; one check covers both. |

**What was observed.** AB1–AB18 and AB20–AB22 passed; AB19 and AB23 are covered
above. The numbers worth keeping:

- **AB3** — the marker's disc measured 10×10 px with 88 coloured pixels at 1.6x
  (3.7%), and 10×10 px with 88 coloured pixels at 305.2x (693.7%). A 190-fold
  zoom range moved neither its size nor its position by a pixel.
- **AB4** — the grab radius is 6 px exactly: a click 6 px from the centre
  selects, 7 px does not. The disc is 10 px across plus a 1 px outline, so the
  target and the glyph share an edge. This is the one thing the parameterised
  disc radius was for, and it holds by construction rather than by coincidence.
- **AB2, AB5** — placement and movement are exact in slide coordinates: ten
  markers 200 screen px apart came out 5450.9 slide units apart every time, and
  a 200 px drag moved one marker by 5451.0 units with the other axis unchanged.
- **AB6, AB7** — tail is the press and head is the release, to the unit, in both
  directions. The right-to-left and bottom-to-top arrow stored `tail` greater
  than `head` on both axes and still drew its head at the release end: the pair
  is not normalised, which is the one thing an arrow cannot survive getting
  wrong.
- **AB9** — the head tracked the shaft down: 12 px of head at shafts of 1902,
  735, 284, 111 and 44 px; 8 px at a shaft of 18; 2 px at a shaft of 8. The
  fixed size governs until half the shaft falls below it and the fraction takes
  over, and the head never exceeded half the shaft at any zoom.
- **AB14, AB15** — a marker reads type Point, length 0.00 µm, no area; an arrow
  reads type Arrow, length 2.77 mm, no area, and the row is labelled Length, not
  Perimeter.
- **AB16** — the marker landed on the viewport centre to within a pixel and the
  zoom read 4.2x before and 4.2x after.
- **AB17, AB18** — close and reopen returned all twelve annotations with every
  arrow's direction intact, and the file on disk was byte-identical before and
  after the reopen, `schemaVersion` 1.

**One defect found, outside this round's code.** `"&Point Marker"` and
`"&Preferences..."` both declare `P` as their mnemonic in the Tools menu, and
both render with `P` underlined. `"P&olygon"` avoids the clash deliberately;
the new action did not. `"Point &Marker"` fixes it.

---

## 8. Deliberate absences

- **Arrow endpoint editing.** A selected arrow paints no handles; re-aiming
  means delete and redraw, which loses the annotation's id, label,
  classification and creation time. Chosen deliberately to keep this round to
  two geometry types, two tools and the glyph concept. The follow-up worth
  doing is not "add two handles" but generalising the six-exit drag machinery
  — written once for vertex editing, again for box resize — rather than
  writing it a third time.
- **Arrow modifiers** (§4.3).
- **Per-category counting** — the agreed next round (§1.1).
- **A configurable marker symbol set.** The glyph is a disc. Crosses, stars
  and numbered markers are a preferences feature, not a geometry one.
- **FR-ANN-07 ruler, FR-ANN-08 area, FR-ANN-09 angle.** These are measurement
  presentations over existing geometry, not new geometry types.
- **FR-ANN-10 text label.** Its own round; it needs in-place editing, fonts
  and a hit test over painted text.
- **Hiding a point's zero length in the properties panel.** The zero is
  truthful. A type test in the panel to conceal a true value is not worth the
  branch.

---

## 9. Risks and accepted trade-offs

| Risk | Disposition |
|---|---|
| `readGeometry` branch forgotten | The only unguarded site. Mitigated by two round-trip tests, which is the only thing that can force it (§2.6). |
| A count of markers is not yet available per category | Accepted and named in §1.1; the agreed next round. |
| 50-step undo limits a long counting pass | Accepted (§4.5). FR-ANN-18's floor is met. |
| Marker invisible against some tissue | Mitigated by the black underlay the existing handles already use; AB21 checks it. |
| Arrowhead geometry wrong at extreme zoom | The one real calculation here, and it is in core precisely so it is tested (§3.4); AB9 and AB10 check it by hand. |
| A future shape forgets `withBoxReplaced` | Already mitigated by `std::visit` (box-resize spec §3.1), and this round is the first demonstration of it working. |

---

## 10. Requirements

| Requirement | Status |
|---|---|
| FR-ANN-05 — Point Marker, single-click, screen-space size | **Complete** with this round |
| FR-ANN-06 — Arrow, click-drag tail to head | **Complete** with this round |
| FR-ANN-10 — Text Label | Still open; §8 |
| FR-ANN-07/08/09 — ruler, area, angle | Still open; measurement presentations, §8 |
| FR-ANN-18 — 50 undo levels | Unchanged and met; §4.5 notes what it implies for counting |
