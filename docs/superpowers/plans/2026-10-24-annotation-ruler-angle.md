# Ruler and Angle Measurement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Ruler (`L`) and Angle (`G`) shapes that measure through the one `core::Calibration` and label themselves on the slide.

**Architecture:** Two variant alternatives in core with their measurement (`measure()` gains `angleDegrees`), file and GeoJSON support; pure ui rules for the tally, the CSV and on-slide label placement/text; then the tools, the pending angle and the label painting in `ViewportWidget`, which now receives the calibration from `MainWindow::Impl::refreshCalibration()`.

**Tech Stack:** C++17, Qt 6.7 Widgets, Catch2 v3, CMake.

**Spec:** `docs/superpowers/specs/2026-10-24-annotation-ruler-angle-design.md`

## Global Constraints

- New variant alternatives and enum values are **appended**: `RulerGeometry`, `AngleGeometry`; `AnnotationType::Ruler`, `AnnotationType::Angle`.
- `RulerGeometry{PointF start; PointF end;}`, `AngleGeometry{PointF first; PointF vertex; PointF second;}`; point order stored as drawn, never normalised.
- File type names `"ruler"` (`start`, `end`) and `"angle"` (`first`, `vertex`, `second`); `schemaVersion` unchanged.
- Angle range [0, 180] degrees, measured after scaling by the pixel size; absent (not 0) when either ray has zero length.
- Shortcuts `L` (Ruler) and `G` (Angle), `Qt::WidgetWithChildrenShortcut` on the viewport like the other tools.
- On-slide labels are filled `QPainterPath`s on a plate at `ui::textPlateOpacity`, **never `drawText`**.
- Label suffix by source: Metadata `""`, Manual `" (manual)"`, None → value in px plus a warning icon (an angle keeps degrees but still carries the marker).
- CSV column `angle_deg` right after `length_um`; numbers never through `csvField`; no floating-point `std::to_chars`/`from_chars` added.
- Rulers and angles are excluded from `ui::categoryTally`.
- Non-ASCII literals as byte escapes (`°` is `\xC2\xB0`, `µ` `\xC2\xB5`); MSVC has no `/utf-8`.

## Review Focus

1. **A ruler or angle drawn on an uncalibrated slide, then calibrated** — the on-slide label must change at once to µm and "(manual)", not keep the px value until a repaint or reopen. Owned by Task 4 (`ViewportWidget::setCalibration` calls `update()`); checked as AL3.
2. **A degenerate angle** (both clicks on the vertex, or a first ray of zero length that slipped past the drag minimum through an import) — no NaN anywhere: the panel shows no angle, the CSV cell is empty, the label is omitted. Pinned in Task 1 (`angleDegrees` absent) and Task 3 (CSV empty cell, label text for an absent value).
3. **A ruler drawn exactly vertical or horizontal, and one drawn right-to-left** — the label sits beside the line, upright and readable, never on it. Pinned in Task 3's placement tests (vertical, horizontal, reversed).
4. **A pending angle when the slide closes or the tool changes mid-way** — nothing is saved, nothing is left drawn over the next slide. Owned by Task 4 (discard in `resetAnnotationState` and `setActiveTool`); checked as AL7.
5. **An existing file's two-point ASAP Measurement re-imported** — keeps its id, so it is skipped, not duplicated as a ruler beside the old line. Pinned in Task 2 (the seed for a two-point Measurement is unchanged).

---

