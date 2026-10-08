# Annotation foundation — design

**Date:** 2026-10-08
**Status:** Proposed
**Base:** `main` @ `e005608`

## Goal

Put a drawable, selectable, movable rectangle annotation on a real slide, built on
the domain model the rest of Stage 3 will extend.

The purpose of this slice is **to validate the three things most likely to be wrong**
before nine drawing tools and a file format are built on top of them:

1. The level-0-pixel ↔ screen coordinate transform under zoom.
2. Hit-testing tolerance, which must stay a constant target size at every
   magnification.
3. Whether annotation painting can coexist with the viewport's FBO-based tile
   renderer at all.

Nothing is persisted. Nothing is undoable. There is one tool.

## Where this sits in Stage 3

`implementation-stages.md` Stage 3 lists ten deliverables against 34+ requirements in
`01-software-requirements-specification.md` §5.1–§5.6. That is too large for one spec
or one plan. It decomposes into eight sub-projects:

| | Sub-project | Layer | Depends on |
|---|---|---|---|
| **A** | Geometry, `Annotation`, layers — the domain model | `core` | — |
| **B** | Overlay rendering in the viewport | `ui` | A |
| **C** | Drawing tools and interaction | `ui` | A, B |
| **D** | Undo/redo command stack | `core`/`app` | A |
| **E** | `AnnotationModel`, JSON persistence, autosave, locking | `app`/`infra` | A |
| **F** | List, properties and layer panels | `ui` | A, E |
| **G** | Measurement and calibration | `core`+`ui` | A |
| **H** | Import/export — QuPath GeoJSON, ASAP XML, CSV | `infra` | A, E |

**This spec is A, plus the thinnest viable slice of B, C and E** — one geometry type,
one tool, in-memory storage. Each remaining sub-project gets its own spec.

## What already exists

- **`Viewport::slideToScreen` / `screenToSlide`** (`Viewport.h`), covered by
  `tests/core/ViewportTest.cpp`. The transform itself needs no new work.
- **`computeSlideId`** already satisfies FR-ANN-29. It was built for the per-slide
  colour profile store with the annotation store as its intended second consumer, and
  architecture §9.1 documents what shipped. Sub-project E inherits it.
- **`pickScaleBarLength`** (`ScaleBar.h`) already derives physical lengths from
  microns-per-pixel, so G starts from working calibration plumbing.
- **`slideio-viewer-app`** exists as a target linking `Qt6::Core`, currently holding
  only `SlideViewerService.h`.

No annotation code exists anywhere.

## Two prerequisites this slice must deliver

### Left-drag currently pans unconditionally

`ViewportWidget.cpp:3176-3186` begins a pan on any left press, and **there is no
middle-button pan at all**. Annotation tools need left-drag.

`02-user-interface-design.md` §3.2 already resolves this:

> | Left mouse drag (Pan tool active or Space held) | Pan the viewport |
> | Middle mouse drag (always) | Pan the viewport |
>
> Pan is always available via middle mouse button, regardless of the active tool. This
> allows panning while in annotation mode without switching tools.

So tool modes and middle-button pan are prerequisites, not extras. **Pan remains the
default tool**, so existing left-drag behaviour is unchanged until the user selects a
drawing tool — this is not a regression for current users.

### `PointF`, `RectF` and `Color` do not exist

`Types.h:331` defines `Rect<T>` only. Architecture §10.2/§10.3 assume all four types.
This slice adds them.

## 1. Coordinates

**Geometry is stored in slide coordinates. Presentation is computed in screen
coordinates.** Every other rule here follows from that one.

FR-ANN-28 fixes geometry at level-0 pixels. The transform exists. What is new is the
conversions around it:

- **Hit tolerance is a screen-space quantity.** A click target must be the same
  physical size at 1× and at 40×, so the caller converts before calling into `core`:

  ```
  toleranceSlide = toleranceScreen / viewport.scale()
  ```

  `hitTest` takes slide units and knows nothing about zoom. This keeps the pure
  function pure and puts the only zoom-dependent arithmetic at one call site.

- **Line width is screen-space.** FR-ANN-11 specifies 1–5 *screen* pixels. Strokes
  must not scale with zoom.

- **Point markers are screen-space sized** (FR-ANN-05). Not in this slice, but the
  rule is the same one.

