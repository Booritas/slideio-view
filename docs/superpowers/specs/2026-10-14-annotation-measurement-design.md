# Annotation measurement and calibration

**Sub-project G of Stage 3.** Written 2026-10-14 against `main` @ `500dd4d`.

Shows the area and perimeter of the selected annotation in physical units, and
the length of an open freehand trace. The numbers are derived from geometry and
never stored, so the serializer and `schemaVersion` are untouched.

## Intent

The numbers are for **reporting and export**, not only for orientation while
reading. That was the first thing settled, and it decides the rest: a figure
that may end up in a report or a research dataset has to be defensible and
reproducible, so the application must never show an area that silently
disagrees with the region shaded on screen.

Everything below follows from that. Where a shape's area is well-defined, it is
computed exactly. Where it is not, the application says so rather than printing
a plausible number.

## Scope

**In:** area, perimeter and length in `core`; self-intersection detection; unit
selection and formatting; two read-only rows in the annotation properties panel;
calibration pushed from the open slide.

**Out:**

- **A measurement column in the annotation list.** Nothing asks for it, and the
  list rebuilds on every model change where the panel does not.
- **Storing measurements in the annotation file.** They are derived. Storing
  them would mean a schema change, and this project refuses a file with a higher
  `schemaVersion` outright, so a bump breaks every shipped build during a
  staggered rollout.
- **Export.** Sub-project H owns it. When it arrives it must call these same
  `core` functions rather than reimplement the arithmetic.
- **A unit preference.** Units are chosen by magnitude; see §2.

## 1. What core computes

Three pure functions, Qt-free, reachable from `core-tests`:

```cpp
/// Metres per pixel along each axis. The default measures in pixel units.
struct PixelSize
{
    double x = 1.0;
    double y = 1.0;
};

struct ShapeMetrics
{
    /// Absent for an open path, and for a closed path that intersects itself.
    std::optional<double> area;
    /// Perimeter for a closed shape, total length for an open one.
    double outlineLength = 0.0;
    bool closed = false;
    bool selfIntersecting = false;
};

ShapeMetrics measure(const AnnotationGeometry& geometry, PixelSize pixelSize);
bool isSelfIntersecting(const PolygonGeometry& polygon);
```

### Why `measure` takes the calibration

Pixels are not necessarily square: `SlideInfo` carries `resolutionX` and
`resolutionY` separately. An area scales by `x·y`, but a perimeter does not
scale by any single factor — each segment scales differently with its
direction. The only correct order is to scale the vertices and then measure,
which is what passing `PixelSize` in allows. `PixelSize{1, 1}` yields pixel
units, which is also what most tests use, because the arithmetic stays
checkable by hand.

### Per geometry type

| Geometry | Area | Outline length | Can self-intersect |
|---|---|---|---|
| `RectangleGeometry` | `w·h` | `2(w+h)` | No |
| `PolygonGeometry`, `closed == true` | `abs(shoelace)` when simple, absent otherwise | perimeter, including the closing edge | Yes |
| `PolygonGeometry`, `closed == false` | absent, always | length, excluding any closure | Not tested — see below |

`closed` is carried out in `ShapeMetrics` so the caller knows whether
`outlineLength` means "perimeter" or "length". That mirrors `PolygonGeometry`,
which already uses one flag to mean two shapes.

### Self-intersection

A pairwise segment test, skipping adjacent pairs, including the closing edge
for a closed polygon. Two degenerate cases are decided here rather than left to
discovery:

- Segments meeting at a **shared endpoint** are not an intersection. Every
  polygon would otherwise report as self-intersecting.
- **Collinear overlapping** segments — a trace that doubles back exactly along
  itself — are an intersection. That shape's interior is genuinely ill-defined,
  and declining to measure it is the honest answer.

**An open path is never tested for self-intersection.** It has no area either
way, so the answer would change nothing, and `selfIntersecting` stays false even
for an open trace that visibly crosses itself. That is a deliberate narrowing of
the field's meaning: it means "this is why the area is missing", not "this shape
is geometrically simple".

The narrowing pays for itself. Open freehand lines are exactly the shapes that
carry thousands of vertices — a hand-traced line samples every mouse move, where
a polygon is clicked vertex by vertex. Skipping the O(n²) test for them removes
the worst case entirely, and leaves it running only on polygons, which are
small by construction. §3 covers the remaining cost.

### Why not an exact area for self-intersecting shapes

Two alternatives were considered and rejected.

Computing the even-odd area exactly means finding every self-intersection,
splitting edges, building the planar subdivision and summing the odd-parity
faces. It is a real computational-geometry component with well-known
floating-point robustness traps, and its failure mode is the worst one
available here: a subtly wrong number that looks entirely plausible. The user
confirms self-intersecting traces are rare, so that risk would be carried
permanently to serve a rare accident.

