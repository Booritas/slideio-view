# Ruler and Angle Measurement — Design

Stage 3 item 3, the two measurement tools still missing. FR-ANN-07 (Ruler,
Must), FR-ANN-09 (Angle, Should); UI design §4.2 "Ruler / Distance
Measurement" and "Angle Measurement".

## 1. Why

A pathologist measures a tumour's greatest dimension, a margin, a depth of
invasion. Today the only way is to draw a freehand line and read its Length in
the properties panel — which is in a dock, not on the slide, and does not
travel into a screenshot. A ruler draws the line and writes its length beside
it, on the slide, in the units of the calibration that has just landed. The
angle tool does the same for an angle (gland orientation, invasion front).

Both are **measurements, not findings**: the number is the point of them.

## 2. Goals, non-goals, anti-goals

**Goals**

- A **Ruler** shape: two points; its length drawn beside it on the slide.
- An **Angle** shape: a vertex and two ray ends; its angle in degrees drawn at
  the vertex.
- Both measured through the one `core::Calibration` (CLAUDE.md, "Every
  physical number reads one `core::Calibration`") and labelled with its
  source exactly as the panel and scale bar are.
- Both saved, undone, moved, deleted, exported and imported like every other
  shape.

**Non-goals**

- **Area measurement (Shift+M).** The polygon already is one: the properties
  panel shows its area and perimeter, the CSV exports them. An on-slide area
  label for polygons is a separate, later change.
- **Editing a ruler's or angle's points after creation.** Move and delete work
  as for every shape; to change a measurement, draw it again. Endpoint handles
  (for rulers, angles and arrows together) are a follow-up.
- Snapping, multi-segment rulers, reflex angles.

**Anti-goals** — it fails even if it works when:

- A length or angle on the slide disagrees with the panel or the CSV for the
  same annotation.
- An on-slide number on a manually calibrated or uncalibrated slide does not
  say so.
- A ruler or angle changes a clinical count.
- An on-slide label is drawn with `drawText` (CLAUDE.md, text annotation:
  glyph-cache textures come out garbled over the GL tiles).

## 3. The shapes

```cpp
struct RulerGeometry { PointF start; PointF end; };
struct AngleGeometry { PointF first; PointF vertex; PointF second; };
```

Appended to the `AnnotationGeometry` variant, with `AnnotationType::Ruler` and
`AnnotationType::Angle` appended to the enum — never inserted, so stored
indices and the existing order do not move.

- **Separate types, not degenerate polylines**, for the arrow's reason: `typeOf()`
  derives the type from the geometry, and a two-point polyline, an arrow and a
  ruler would otherwise be indistinguishable.
- **Point order is stored as drawn and never normalised.** It does not change
  a ruler's length, but it is what the user drew and the GeoJSON writes it.
- `outline()`: a ruler is the open path `{start, end}`; an angle is the open
  path `{first, vertex, second}`. So `boundingBox`, `hitTest` (within
  tolerance of either segment), `translated`, the painter's line, and the
  GeoJSON geometry all come from the same points.
- `hasWellDefinedArea`: false for both.
- Every exhaustive `std::visit` site answers both (the compiler names them —
  fourteen when the text landed); `replaceBox` returns `nullopt`;
  `readGeometry` and `readExportedShape` are hand-kept chains and need their
  branches, pinned by round-trip tests.

## 4. Measuring

- **Length** of a ruler: `core::measure()` already returns an open outline's
  length; a ruler is a two-point open outline, so the panel, the CSV and the
  on-slide label all read `measure()` — one function, as the anti-goal
  requires.
- **Angle**: a new `core::angleDegrees(const AngleGeometry&, PixelSize)`,
  computed **after scaling both rays by the pixel size**. On non-square
  pixels the physical angle differs from the pixel-space one, and the physical
  one is the true one. With no calibration it is measured in pixel space, as
  every uncalibrated number is. Range **[0, 180]**, the unsigned angle between
  the rays. A zero-length ray has no direction: the angle is **absent**, not 0.
- `ShapeMetrics` gains `std::optional<double> angleDegrees`, filled only for
  an angle, so every caller of `measure()` gets it from the same call.

## 5. Drawing them

**Ruler (`L`)** — click-drag from start to end, exactly as the arrow is drawn
(press point is `start`, release point is `end`; the existing minimum-drag
rule refuses a click). The preview is the dashed line **with its live length
label**, so the user sees the number while choosing the end.

**Angle (`G`)** — three points:

1. Press at the **vertex**, drag, release at the **first ray's end**. A press
   released without a drag is refused, as for the ruler.
2. Move: the second ray follows the cursor from the vertex, with the **live
   angle** at the vertex.
3. Click at the **second ray's end**: the angle is committed.

Between steps 1 and 3 the angle is **pending** — like a pending polygon trace
it is not an annotation, is never saved, survives focus loss, and is discarded
on Escape, a tool change and a slide close. Ctrl+Z during it removes the first
ray (back to nothing), as Ctrl+Z removes a trace's last vertex.

**Shortcuts.** The UI design gives the ruler `M`, but `M` is the Point Marker
here and has been since group AB; the ruler takes **`L`** (Line). The angle
takes **`G`**, as designed. Both join the viewport-scoped bare-letter
shortcuts, the Tools menu, `applyAnnotationAvailability` and every site the
Text tool's action was twinned into.

Both tools commit through the same `annotationsEditable()` gate, one undo step
each ("Draw ruler", "Draw angle"), and select what they created — so, as the UI
design asks, the measurement just made stays selected while the tool stays
active for the next one.

## 6. On-slide labels

Each ruler and angle carries a **label in screen space**, drawn whenever the
shape is:

- **Ruler**: at the line's midpoint, offset perpendicular to it by a fixed
  number of screen pixels, on the side away from the line's left-to-right
  normal so it reads upright; it never overlaps the line itself.
- **Angle**: at the vertex, offset along the **bisector** of the two rays, on
  the outside of the angle.
- **Text**: the formatted value in the display unit preference (µm or mm,
  `core::formatLength`) or degrees to one decimal (`47.3°`). The calibration
  decides the suffix exactly as the scale bar does: `Metadata` → nothing,
  `Manual` → `" (manual)"`, `None` → the value in **px** with a warning icon
  beside it. An angle reads in degrees whatever the calibration, but on an
  uncalibrated or manual slide it carries the same marker, because a
  non-square-pixel angle depends on it.
- **Drawn as filled `QPainterPath` outlines on a white plate** at the text
  annotation's legibility floor (`ui::textPlateOpacity`), outlined in the
  annotation colour — the text annotation's plate and its lesson.
- The label's placement is a pure function in ui (`measurementLabelPlacement`,
  taking the shape's screen points and the label's measured size) so a test
  reaches it; the painter only draws where it says.