### Task 1: The shapes in core

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`, `src/core/src/AnnotationGeometry.cpp`, `src/core/include/slideio/viewer/core/AnnotationMeasurement.h`, `src/core/src/AnnotationMeasurement.cpp`, `src/core/include/slideio/viewer/core/AnnotationDecoration.h`, `src/core/src/AnnotationDecoration.cpp`, `src/core/src/BoxResize.cpp`, `src/core/src/AnnotationSerialization.cpp`, `src/core/src/AnnotationGeoJson.cpp`, `src/ui/src/AnnotationShapeName.cpp` (the compiler will demand it)
- Test: `tests/core/AnnotationGeometryTest.cpp`, `tests/core/AnnotationMeasurementTest.cpp`, `tests/core/AnnotationSerializationTest.cpp`, `tests/core/AnnotationGeoJsonTest.cpp`, `tests/core/AnnotationGeoJsonImportTest.cpp`, `tests/core/AnnotationDecorationTest.cpp`, `tests/ui/AnnotationShapeNameTest.cpp`

**Interfaces:**
- Produces: the two structs and enum values (Global Constraints); `ShapeMetrics::angleDegrees` (`std::optional<double>`); `[[nodiscard]] std::optional<double> angleDegrees(const AngleGeometry& angle, PixelSize pixelSize);` in `AnnotationMeasurement.h`; `ScreenDecoration::Kind::EndCaps` (size = half-length of each cap in screen px, anchor = `start`; the painter draws a cap at both ends perpendicular to the line) with `inline constexpr double kRulerEndCapScreenPixels = 6.0;`; shape names "Ruler" and "Angle".

- [ ] **Step 1: Write the failing tests:**
  - geometry: `outline` of a ruler is the open `{start, end}`, of an angle the open `{first, vertex, second}`; `hitTest` hits within tolerance of either angle ray and misses off both; `translated` keeps the point order; `typeOf` gives Ruler/Angle; `hasWellDefinedArea` false.
  - measurement: `measure(ruler {0,0}-{3,4}, {1,1}).outlineLength == 5`; `angleDegrees` 90 on rays (1,0)/(0,1), 180 on (1,0)/(-1,0), 0 on (1,0)/(2,0), absent when `first == vertex`; **`angleDegrees` of rays (1,0) and (1,1) at pixel size {1, 2} ≈ 63.4349** (atan(2)); `measure(angle).angleDegrees` equals `angleDegrees`, and is absent for every non-angle shape.
  - serialization: round trip of a ruler and an angle preserves every point in order; a non-finite point is refused on write.
  - decoration: a ruler asks for `EndCaps` sized `kRulerEndCapScreenPixels`; an angle asks for nothing.
  - GeoJSON export: a ruler is a 2-position `LineString` with `slideio.type` `"ruler"`, an angle a 3-position `LineString` [first, vertex, second] with `"angle"`; import of each comes back the same type and points; a 3-position `LineString` without the type imports as a polyline; a `"ruler"`-tagged 3-position LineString imports as a polyline (the geometry wins).
  - shape names: "Ruler", "Angle".
- [ ] **Step 2: Run** `cmake --build build/build --config Release --target slideio-viewer-core-tests` — Expected: compile errors on the unknown types.
- [ ] **Step 3: Implement** — add the structs and enum values, then answer every site the compiler names (record the count), plus the hand-kept `readGeometry` and `readExportedShape` branches.
- [ ] **Step 4: Run** `build/build/tests/Release/slideio-viewer-core-tests.exe` then `ctest --test-dir build/build -C Release` — Expected: all pass, 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Add the ruler and angle shapes to core, their file format and GeoJSON"`

### Task 2: ASAP's Measurement is a ruler