`QPainterPath::simplified()` resolves self-intersections using the fill rule and
would do the job — but it is Qt Gui. `core` links no Qt at all and `app` gets
only Qt Core, so reaching for it would push the arithmetic into `ui`, where no
test can see it. That trade is backwards for the one part of this feature that
most needs testing.

## 2. Calibration and units

### How calibration reaches the panel

`AnnotationPropertiesPanel::setPixelSize(core::PixelSize)`, called from the
existing `slideOpened` handler in `MainWindow.cpp` alongside
`zoomIndicatorWidget->setResolution(info.resolutionX)`. That block already has
`const auto& info = viewportWidget->slideInfo()` in scope and is already the
designated place for populating panel content — its comment notes that
visibility and position are deliberately not touched there. No new signal.

### Uncalibrated slides show pixel units

When `resolutionX` or `resolutionY` is zero or negative the slide carries no
physical calibration. Measurements are then shown in pixels, explicitly
labelled: `12480 px²`.

This deliberately diverges from `ScaleBarWidget`, which **hides** itself when
`metersPerPixelNative <= 0`. The divergence is intentional and the reasoning
differs: a scale bar is a physical ruler, and a ruler with no physical scale is
meaningless. A measurement is a number. `12480 px²` is real, reproducible and
useful for export; the unit label is what keeps it from being mistaken for
mm². Suppressing measurements entirely on an uncalibrated slide would be a
worse answer for the stated purpose.

### Units are automatic, not configurable

A preference is one more thing to set, to persist, to get wrong, and to
disagree with a colleague's export.

| Quantity | Below threshold | At or above |
|---|---|---|
| Length | `µm` | `mm` at 1 mm |
| Area | `µm²` | `mm²` at 1 mm² |

Three significant figures throughout, so the mantissa always reads 1–999:
`245 µm`, `1.23 mm`, `872 µm²`, `4.56 mm²`. Fixed significant figures is what
makes two people measuring the same shape write down the same number.

No cm² and no nm. A whole slide is roughly 15 mm across, so mm² is the top of
the useful range, and a trace shorter than a micron is a stray click.

### Formatting

```cpp
enum class MeasurementUnits { Physical, Pixels };

std::string formatLength(double value, MeasurementUnits units);
std::string formatArea(double value, MeasurementUnits units);
```

`Physical` takes metres and square metres; `Pixels` takes pixels and square
pixels. The enum exists so a caller cannot pass one where the other is expected
and get a plausible wrong answer.

These return UTF-8 `std::string` and are **locale-free**: no thousands
separators, `µ` spelled as the UTF-8 bytes the codebase already uses in
`ScaleBarWidget`. Grouping and localisation are presentation decisions and this
layer has no business making them. The panel converts with
`QString::fromStdString` at the boundary.

## 3. Presentation

Two read-only rows in the annotation properties panel, below the existing Type,
Author, Created and Modified rows.

| Shape | Rows shown |
|---|---|
| Rectangle, closed polygon | **Area**, **Perimeter** |
| Open freehand line | **Length** only; the Area row is hidden |

The second row's label changes between "Perimeter" and "Length" with the shape.
The Area row is hidden through `QFormLayout::setRowVisible`, available since
Qt 6.4 (this project is on 6.7.3).

A self-intersecting trace shows the Area row with the value
`unavailable (self-intersecting)` — not a blank, and not a tooltip. A blank
reads as "not computed yet", and a tooltip only reaches someone who hovers.
Since these numbers may reach a report, the reason a number is missing has to be
as visible as the number would have been. Perimeter still shows normally,
because it is well-defined either way.

That string is panel text and goes through `tr()`, unlike the `core` formatters
in §2, which stay locale-free because they produce numbers rather than prose.
The row labels "Area", "Perimeter" and "Length" are `tr()` too.

### The self-intersection cache

The panel repopulates on `annotationChanged`, and `ViewportWidget` emits that on
**every mouse-move of a drag**. Area and perimeter are O(n) and do not care. The
self-intersection test is O(n²): roughly 1 ms at 1,000 vertices, but around
125 ms at 5,000. Recomputing it per frame with the dock open would make dragging
a long freehand trace unusable.

The fix falls out of the geometry: **self-intersection, area and perimeter are
all translation-invariant.** Moving a shape cannot change any of them. So the
panel caches the self-intersection result keyed by annotation id and recomputes
only the cheap parts. The cache is cleared on `cleared` and `modelReset`.

**This cache carries an obligation.** It is correct only while translation is
the sole geometry mutation, which is true today. Vertex editing is on the
roadmap, and **when it lands it must invalidate this cache.** A stale "not
self-intersecting" after a reshape would show an area that disagrees with the
shaded region — precisely the failure this whole design exists to prevent. The
cache's declaration carries this warning in a comment; this paragraph is the
other half of the record.