Painting therefore converts each geometry's points to screen coordinates explicitly
rather than installing a `QTransform` on the painter. A world transform scales the pen
along with the geometry, which is the wrong behaviour; a cosmetic pen would avoid that,
but explicit conversion states the rule in the code instead of hiding it in a pen flag.

## 2. `core` — the domain model

### New types in `Types.h`

```cpp
struct PointF { double x = 0.0; double y = 0.0; };
using RectF = Rect<double>;
struct Color { uint8_t r = 0; uint8_t g = 0; uint8_t b = 0; uint8_t a = 255; };
```

### Geometry

```cpp
struct RectangleGeometry { PointF topLeft; PointF bottomRight; };

using AnnotationGeometry = std::variant<RectangleGeometry>;

[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);
[[nodiscard]] bool  hitTest(const AnnotationGeometry& geom, PointF point,
                            double toleranceSlideUnits);
```

The variant begins with one alternative and widens as tools land. A single-alternative
`std::variant` is legal, and the `std::visit` dispatch in the free functions is
identical whether there is one alternative or six. This establishes the structure
architecture §10.2 specifies without writing nine geometry types against a transform
that has not yet been proven on a real slide.

`boundingBox` normalises: a rectangle dragged right-to-left or bottom-to-top produces a
valid, positive-extent box.

`hitTest` succeeds when the point is **inside the shape or within tolerance of its
outline**. FR-ANN-11 gives annotations a fill opacity, so a filled annotation must be
clickable in its interior.

### `Annotation`

Follows architecture §10.3 as written, with one deviation.

**`AnnotationMetadata::author` exists but stays empty.** FR-ANN-12 requires an author
and FR-USER-01 blocks annotation creation until a user identity is configured — but
user identity is Stage 4 item 8. Populating the field is sub-project E's problem.
Omitting the field would mean changing the struct later; leaving it empty would not.

## 3. `app` — `AnnotationModel`

A `QObject` in `slideio-viewer-app`, per architecture §10.6's placement of the model in
the Application layer. The target exists and links `Qt6::Core`, so no new library.

In-memory, id-keyed CRUD with signals: `annotationAdded`, `annotationRemoved`,
`annotationChanged`, `selectionChanged`.

**Id generation lives here, not in `core`** — `QUuid` is Qt, and `core` is Qt-free. This
is the same split that put `computeSlideId` in `core` and its `QSettings` storage in
`infra`.

Spatial queries are a linear scan with a bounding-box reject before the precise test.
At the hundreds-of-annotations scale this is irrelevant, and the interface does not
change when an R-tree eventually replaces it.

## 4. `ui` — tool mode and event routing

The part with the most failure modes, so the decision is extracted from the event
handlers into a pure function:

```cpp
enum class AnnotationTool { Pan, Rectangle };
enum class DragOwner { None, Pan, Tool, MoveAnnotation };

[[nodiscard]] DragOwner resolveDragOwner(Qt::MouseButton button,
                                         Qt::KeyboardModifiers modifiers,
                                         bool spaceHeld,
                                         AnnotationTool active,
                                         bool pressHitAnnotation);
```

Rules it encodes:

| Input | Owner |
|---|---|
| Middle button | `Pan`, whatever tool is active |
| Space held | `Pan`, whatever tool is active |
| Left, Pan tool, press hit an annotation | `MoveAnnotation` |
| Left, Pan tool, empty space | `Pan` |
| Left, drawing tool | `Tool` |
| Anything else | `None` |

Space auto-repeat is ignored (`QKeyEvent::isAutoRepeat`); without that, holding Space
re-enters pan mode on every repeat.

One rule the function cannot express, enforced in the handlers instead: **a drag that
has begun keeps its owner until release.** Changing tools mid-drag otherwise strands
half-built state.

**Selection.** A left *click* with no drag selects whatever lies under the cursor, so
the Pan tool doubles as select — matching §7.5's "press V or Escape to return to
navigation", where navigation means pan and select together. A click with no drag never
creates a rectangle: a zero-area annotation is a misclick, not a shape.

**Escape** cancels an in-progress draw.

## 5. Painting

`QPainter` at the end of `paintGL`, **after `captureSnapshotTexture`**
(`ViewportWidget.cpp:3172`).