- `ViewportWidget` needs the calibration to write these, so
  `MainWindow::Impl::refreshCalibration()` pushes `currentCalibration` to the
  viewport as it does to every other surface (`ViewportWidget::setCalibration`).

A ruler also paints short **perpendicular end caps** in screen pixels, so its
ends read as measured points and not as an arrow's tail; a new
`ScreenDecoration::Kind::EndCaps`, sized in core like the arrowhead.

## 7. Everywhere else

| Surface | Ruler | Angle |
|---|---|---|
| Properties panel | Type "Ruler", Length row | Type "Angle", **Angle** row in place of Length (an angle's path length means nothing) |
| Counting strip / counts export | **excluded**, as the text is | **excluded** |
| Measurements CSV | a row: `length_px`, `length_um` | a row with **`angle_deg`**; length empty |
| GeoJSON export | `LineString` [start, end], `slideio.type` `"ruler"` | `LineString` [first, vertex, second], `slideio.type` `"angle"` |
| GeoJSON import | a 2-position LineString with type `"ruler"` comes back a ruler | a 3-position LineString with type `"angle"` comes back an angle; anything else is the ordinary polyline (the geometry wins) |
| ASAP XML import | ASAP's `Measurement` **is** ASAP's ruler: one with exactly two coordinates becomes a ruler (today it becomes a two-point line); any other count stays a line. Its content seed is unchanged, so one imported before keeps its id and a re-import skips it rather than adding a ruler beside it | — |
| Annotation file | `"type": "ruler"`, `{"start", "end"}` | `"type": "angle"`, `{"first", "vertex", "second"}` |
| Older build | refuses the file naming the type, leaves it intact (the `schemaVersion` rule) | same |

**The CSV gains one column, `angle_deg`, after `length_um`** — the third
header change in three rounds (the calibration column came first); it goes in
the release notes with `calibration_source`. Written with the same fixed
notation and never through `csvField`, as every number is. Empty for every
shape but an angle, and for a degenerate angle.

**Why excluded from the tally**: a ruler classified "Tumor" is a tumour's
measurement, not a second tumour. Counting it would change a clinical count,
which is an anti-goal.

## 8. Tests

- core: geometry round trip of both through the serializer (pins
  `readGeometry`), `outline`, `hitTest` on either ray and off both,
  `translated` keeps point order, `typeOf`; `angleDegrees` — 90° on a right
  angle, 180° on a straight line, 0° on overlapping rays, absent on a
  zero-length ray, **non-square pixels change the angle** (a 45° pixel-space
  angle at 1 × 2 µm/px is atan(2) ≈ 63.43°); `measure()` gives a ruler's length
  and an angle's `angleDegrees`; GeoJSON export and import of both, and a
  3-position LineString without the type importing as a polyline.
- app: an ASAP `Measurement` with two coordinates imports as a ruler, one with
  three as a polyline.
- ui: the tally excludes both; the CSV header has `angle_deg` after
  `length_um` and the angle row fills it; shape names; the label placement
  (ruler offset perpendicular and away from the line; angle on the bisector,
  outside); the label text per calibration source.

## 9. Manual checks — group AL

Written before the run.

| | Check | Result |
|---|---|---|
| AL1 | `L` selects the Ruler, `G` the Angle; both disabled on a read-only slide. | **Pass.** `L` drew a ruler and `G` an angle; on a second, read-only window on CMU Tools > Ruler and Angle were disabled, on the first enabled. |
| AL2 | Drag a ruler across a gland: the length is on the slide beside the line, matches the panel's Length and the CSV `length_um`. | **Pass.** On the slide "230 µm"; the panel 230 µm; the CSV `length_um` 229.64 (460.19 px at 0.499 µm/px). |
| AL3 | With a manual calibration the label reads "(manual)"; on an uncalibrated slide it reads px with a warning icon. Changing the calibration updates the label at once. | **Pass.** At a manual 0.5 µm/px the label changed at once to "230 µm (manual)"; on CMU-1-Small-Region-page-2.tif (no calibration) a ruler read "72 px" with the warning icon. |
| AL4 | Zoom 1× to 40×: the label stays the same size on screen, beside the line, never on it. | **Pass.** Zoomed five wheel notches in and back at the ruler: the label kept its size, beside the line ("486 µm (manual)" at 1 × 2 µm/px). |
| AL5 | Draw an angle by press-drag-release then click: the live angle shows while choosing the second ray; the committed value matches the panel's Angle and the CSV `angle_deg`. | **Pass.** The live label showed while the second ray followed the cursor; committed, the label, the panel's Angle row and the CSV `angle_deg` agreed. |
| AL6 | A right angle drawn along the slide's axes reads 90.0°; on a slide calibrated 1 × 2 µm/px a 45° pixel angle reads about 63.4°. | **Pass.** A right angle along the axes read "90.0°" (the smoke run); at 1 × 2 µm/px a 45° pixel angle read 63.4° on the slide and in the panel, and 63.43494882 in the CSV. |
| AL7 | Escape, a tool change and Ctrl+Z each end a pending angle without creating one; a lone click with either tool creates nothing. | **Pass.** Escape, a tool change (`R`) and Ctrl+Z each ended a pending angle; a lone click with each tool, and two lone clicks with the Angle, created nothing — the scene stayed at 2 new shapes. |
| AL8 | Undo removes a ruler or angle in one step; redo restores it; move and delete work. | **Pass.** Undo removed the angle, redo restored it; a drag moved the ruler (824.6, 2017.1 → 1050.2, 2242.7) and undo put it back; Delete removed it and undo restored it. |
| AL9 | A ruler and an angle classified Tumor do not change the Tumor count, in the strip or the counts export. | **Pass.** Both classified Tumor (set in the file with the viewer closed, as in AK10): Export Counts read Tumor 0, Unclassified 3. |
| AL10 | GeoJSON export carries both with their `slideio.type`; importing that file into an empty scene brings back a ruler and an angle. | **Pass.** The GeoJSON carried a 2-position LineString tagged `ruler` and a 3-position one tagged `angle`; imported into CMU-1-Small-Region-page-2.tif they came back as a ruler and an angle. |
| AL11 | A pre-change build refuses a file holding a ruler, naming `ruler`, and leaves it byte for byte. | **Pass.** The build from before Task 4 (`D:	mppre-ruler-viewer`) refused with "unknown geometry type 'ruler' -- this file may have been written by a newer version of the viewer"; the file's hash was unchanged. |

Run 2026-10-24 against `229334e` on the main monitor, **11 of 11 pass**; CMU's annotation file was restored from its backup and the CMU calibration override set for AL3/AL6 removed.

## 10. Left to the builder

- The exact label offset, font size (the text annotation's 12 pt is the
  default) and end-cap length.
- Whether the pending angle reuses the multi-click pending state or has its
  own, as long as §5's behaviour holds.
- The warning icon (the scale bar's `SP_MessageBoxWarning` is fine).