## 4. Testing

### Automated — `tests/core/AnnotationMeasurementTest.cpp`

`measure()`:

- A 10×20 rectangle: area 200, perimeter 60.
- **Anisotropy on a diagonal.** A 45° segment under `PixelSize{1, 2}` has length
  `√(dx² + (2·dy)²)`, which is neither `L` nor `2L`. This one test catches the
  entire class of "scaled the length by a single factor" bugs, which is what the
  API shape exists to prevent.
- A closed triangle against a hand-computed area.
- An open polyline whose length **excludes** the closing edge.
- A figure-eight: `selfIntersecting` true, `area` absent, perimeter correct.
- Anisotropic rectangle: `PixelSize{2, 3}` on a 10×20 rectangle gives area
  `(10·2)·(20·3) = 1200` and perimeter `2(20 + 60) = 160`.

`isSelfIntersecting()`:

- Convex and concave simple polygons: false.
- A figure-eight: true.
- A crossing that exists **only because of the closing edge**: true. This pins
  that the closure is tested at all.
- Segments sharing an endpoint: false.
- Collinear overlapping segments: true.

`formatLength` / `formatArea`:

- Threshold boundaries in both directions: `999 µm` stays µm, `1000 µm` becomes
  `1.00 mm`; `999999 µm²` stays µm², `1000000 µm²` becomes `1.00 mm²`.
- Three-significant-figure rounding.
- Both pixel forms, confirming no thousands separator appears.

### Manual — group W

The panel rows, the row hiding and the cache are inside a widget, and no test
binary in this repo creates a `QApplication`. They get a check list, following
the P, U and V groups. Run against an installed binary, with the viewer killed
before `cmake --install` and the install verified by timestamp — a running
viewer makes that a silent no-op that still exits 0.

| # | Check |
|---|-------|
| W1 | A rectangle of known size on a calibrated slide shows Area and Perimeter, and the numbers match a hand calculation from the slide's `resolutionX`/`resolutionY` |
| W2 | An open freehand trace shows **Length** and no Area row at all |
| W3 | A closed polygon shows **Area** and **Perimeter** |
| W4 | A deliberately self-intersecting trace shows `unavailable (self-intersecting)` in the Area row, with Perimeter still populated |
| W5 | A slide with no calibration shows measurements in `px` and `px²`, labelled |
| W6 | A shape crossing 1 mm switches from µm to mm, and one crossing 1 mm² from µm² to mm² |
| W7 | Selecting an annotation pushes no undo step and does not restamp `modifiedAt` — measurements are derived and must mutate nothing |
| W8 | **Dragging a long freehand trace with the properties dock open stays responsive.** This is what catches a mistake in the §3 cache |
| W9 | Measurements survive a slide close and reopen. **Write the numbers down first — the reopened values must be identical, not merely present.** That is what catches a calibration failing to re-arrive through `setPixelSize`: without it the panel falls back to pixel units silently, and W5 would not distinguish that |

### Added after the whole-branch review

Each exists because the first nine would pass while something specific was broken.

| # | Check |
|---|-------|
| W10 | Draw a region about 300 µm across and read Area: it must show **three** significant figures, not five. Then a region just under 1 mm across must promote to `1.00 mm²` rather than reading `999999 µm²`. W1 passes either way, because a hand calculation does not care how many digits are displayed |
| W11 | Select a closed annotation, deselect; select an open trace, deselect. With nothing selected the Area row must reappear and the caption return to "Perimeter", both values empty |
| W12 | Delete the selected annotation with the dock open, then Ctrl+Z. Area and Perimeter must come back identical, with no `unavailable` flicker in between |
| W13 | Drag a closed polygon of a few hundred vertices with the dock open and watch the Area field: it must not change by a single digit. W8 covers whether that path stays *responsive*; nothing else covers whether it stays *correct* |
| W14 | Drag the self-intersecting trace from W4 and confirm `unavailable (self-intersecting)` persists rather than a number appearing. The cache stores `true` as readily as `false` |

**Ordering note for W5:** run it immediately after a calibrated slide in the same
session, not from a cold start. One `setPixelSize` call clears both the
calibration and the cache, so only a slide-to-slide transition shows both
resetting.

## Compliance note

Measurements are derived from annotations, which are clinical findings on
patient images. Nothing here changes what is stored or where — the same
per-(slide, scene) JSON files, the same `schemaVersion` 1 — so this sub-project
introduces no new PHI-handling surface. The numbers are computed on demand and
held only in the panel's widgets.

One point worth stating for the regulated context this runs in: because these
figures are intended for reporting, the decision not to display an area the
application cannot compute exactly is a safety property, not a limitation. An
approximate measurement presented without qualification in a clinical record is
a worse outcome than no measurement, and the EU MDR and EU AI Act obligations
around accuracy and transparency point the same way.
