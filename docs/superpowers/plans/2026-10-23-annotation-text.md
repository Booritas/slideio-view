# Text Annotation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Text tool (T): click, type, Enter — words anchored on the slide at a fixed 12-point screen size, behaving like every other annotation, excluded from the counting strip, and carried through every file the viewer reads and writes.

**Architecture:** A new geometry alternative `core::TextGeometry { PointF anchor; }` whose words are the annotation's `properties.label`. Core gains its overloads at every dispatch site the compiler names, plus the hand-written `readGeometry` branch. A pure ui unit computes the text's screen plate and the topmost-hit rule; `ViewportWidget` owns the inline `QLineEdit`, painting, the extra hit step and double-click editing.

**Tech Stack:** C++17, Qt 6 Widgets, Catch2 v3.

**Spec:** `docs/superpowers/specs/2026-10-23-annotation-text-design.md`

## Global Constraints

- `TextGeometry` and `AnnotationType::Text` are **appended** (never inserted) to the variant and the enum.
- The displayed words are `properties.label`. Creation with empty trimmed words creates nothing; editing to empty keeps the old words.
- Font: the application default UI font at **12 points**, screen space. Anchor dot = the marker disc at half its radius; words 6 screen px to its right, vertically centred; plate = white at the annotation's fill opacity, outline in its colour at its line width; empty words paint an italic grey "(empty)".
- Saved form: `"type": "text"`, geometry `{"type": "text", "anchor": {"x", "y"}}`; `schemaVersion` unchanged.
- GeoJSON: export a `Point`, `properties.name` = words, `slideio.type` = `"text"`; import a `Point` with `slideio.type` `"text"` as text.
- `ui::categoryTally` skips text annotations entirely; the measurements CSV writes them as `type` `text` with empty measurements.
- Creation and each edit are **one undo step** each. Escape cancels; every other focus loss — click elsewhere, tool change, window switch, slide close (through `aboutToCloseSlide`, before the flush) — commits.
- Tool shortcut `T`, disabled in `applyAnnotationAvailability` with the other drawing tools.
- Never run a Qt-linked test exe directly — only through `ctest`.
- **Before Task 1**: a copy of the pre-change installed viewer is at `D:\tmp\pre-text-viewer` for check AJ12.

## Review Focus

1. **A text whose words contain characters a JSON or CSV writer must escape** (quotes, comma, backslash, newline pasted into the editor): saved, exported and re-read identical; a newline pasted into the single-line editor is not an error. Pinned in Task 1 (serializer and GeoJSON round trip with such words) and Task 2 (measurements CSV).
2. **Zoomed far out with many texts on one area**: the topmost-hit rule still picks the later annotation, and a text with empty words remains hittable. Pinned in Task 2.
3. **An older annotation file without any text**: loads exactly as before (no behaviour change for the five existing shapes). Pinned in Task 1 by the existing serializer suite staying green unchanged.
4. **Undo of a text creation while its editor is open on another text**: the open editor commits first, then undo applies — never an editor left pointing at a removed annotation. Pinned in Task 3's manual checks (AJ6, AJ7) since no test reaches the widget.
5. **Counting with text present**: every count row and the Unclassified row exclude text, and the column sum equals the non-text annotation count. Pinned in Task 2.

---

### Task 1: The shape in core, its file format and GeoJSON

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`, `src/core/src/AnnotationGeometry.cpp` (the struct, the enum value, the six functions' overloads)
- Modify: `src/core/src/AnnotationSerialization.cpp` (`geometryTypeName`, `shapeToJson`, `isRepresentableShape`, **and `readGeometry`'s `text` branch by hand**)
- Modify: `src/core/src/AnnotationDecoration.cpp` (anchor dot), `src/core/src/BoxResize.cpp` (`std::nullopt`), `src/core/src/AnnotationGeoJson.cpp` (export `Point`; import via `slideio.type` `text`)
- Modify: any other site the compiler names (let it enumerate; do not trust a count)
- Test: `tests/core/AnnotationGeometryTest.cpp`, `tests/core/AnnotationSerializationTest.cpp`, `tests/core/AnnotationDecorationTest.cpp`, `tests/core/BoxResizeTest.cpp`, `tests/core/AnnotationGeoJsonImportTest.cpp`

**Interfaces — Produces:** `struct TextGeometry { PointF anchor; };`, `AnnotationType::Text`, and `core::annotationTypeName` returning `"text"` for it.

- [ ] **Step 1: Write the failing tests:**
  - `typeOf(TextGeometry{{5,6}}) == AnnotationType::Text`; `outline` is one point (5,6), open; `boundingBox` is (5,6,0,0); `translated(+2,+3)` anchors at (7,9); `hasWellDefinedArea` false; `measure` gives no area and length 0; `hitTest` true at (5.5,6) with tolerance 1, false at (8,6).
  - Serializer round trip of a text whose label is `"Νέκρωση, \"re-cut\" \\ ok"`: identical after save and load, type `text`.
  - An annotation file with `"type": "texty"` geometry is refused naming it and the input untouched (the existing unknown-type pattern).
  - `withBoxReplaced(TextGeometry…)` is `std::nullopt`; `screenDecoration(TextGeometry…, discRadius)` is a `Disc` of radius `discRadius / 2`.
  - GeoJSON: export of a text labelled with the same tricky words is a `Point` with `name` and `slideio.type` `text`; importing that file gives a `TextGeometry` with the same anchor and label; a plain `Point` still imports as `PointGeometry`.
- [ ] **Step 2: Run** — build. Expected: compile errors for `TextGeometry`.
- [ ] **Step 3: Implement**, adding overloads wherever the build fails; then `readGeometry`'s branch.
- [ ] **Step 4: Run** all four suites. Expected: pass, every pre-existing test unchanged.
- [ ] **Step 5: Commit** — `git commit -m "Add the text shape to core, its file format and GeoJSON"`

### Task 2: The pure ui rules

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/TextAnnotationLayout.h`, `src/ui/src/TextAnnotationLayout.cpp` (add to `src/ui/CMakeLists.txt`)
- Modify: `src/ui/src/AnnotationListPolicy.cpp` (`categoryTally` skips text), `src/ui/src/AnnotationShapeName.cpp` ("Text", and its `AnnotationType` switch)
- Test: `tests/ui/TextAnnotationLayoutTest.cpp` (new), `tests/ui/AnnotationListPolicyTest.cpp`, `tests/ui/AnnotationShapeNameTest.cpp`, `tests/ui/AnnotationMeasurementExportTest.cpp`