That ordering is not incidental. `paintGL` captures the finished frame into a texture
and reuses it as the blurry backdrop during zoom. Annotations drawn before the capture
would be baked into that texture and would smear and scale with the backdrop while
zooming.

Draw order within the overlay: annotations in creation order, then the selected one
again with its handles, so selection is never occluded by a later annotation.

### The risk, and the fallback

`QPainter` on a `QOpenGLWidget` will disturb GL state at the end of a tuned,
FBO-based render path. **This is verified on day one**, before §2–§4 are built on the
assumption that it works.

If it cannot be made to work cheaply, the fallback is a transparent child `QWidget`
overlay — a pattern this application already uses, with `ZoomIndicatorWidget` and
`LoadingOverlay` both parented to `viewportWidget` (`MainWindow.cpp:855,861`). That
fallback changes **only this section**: the domain model, the routing function and the
coordinate rules are untouched. It costs event-routing complexity, because mouse
handling would then span two widgets.

## 6. Error handling

| Case | Behaviour |
|---|---|
| Slide closed or switched | The model is cleared explicitly. In-memory annotations must never survive into a different slide; nothing is persisted yet, so there is nothing to recover and a clinical misassociation is the only possible outcome of getting this wrong. |
| No slide open | Drawing tools disabled; the active tool resets to `Pan`. |
| Zero-area rectangle | Not created. The press is treated as a selection click. |
| Drag leaves the widget | The drag continues and a release outside still completes it, matching how pan already behaves. |
| Tool changed mid-drag | The in-flight drag keeps its original owner until release. |

## 7. Testing

| Target | Tests |
|---|---|
| `slideio-viewer-core-tests` | `boundingBox` including inverted and degenerate rectangles; `hitTest` inside, on the outline, outside, and exactly at the tolerance boundary; variant dispatch |
| `slideio-viewer-app-tests` **(new)** | `AnnotationModel` CRUD, id uniqueness, each signal fires once with the right payload, clear-on-slide-change |
| `slideio-viewer-ui-tests` | `resolveDragOwner` across the full input matrix; screen↔slide tolerance conversion at several zoom levels |

`tests/CMakeLists.txt` currently builds core, infra and ui test binaries only, so the
app target is new. Signals work with direct connections without a `QApplication`,
keeping it consistent with the other three — none of which creates one, which is why
no test here may construct a widget.

Painting and live interaction are hand-verified against real slides, as dialogs are
in this repo.

## 8. Sequencing

1. **Prove the painting path.** A hard-coded rectangle drawn in `paintGL` after the
   snapshot capture, on a real slide, zoomed and panned. This answers §5's risk before
   anything depends on it.
2. **`core`** — types, geometry, `boundingBox`, `hitTest`, with tests.
3. **`app`** — `AnnotationModel` and the new test target.
4. **`ui`** — `resolveDragOwner` with tests, then tool modes, middle-button pan and
   Space-pan wired into the existing handlers.
5. **The rectangle tool** — draw, select, move, delete.

Step 1 is deliberately throwaway and deliberately first.

## 9. Deliberately absent

Persistence · undo/redo · panels · the other eight geometry types and tools · layers UI
· measurement units and calibration · author identity · copy/paste · context menus ·
multi-select and rubber-band selection · z-order · rotation handling (`Viewport` has
none).

## 10. Flags found in the specifications

These are defects in the requirements, not in this design. They are recorded here
because they affect sub-projects downstream of this one.

- **FR-ANN-27 is assigned to two different requirements** — "jump to next/previous
  annotation" in `01-software-requirements-specification.md` §5.4, and "annotations
  shall be stored in JSON format" in §5.5. For a product carrying traceability
  obligations a duplicated requirement ID should be fixed at the source.
- **FR-USER-01 and FR-ANN-12 depend on Stage 4.** Annotation creation is blocked on a
  configured user identity, and every annotation must record an author, but user
  identity is Stage 4 item 8. Either a slice of Stage 4 moves forward or sub-project E
  defines an interim author policy. This slice leaves the field empty; it is not a
  decision that can be deferred past E.
- **Architecture §10.4 puts `QSize` in `ISlideSource`**, inside the layer required to
  be Qt-free. The shipped `ISlideSource.h` does not do this, so it is a documentation
  defect — but it sits in the same section the annotation interfaces are specified in,
  and will mislead whoever implements E.