**Files:**
- Modify: `src/app/src/AsapXmlImport.cpp` (the `Kind::Measurement` case, ~line 176)
- Test: `tests/app/AsapXmlImportTest.cpp` (the ASAP reader's existing test file; check `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: Task 1's `RulerGeometry`.

- [ ] **Step 1: Write the failing tests:** `"a two-point ASAP Measurement imports as a ruler"` (start and end in document order); `"a three-point ASAP Measurement stays a polyline"`; `"a Measurement's seed does not depend on what it imports as"` — the `idSeeds` entry for a two-point Measurement equals the value it had before this change (compute it once from the current build in the test's comment, or assert it equals the seed of the same coordinates under the old mapping helper if one exists; at minimum assert it is unchanged across the type change by comparing with a hard-coded expected SHA-1 taken before Step 3).
- [ ] **Step 2: Run** `ctest --test-dir build/build -C Release -R app-tests --output-on-failure` after building `slideio-viewer-app-tests` — Expected: the ruler test fails (a polyline comes back).
- [ ] **Step 3: Implement** — exactly two coordinates → `RulerGeometry{v[0], v[1]}`; otherwise unchanged.
- [ ] **Step 4: Run** the same — Expected: pass; full ctest 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Import a two-point ASAP Measurement as a ruler"`

### Task 3: The ui rules — tally, CSV, labels

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/MeasurementLabel.h`, `src/ui/src/MeasurementLabel.cpp`
- Modify: `src/ui/src/AnnotationListPolicy.cpp` (`categoryTally`), `src/ui/src/AnnotationMeasurementExport.cpp`, `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/ui/MeasurementLabelTest.cpp`, `tests/ui/AnnotationListPolicyTest.cpp`, `tests/ui/AnnotationMeasurementExportTest.cpp`

**Interfaces:**
- Consumes: Task 1's types and `ShapeMetrics::angleDegrees`; `core::Calibration`, `core::formatLength`, `core::MeasurementUnits`.
- Produces (namespace `slideio::viewer::ui`, pure):
  - `inline constexpr double kMeasurementLabelOffsetScreenPixels = 10.0;`
  - `[[nodiscard]] QRectF rulerLabelRect(QPointF startScreen, QPointF endScreen, QSizeF labelSize);` — centred on the midpoint pushed `kMeasurementLabelOffsetScreenPixels` plus half the label's extent along the perpendicular, on the side of **smaller screen y** (above) for a non-vertical line and the side of **larger screen x** (right) for a vertical one, whatever the drawing direction.
  - `[[nodiscard]] QRectF angleLabelRect(QPointF firstScreen, QPointF vertexScreen, QPointF secondScreen, QSizeF labelSize);` — centred on the vertex pushed along the **negated** bisector of the two unit rays (outside the angle) by the offset plus half the label's diagonal; for a 180° angle (bisector of opposite rays is zero) along the perpendicular to the first ray, above.
  - `[[nodiscard]] std::string rulerLabelText(double lengthPixels, const core::Calibration& calibration);` (the units follow the source: `Physical` in metres when calibrated, `Pixels` for None, through `core::formatLength`) and `[[nodiscard]] std::string angleLabelText(std::optional<double> degrees, const core::Calibration& calibration);` — value plus `scaleBarSuffix(source)`; for None the ruler reads px; an absent angle gives `""` (the label is not drawn). Degrees to one decimal with `\xC2\xB0`.
  - `[[nodiscard]] bool measurementLabelWarns(core::CalibrationSource source);` — true for None.

- [ ] **Step 1: Write the failing tests:**
  - placement: horizontal ruler (0,100)-(100,100), label 40×14 → rect centre (50, 100 − 10 − 7); the same ruler drawn (100,100)-(0,100) → the same rect; vertical (100,0)-(100,100) → centre (100 + 10 + 20, 50); the rect never intersects the line segment for a diagonal ruler; a right angle with rays along +x and +y from (100,100) → centre strictly up-left of the vertex (x < 100, y < 100); a 180° angle → centre above the vertex.
  - text: ruler 100 px at Metadata 0.5 µm/px, µm units → `"50.0 \xC2\xB5m"` style as `core::formatLength` gives it (assert it equals `formatLength(50e-6, Physical)`); Manual appends `" (manual)"`; None → `formatLength(100, Pixels)` and `measurementLabelWarns(None)`; angle 47.25 → `"47.3\xC2\xB0"` (+ `" (manual)"` when Manual); absent angle → `""`.
  - tally: a ruler and an angle classified "Tumor" leave the Tumor count at 0 and are not in Unclassified.
  - CSV: header contains `length_um,angle_deg,area_note`; an angle row has `angle_deg` 90 for a right angle and empty length cells; a degenerate angle row has `angle_deg` empty; a ruler row has `length_px` 5 for (0,0)-(3,4) and `angle_deg` empty.
- [ ] **Step 2: Run** the ui-tests build — Expected: compile error, `MeasurementLabel.h` not found.
- [ ] **Step 3: Implement** all of the above; `MeasurementLabel.cpp` in `src/ui/CMakeLists.txt`, the test in `tests/CMakeLists.txt`.
- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release --output-on-failure` — Expected: 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Place and word ruler and angle labels; keep them out of the counts"`

### Task 4: The tools, the pending angle and the painting

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` (append `Ruler`, `Angle` to `AnnotationTool`), `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, `src/ui/src/ViewportWidget.cpp`, `src/ui/src/MainWindow.cpp`, `src/ui/src/AnnotationPropertiesPanel.cpp`, `CLAUDE.md`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: `void ViewportWidget::setCalibration(const core::Calibration& calibration);` (stores it and calls `update()`); `refreshCalibration()` calls it beside the other surfaces.

- [ ] **Step 1: MainWindow** — `rulerToolAction` ("&Ruler", `L`) and `angleToolAction` ("An&gle", `G`), twinned with `textToolAction` at every site (shortcut context, tool group, initial disable, Tools menu, `viewportWidget->addAction`, checked-state sync, slide-close disable, `applyAnnotationAvailability`); triggered → `selectDrawingTool`.
- [ ] **Step 2: Ruler tool** — a press-drag-release committed in `commitDrawDrag` as `RulerGeometry{drawAnchorSlide, drawCurrentSlide}` behind `annotationsEditable()` and the existing minimum-drag rule (a click creates nothing); `pushCreateCommand` ("Draw ruler"); select it. Preview: the dashed line plus its live label (Task 3's placement and text).
- [ ] **Step 3: Angle tool** — first press-drag-release (refused below the drag minimum) stores a pending `{vertex = press, first = release}`; mouse move updates the second ray's end and repaints the dashed preview with the live angle label; the next left click commits `AngleGeometry{first, vertex, click}` (gate, "Draw angle", select). Escape, `setActiveTool`, `resetAnnotationState` and `closeSlide` discard the pending angle; Undo while pending discards it (and records nothing) — wire it through the same path that removes a trace's last vertex (`undoLastPendingVertex`). Focus loss keeps it.
- [ ] **Step 4: Painting** — for every ruler and angle: the shape's line (already from `outline()`), the ruler's `EndCaps` decoration, and the label from Task 3 drawn as a filled `QPainterPath` on the plate (reuse the text annotation's plate drawing and cached font/metrics), plus `SP_MessageBoxWarning` beside it when `measurementLabelWarns`. The paint cull for these includes the label rect. Labels use `ViewportWidget`'s stored calibration and the measurement display-unit preference the properties panel uses.
- [ ] **Step 5: Properties panel** — an angle shows an **Angle** row (`angleLabelText` value without suffix, the badge row as for every measurement) in place of Length; a ruler shows Length as any open path does.
- [ ] **Step 6: Build, ctest 4/4, install** (first copy the currently installed `build/install/release` to `D:	mppre-ruler-viewer` for AL11; kill any viewer first; compare installed and built exe timestamps), smoke on CMU-1-Small-Region: draw a ruler and an angle, read their labels, undo both, restore the annotation file from a backup taken first.
- [ ] **Step 7: CLAUDE.md** — a decision bullet for the two shapes (L/G and why not M, label rules and why paths, excluded from the tally, `angle_deg`, ASAP Measurement → ruler, the dispatch-site count the compiler gave), and the dispatch-site paragraph's count updated.
- [ ] **Step 8: Commit** — `git commit -m "Add the Ruler and Angle tools"`

### Task 5: Manual checks — group AL

**Files:** the spec's §9 (results, run record); `CLAUDE.md` (checks run, result).

- [ ] **Step 1: Run AL1–AL11** on CMU-1-Small-Region (back up its annotation file first and restore after; leave no calibration override stored). AL6's 1 × 2 µm/px slide is CMU with a manual non-square override. AL11 needs a pre-change build: copy the currently installed `build/install/release` to `D:\tmp\pre-ruler-viewer` **before Task 4's install**, and ask the owner before deleting it afterwards.
- [ ] **Step 2: Record and commit** — `git commit -m "Record the group AL run"`