**Interfaces — Produces** (namespace `slideio::viewer::ui`):

```cpp
inline constexpr double kTextPointSize = 12.0;
inline constexpr double kTextGapScreenPixels = 6.0;
inline constexpr double kTextPlatePaddingScreenPixels = 3.0;
/// The plate around a text, in screen pixels: left edge kTextGapScreenPixels right of the
/// anchor, vertically centred on it, padded by kTextPlatePaddingScreenPixels around a text
/// block of the measured size. Independent of zoom by construction.
[[nodiscard]] QRectF textPlateRect(QPointF anchorScreen, double textWidth, double textHeight);
/// The annotation a press selects when a geometric hit and a text-plate hit both exist:
/// whichever is later in `annotations` (paint order). Either id may be empty.
[[nodiscard]] std::string topmostOf(const std::string& geometricHit, const std::string& textHit,
                                    const std::vector<core::Annotation>& annotations);
```

- [ ] **Step 1: Write the failing tests:**
  - `textPlateRect({100,50}, 80, 14)` == `QRectF(106, 50 - 7 - 3, 80 + 6, 14 + 6)`; doubling the width widens it only to the right; width 0 still gives a non-empty plate (the padding).
  - `topmostOf`: geometric id earlier, text id later → text; the reverse → geometric; one empty → the other; both empty → empty.
  - `categoryTally` with markers in Tumor, a text classified Tumor and an unclassified text: Tumor counts only markers; Unclassified excludes the text; the counts sum to the non-text count.
  - Shape name of a text is "Text".
  - Measurements CSV: a text labelled `a,"b"` at (3,4) writes `type` `text`, centre 3/4, the label quoted correctly, every measurement column empty.
- [ ] **Step 2: Run** — ctest `-R ui`. Expected: compile error, header not found.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** all suites. Expected: pass.
- [ ] **Step 5: Commit** — `git commit -m "Lay out and count text annotations"`

### Task 3: The tool, the editor and the painting

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` (`AnnotationTool::Text`, appended), `src/ui/src/ViewportWidget.cpp`, `src/ui/src/MainWindow.cpp`
- Modify: `CLAUDE.md` (a text-annotation decision bullet; "text shape" removed from "Not built yet"; the counts export's "sums to the scene's annotation count" becomes "non-text annotation count"; the dispatch-site count updated to what the compiler found)

**Interfaces — Consumes:** Tasks 1–2.

- [ ] **Step 1: MainWindow** — `textToolAction` ("&Text", shortcut `T`, `Qt::WidgetWithChildrenShortcut` like the others), in the tool group and Tools menu after Arrow, enabled/disabled in `applyAnnotationAvailability` beside the six tools, checked from the viewport's tool-changed signal.
- [ ] **Step 2: Painting** — in the annotation paint pass, a text paints the half-size anchor disc, then the plate (`textPlateRect` from `QFontMetricsF` of the 12-pt font) and the words, per Global Constraints; selected → the plate outline as the selection stroke.
- [ ] **Step 3: Hit step** — wherever the viewport calls `annotations->hitTest(...)` for selection, move and double-click, also test text plates topmost-first and resolve with `topmostOf`.
- [ ] **Step 4: Inline editor** — a `QLineEdit` child of the viewport, styled as the plate. Under the Text tool a release (at the press point, as markers) opens it empty at the anchor; Enter or any focus loss commits, Escape cancels. Commit of a new text: refuse if `!annotationsEditable()` (`annotationEditBlocked`); trimmed words empty → nothing; else `add(TextGeometry)` then put the words on it before `pushCreateCommand(id)` records the state, so the create step carries them; select it. Double-click a text under the Pan tool opens the editor pre-filled over it; a changed, non-empty commit goes through `app::setPropertiesWithUndo`; unchanged or empty records nothing. The editor commits on tool change, on `aboutToCloseSlide` (before the flush), and before any undo or redo; `resetAnnotationState` closes it.
- [ ] **Step 5: Build, run all suites, install;** confirm the installed exe's timestamp and that it contains `"&Text"`.
- [ ] **Step 6: Smoke**: draw one text on CMU-1-Small-Region, close and reopen: it is there with its words.
- [ ] **Step 7: Commit** — `git commit -m "Add the Text tool"`

### Task 4: Manual checks — group AJ

**Files:** the spec's §6 (results, run record); `CLAUDE.md` (checks run, result).

- [ ] **Step 1: Run AJ1–AJ12** on CMU-1-Small-Region (back up its annotation file first, restore after). AJ12 uses `D:\tmp\pre-text-viewer\bin\slideio-viewer.exe` on a copy of an annotation file holding a text; compare the file's hash before and after.
- [ ] **Step 2: Record and commit** — `git commit -m "Record the group AJ run"`
