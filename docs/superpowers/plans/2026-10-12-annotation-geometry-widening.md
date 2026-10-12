# Annotation Geometry Widening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add polygon and freehand-line annotations to the viewer — a new `PolygonGeometry` alternative in `AnnotationGeometry`, real per-type dispatch everywhere a shape is painted, moved or serialised, and a multi-click drawing interaction that lives across press/release pairs.

**Architecture:** The variant widens exactly once, in Task 6, and only after Tasks 1–5 have replaced every faked per-type behaviour with real `std::visit` dispatch. That ordering is the whole shape of this plan: five `static_assert(std::variant_size_v<AnnotationGeometry> == 1)` sites exist to make a premature widening fail to compile, so each is retired by the task that makes it untrue, and the build stays green at every commit. Geometry logic goes in `core` (where `core-tests` can reach it), interaction decisions go in `ui`'s pure `AnnotationInteraction` helpers, and `ViewportWidget` keeps only event plumbing.

**Tech Stack:** C++17, `std::variant`/`std::visit`, Qt 6 Widgets (`QPainterPath`), nlohmann_json 3.11.3 (`ordered_json`, PRIVATE to core), Catch2 v3, CMake 3.20+.

**Spec:** `docs/superpowers/specs/2026-10-12-annotation-geometry-widening-design.md`

## Global Constraints

- **C++17.** No `std::span`, no designated initialisers, no `<numbers>`.
- **`core` is Qt-free.** `slideio-viewer-core` links no Qt target. No `QPainterPath`, `QPointF`, `QString` or signal/slot in `src/core/`.
- **nlohmann_json is PRIVATE to core** and may appear only in `AnnotationSerialization.cpp`, never in a core public header. The serializer uses `nlohmann::ordered_json`, not `nlohmann::json`.
- **`schemaVersion` stays at `1`.** Do not bump it. (Spec §3.2.)
- **Never run a Qt-linked test executable directly.** `app-tests`, `infra-tests` and `ui-tests` resolve their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property; running the `.exe` pops a modal Windows dialog that blocks until a human dismisses it. Only `core-tests` is safe to run directly.
- **Every `ctest` invocation must pass `--timeout 120`.** A test that spins without a bound once burned 27 minutes of CPU in this project.
- **No test may construct a widget.** No suite creates a `QApplication`. Logic that needs testing is extracted into pure functions over plain data.
- **Naming:** `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, 4-space indent, 120-char lines, Allman braces for functions, K&R for control flow, `#pragma once`.
- **Include order:** own header, project headers, Qt headers, std headers.
- **Commit after every task.** All AI-generated code must be human-reviewed before it reaches a protected branch; `main` is not protected in this repository, but work for this plan lands on a branch.

**Build and test commands** (used verbatim throughout):

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure --timeout 120
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

## Review Focus

The five input classes the spec implies that no obvious happy-path test would exercise. Each has a test assigned to the task that owns the code.

1. **A ray cast through a vertex, where the test point's `y` exactly equals a vertex's `y`.** The classic point-in-polygon double-count bug: a naive implementation counts the crossing twice and reports "outside" for a point that is plainly inside. A pathologist tracing a horizontal tissue edge produces vertex rows at identical `y` constantly. → Task 6.
2. **A point inside the bounding box but outside a concave polygon.** The single test that distinguishes real point-in-polygon from the bounding-box approximation this whole increment removes. → Task 6.
3. **A degenerate polygon: all vertices identical, or three collinear vertices.** No interior, zero-area bounding box, a division waiting to happen in the edge-distance maths. Must not crash and must still be hit-testable by edge proximity. → Task 6.
4. **A hand-edited file with `"vertices": []`, no `vertices` key, or a `vertices` entry that is not an object.** The parser must reject with a named problem and leave the file untouched, not index into an empty vector. → Task 7.
5. **`isRepresentable` must refuse an under-minimum polygon.** If a bug ever produces a 2-vertex closed polygon in memory, the save must be refused rather than writing a file the parser will reject — that is the governing rule ("a file we could not parse is never a file we write to") seen from the writer's side. → Task 7.

---

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `src/core/include/slideio/viewer/core/AnnotationGeometry.h` | Geometry types, `AnnotationType`, `Outline`, the five free functions | 1, 6 |
| `src/core/src/AnnotationGeometry.cpp` | Per-shape overloads + `std::visit` dispatch | 1, 6 |
| `src/core/include/slideio/viewer/core/Annotation.h` | `Annotation` without an `AnnotationType` parameter | 2 |
| `src/core/src/Annotation.cpp` | `type()` delegates to `typeOf()` | 2 |
| `src/core/src/AnnotationSerialization.cpp` | Per-type JSON shape, validity, parse | 5, 7 |
| `src/app/include/slideio/viewer/app/AnnotationModel.h` | `add(AnnotationGeometry)` | 2 |
| `src/app/src/AnnotationModel.cpp` | ditto | 2 |
| `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` | `AnnotationTool`, pure interaction decisions | 4, 8 |
| `src/ui/src/AnnotationInteraction.cpp` | ditto | 4, 8 |
| `src/ui/src/ViewportWidget.cpp` | Painting, move drag, pending-shape plumbing, preview | 3, 4, 9, 10 |
| `src/ui/src/MainWindow.cpp` | Two tool actions, gating, status hint | 11 |
| `tests/core/AnnotationGeometryTest.cpp` | All geometry behaviour | 1, 6 |
| `tests/core/AnnotationTest.cpp` | Derived type | 2 |
| `tests/core/AnnotationSerializationTest.cpp` | Round trip and rejection | 5, 7 |
| `tests/ui/AnnotationInteractionTest.cpp` | Interaction decisions | 4, 8 |
| `tests/app/AnnotationModelTest.cpp`, `tests/app/AnnotationPersistenceServiceTest.cpp`, `tests/infra/JsonAnnotationRepositoryTest.cpp` | Mechanical signature updates | 2 |
| `CLAUDE.md`, `docs/superpowers/specs/2026-10-1*-*followups.md` | Record what changed | 12 |

No new source or test files are created, so `tests/CMakeLists.txt` needs no edit.

---

## Task 1: `typeOf`, `outline` and `translated` over the existing variant

Adds the three new free functions while the variant still holds only `RectangleGeometry`. Nothing else changes yet, so this task is pure addition and the build stays green.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`
- Modify: `src/core/src/AnnotationGeometry.cpp`
- Modify: `src/core/include/slideio/viewer/core/Annotation.h` (move `AnnotationType` out)
- Test: `tests/core/AnnotationGeometryTest.cpp`

**Interfaces:**
- Consumes: `PointF`, `RectF` from `core/Types.h`; the existing `normalized()` helper in `AnnotationGeometry.cpp`.
- Produces: `core::AnnotationType` (now declared in `AnnotationGeometry.h`), `core::Outline{std::vector<PointF> points; bool closed;}`, `core::typeOf(const AnnotationGeometry&) -> AnnotationType`, `core::outline(const AnnotationGeometry&) -> Outline`, `core::translated(const AnnotationGeometry&, double dx, double dy) -> AnnotationGeometry`.

**Why `AnnotationType` moves:** `typeOf()` returns an `AnnotationType` and lives in `AnnotationGeometry.h`, but `AnnotationType` is declared in `Annotation.h`, which *includes* `AnnotationGeometry.h`. Leaving it there is a circular include. Moving the enum down is the correct direction anyway — from Task 2 onward the type is a property of the geometry. Every existing user includes `Annotation.h`, which still includes `AnnotationGeometry.h`, so no call site breaks.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/AnnotationGeometryTest.cpp`:

```cpp
TEST_CASE("typeOf reports a rectangle", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(rect(0.0, 0.0, 10.0, 10.0)) == AnnotationType::Rectangle);
}

TEST_CASE("outline of a rectangle is four corners, closed", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(rect(10.0, 20.0, 110.0, 70.0));
    REQUIRE(shape.closed);
    REQUIRE(shape.points.size() == 4);
    REQUIRE(shape.points[0].x == 10.0);
    REQUIRE(shape.points[0].y == 20.0);
    REQUIRE(shape.points[1].x == 110.0);
    REQUIRE(shape.points[1].y == 20.0);
    REQUIRE(shape.points[2].x == 110.0);
    REQUIRE(shape.points[2].y == 70.0);
    REQUIRE(shape.points[3].x == 10.0);
    REQUIRE(shape.points[3].y == 70.0);
}

// An inverted drag is normalised by boundingBox; the outline must agree with
// it, or a shape would be painted somewhere other than where it hit-tests.
TEST_CASE("outline normalises an inverted rectangle", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(rect(110.0, 70.0, 10.0, 20.0));
    REQUIRE(shape.points[0].x == 10.0);
    REQUIRE(shape.points[0].y == 20.0);
    REQUIRE(shape.points[2].x == 110.0);
    REQUIRE(shape.points[2].y == 70.0);
}

TEST_CASE("translated moves a rectangle without resizing it", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(translated(rect(10.0, 20.0, 110.0, 70.0), 5.0, -3.0));
    REQUIRE(box.x == 15.0);
    REQUIRE(box.y == 17.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("translated by zero is the identity", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(translated(rect(10.0, 20.0, 110.0, 70.0), 0.0, 0.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
}
```

- [ ] **Step 2: Run the tests and verify they fail**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `'typeOf': identifier not found`, `'Outline': undeclared identifier`, `'outline'`, `'translated'`.

- [ ] **Step 3: Move `AnnotationType` out of `Annotation.h`**

Delete this block from `src/core/include/slideio/viewer/core/Annotation.h`:

```cpp
enum class AnnotationType
{
    Rectangle,
};
```

- [ ] **Step 4: Declare the new API in `AnnotationGeometry.h`**

Add `#include <vector>` to the include block, then insert after the `RectangleGeometry` struct and before the `using AnnotationGeometry` alias:

```cpp
/// Named kind of a shape. Derived from the geometry rather than stored
/// alongside it, so the two cannot disagree -- see typeOf() below.
enum class AnnotationType
{
    Rectangle,
};
```

And after the existing `hitTest` declaration:

```cpp
/// The shape as a sequence of points to stroke, in order, plus whether the
/// last point joins back to the first.
///
/// This is the whole of what the painter needs to know about a geometry, and
/// it is plain data: the Qt-side conversion to a QPainterPath is a loop with
/// no knowledge of shape types. Keeping the per-type decision here is also
/// what makes it testable -- no test binary in this repository creates a
/// QApplication, so nothing inside a widget can be covered.
struct Outline
{
    std::vector<PointF> points;
    bool closed = true;
};

/// The kind of shape `geom` holds. The single source of truth for an
/// annotation's type; Annotation::type() delegates here.
[[nodiscard]] AnnotationType typeOf(const AnnotationGeometry& geom);

/// `geom` as points to stroke. A rectangle yields its four corners from the
/// normalised box, so the painted outline matches the box hitTest uses.
[[nodiscard]] Outline outline(const AnnotationGeometry& geom);

/// `geom` moved by a slide-space delta, shape and vertex count preserved.
///
/// Returns a whole geometry rather than a rectangle: the move handler used to
/// rebuild every annotation from its bounding box, which would have turned a
/// polygon into a rectangle the first time a user dragged it.
[[nodiscard]] AnnotationGeometry translated(const AnnotationGeometry& geom, double dx, double dy);
```

- [ ] **Step 5: Implement them in `AnnotationGeometry.cpp`**

Add to the anonymous namespace, after `hitTestShape`:

```cpp
AnnotationType typeOfShape(const RectangleGeometry&)
{
    return AnnotationType::Rectangle;
}

Outline outlineShape(const RectangleGeometry& rect)
{
    const RectF box = normalized(rect);
    return Outline{{PointF{box.x, box.y},
                    PointF{box.x + box.width, box.y},
                    PointF{box.x + box.width, box.y + box.height},
                    PointF{box.x, box.y + box.height}},
                   true};
}

AnnotationGeometry translatedShape(const RectangleGeometry& rect, double dx, double dy)
{
    return RectangleGeometry{PointF{rect.topLeft.x + dx, rect.topLeft.y + dy},
                             PointF{rect.bottomRight.x + dx, rect.bottomRight.y + dy}};
}
```

And after the existing `hitTest` definition:

```cpp
AnnotationType typeOf(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return typeOfShape(shape); }, geom);
}

Outline outline(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return outlineShape(shape); }, geom);
}

AnnotationGeometry translated(const AnnotationGeometry& geom, double dx, double dy)
{
    return std::visit([&](const auto& shape) { return translatedShape(shape, dx, dy); }, geom);
}
```

- [ ] **Step 6: Build and run the core tests**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure --timeout 120
```

Expected: PASS.

- [ ] **Step 7: Run the full suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS. Moving the enum must not have broken any include.

- [ ] **Step 8: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationGeometry.h \
        src/core/include/slideio/viewer/core/Annotation.h \
        src/core/src/AnnotationGeometry.cpp \
        tests/core/AnnotationGeometryTest.cpp
git commit -m "Add typeOf, outline and translated to the geometry variant

Three std::visit free functions beside boundingBox and hitTest, still over
the one-alternative variant. AnnotationType moves from Annotation.h into
AnnotationGeometry.h: typeOf() returns one and Annotation.h already includes
AnnotationGeometry.h, so leaving it where it was would be circular."
```

---

## Task 2: Derive `AnnotationType` from the geometry

Closes foundation follow-up item 3. `Annotation` and `AnnotationModel::add` stop taking a type that can disagree with the shape.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/Annotation.h`
- Modify: `src/core/src/Annotation.cpp`
- Modify: `src/app/include/slideio/viewer/app/AnnotationModel.h`
- Modify: `src/app/src/AnnotationModel.cpp`
- Modify: `src/core/src/AnnotationSerialization.cpp:351` and `:425`
- Modify: `src/ui/src/ViewportWidget.cpp:3565`
- Test: `tests/core/AnnotationTest.cpp`
- Modify (mechanical): `tests/app/AnnotationModelTest.cpp`, `tests/app/AnnotationPersistenceServiceTest.cpp`, `tests/core/AnnotationSerializationTest.cpp`, `tests/infra/JsonAnnotationRepositoryTest.cpp`

**Interfaces:**
- Consumes: `core::typeOf` from Task 1.
- Produces: `core::Annotation(std::string id, AnnotationGeometry geometry)`, `core::Annotation(std::string id, AnnotationGeometry geometry, AnnotationProperties, AnnotationMetadata)`, `app::AnnotationModel::add(core::AnnotationGeometry geometry) -> std::string`. `Annotation::type()` keeps its signature and return type.

- [ ] **Step 1: Write the failing test**

Append to `tests/core/AnnotationTest.cpp`:

```cpp
// The type is not storable independently of the shape, so it cannot drift
// from it. Before this, the two were separate constructor arguments.
TEST_CASE("Annotation derives its type from its geometry", "[core][Annotation]")
{
    const Annotation annotation("id-1",
                                RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});
    REQUIRE(annotation.type() == AnnotationType::Rectangle);
}

TEST_CASE("setGeometry keeps the type consistent with the new shape",
          "[core][Annotation]")
{
    Annotation annotation("id-1", RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});
    annotation.setGeometry(RectangleGeometry{PointF{5.0, 5.0}, PointF{25.0, 25.0}});
    REQUIRE(annotation.type() == AnnotationType::Rectangle);
}
```

- [ ] **Step 2: Build and verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — no two-argument `Annotation` constructor.

- [ ] **Step 3: Change `Annotation.h`**

Replace the two constructor declarations and drop the member:

```cpp
    Annotation(std::string id, AnnotationGeometry geometry);

    /// Restores an annotation exactly as stored, including its timestamps.
    /// Loading must not restamp createdAt/modifiedAt the way the constructor
    /// above does, and creation must be able to supply an author.
    Annotation(std::string id, AnnotationGeometry geometry,
               AnnotationProperties properties, AnnotationMetadata metadata);
```

Delete the line `AnnotationType m_type;` from the private section. Leave `type()` declared exactly as it is.

- [ ] **Step 4: Change `Annotation.cpp`**

```cpp
Annotation::Annotation(std::string id, AnnotationGeometry geometry)
    : m_id(std::move(id))
    , m_geometry(std::move(geometry))
{
    m_metadata.createdAt = std::chrono::system_clock::now();
    m_metadata.modifiedAt = m_metadata.createdAt;
}

Annotation::Annotation(std::string id, AnnotationGeometry geometry,
                       AnnotationProperties properties, AnnotationMetadata metadata)
    : m_id(std::move(id))
    , m_geometry(std::move(geometry))
    , m_properties(std::move(properties))
    , m_metadata(std::move(metadata))
{
}
```

And:

```cpp
AnnotationType Annotation::type() const
{
    // Derived, never stored: a stored copy could disagree with the shape.
    return core::typeOf(m_geometry);
}
```

- [ ] **Step 5: Change `AnnotationModel`**

In `AnnotationModel.h` line 31:

```cpp
    std::string add(core::AnnotationGeometry geometry);
```

In `AnnotationModel.cpp`:

```cpp
std::string AnnotationModel::add(core::AnnotationGeometry geometry)
{
    std::string id = generateId();
    core::AnnotationMetadata metadata;
    metadata.author = m_defaultAuthor;
    const auto now = std::chrono::system_clock::now();
    metadata.createdAt = now;
    metadata.modifiedAt = now;

    m_annotations.emplace_back(id, std::move(geometry), core::AnnotationProperties{}, metadata);
    emit annotationAdded(id);
    return id;
}
```

- [ ] **Step 6: Update the three production call sites**

`src/core/src/AnnotationSerialization.cpp` around line 351 — the comment above it is now accurate rather than aspirational:

```cpp
    // The type is derived from the geometry rather than read: a file must not
    // be able to make them disagree.
    out = Annotation(value.at("id").get<std::string>(),
                     std::move(geometry), std::move(properties), std::move(metadata));
```

Same file, around line 425:

```cpp
        Annotation annotation("", RectangleGeometry{PointF{}, PointF{}});
```

`src/ui/src/ViewportWidget.cpp` around line 3565:

```cpp
            const std::string id = m_impl->annotations->add(geometry);
```

- [ ] **Step 7: Update the test call sites mechanically**

Every occurrence in tests of `AnnotationType::Rectangle` **followed by a comma** is a constructor or `add()` argument; every occurrence without one is an assertion on `type()` and must be kept.

```bash
perl -0pi -e 's/(core::)?AnnotationType::Rectangle,\s*//g' \
  tests/app/AnnotationModelTest.cpp \
  tests/app/AnnotationPersistenceServiceTest.cpp \
  tests/core/AnnotationSerializationTest.cpp \
  tests/infra/JsonAnnotationRepositoryTest.cpp
```

Then confirm what survived is only assertions:

```bash
grep -rn "AnnotationType::Rectangle" tests src
```

Expected: no line ends in `AnnotationType::Rectangle,`. Remaining hits should be comparisons such as `== AnnotationType::Rectangle`, plus `typeName()`'s `static_assert` in `AnnotationSerialization.cpp`.

- [ ] **Step 8: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS. The `perl` edit may have left odd indentation where a wrapped argument closed up; fix any line now over 120 characters or visibly misaligned.

- [ ] **Step 9: Commit**

```bash
git add -A src tests
git commit -m "Derive AnnotationType from the geometry

Closes foundation follow-up item 3. Annotation and AnnotationModel::add no
longer take a type that can disagree with the shape they are given;
Annotation::type() delegates to typeOf(). This also makes the comment at
AnnotationSerialization.cpp:348 true -- it claimed the type was derived on
read while the line below it passed a hardcoded Rectangle."
```

---

## Task 3: Paint from `outline()` instead of the bounding box

Closes foundation follow-up item 2 and retires the first of the five `static_assert`s. Rectangles still look identical; the mechanism changes beneath them.

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` — `paintAnnotations` (around line 2702)

**Interfaces:**
- Consumes: `core::outline`, `core::Outline` from Task 1.
- Produces: a file-local `screenPath` lambda inside `paintAnnotations`. Nothing outside the function.

There is no automated test for this task — `ViewportWidget` cannot be constructed without a `QApplication`. The per-type decision it depends on is already covered by Task 1's `outline()` tests; this step is the Qt-side loop. Verification is the build plus manual check 1 of Task 12.

- [ ] **Step 1: Delete the `static_assert` and add a path builder**

In `paintAnnotations`, remove:

```cpp
    static_assert(std::variant_size_v<core::AnnotationGeometry> == 1,
                  "This assumes Rectangle is the only geometry: it draws every shape from its "
                  "bounding box. Add per-type dispatch before widening the variant.");
```

Keep the `toScreen` lambda and its comment exactly as they are. Replace the `screenRect` lambda with:

```cpp
    // A geometry as a screen-space path. core::outline() makes the per-type
    // decision; this is only the Qt conversion, which is why it has no idea
    // what kind of shape it is drawing.
    const auto screenPath = [&toScreen](const core::AnnotationGeometry& geom) {
        const core::Outline shape = core::outline(geom);
        QPainterPath path;
        // OddEvenFill must match core::hitTest's even-odd ray casting. If these
        // drift apart, a self-intersecting trace is painted solid in regions
        // that test as outside it, and clicking there selects nothing.
        path.setFillRule(Qt::OddEvenFill);
        if (shape.points.empty()) {
            return path;
        }
        path.moveTo(toScreen(shape.points.front()));
        for (std::size_t i = 1; i < shape.points.size(); ++i) {
            path.lineTo(toScreen(shape.points[i]));
        }
        if (shape.closed) {
            path.closeSubpath();
        }
        return path;
    };
```

Add `#include <QPainterPath>` to the Qt include block if it is not already present.

- [ ] **Step 2: Replace the annotation loop**

```cpp
    const QRect widgetRect = rect();

    for (const core::Annotation& annotation : m_impl->annotations->annotations()) {
        // Two transforms to decide, against transforming every vertex of a
        // trace that is not on screen. A dense freehand outline can carry
        // thousands of points and the viewport repaints on every pan frame.
        const core::RectF box = annotation.boundingBox();
        const QRectF bounds =
            QRectF(toScreen(core::PointF{box.x, box.y}),
                   toScreen(core::PointF{box.x + box.width, box.y + box.height}))
                .normalized();
        if (!bounds.intersects(QRectF(widgetRect))) {
            continue;
        }

        const core::AnnotationProperties& props = annotation.properties();
        const QColor stroke(props.color.r, props.color.g, props.color.b, props.color.a);
        QColor fill = stroke;
        fill.setAlphaF(props.fillOpacity);

        const core::Outline shape = core::outline(annotation.geometry());
        // A hand trace is hundreds of short segments at sharp angles, where the
        // default miter join throws long spikes.
        QPen pen(stroke, props.lineWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        // An open path is never filled: Qt would close it implicitly to do so,
        // inventing a segment the user never drew.
        painter.setBrush(shape.closed ? QBrush(fill) : QBrush(Qt::NoBrush));
        painter.drawPath(screenPath(annotation.geometry()));
    }
```

Hoist `annotation.boundingBox()` into a local before building `bounds` rather than calling it four times.

- [ ] **Step 3: Replace the selection highlight**

```cpp
    if (!selectedId.empty()) {
        if (const core::Annotation* selected = m_impl->annotations->find(selectedId)) {
            // The outline follows the shape, not its bounding box: around a
            // traced polygon a box would say "something in this region is
            // selected", and around a thin diagonal line it would be a large
            // box containing a line.
            const QPainterPath path = screenPath(selected->geometry());
            painter.setBrush(Qt::NoBrush);
            // Two-tone, and not white alone: brightfield H&E glass and tissue are
            // near-white, and paintGL clears the slide rect to white, so a 1 px
            // white dash was invisible on the commonest slide type. The user
            // presses Delete on the strength of this outline, so it has to read
            // against both a pale slide and a dark background.
            painter.setPen(QPen(QColor(0x00, 0x00, 0x00), 3.0, Qt::SolidLine));
            painter.drawPath(path);
            painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1.0, Qt::DashLine));
            painter.drawPath(path);
        }
    }
```

Leave the in-progress `if (m_impl->drawing)` preview block below it untouched — Task 10 owns it.

- [ ] **Step 4: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: builds clean, all four suites PASS (none of them reach this code).

- [ ] **Step 5: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Paint annotations from core::outline, not the bounding box

Closes foundation follow-up item 2 and retires the first of the five
static_asserts. Both the annotation loop and the selection highlight drew
screenRect(boundingBox()); both now stroke a QPainterPath built from
core::outline(). Rectangles are pixel-identical.

Carries three things the polygon needs: OddEvenFill matching hitTest's
even-odd ray casting, round joins and caps for traces with sharp corners,
and a per-annotation screen-bounds cull."
```

---

## Task 4: Move an annotation with `core::translated`

Closes foundation follow-up item 1, retires the second `static_assert`, and switches the drag from accumulated increments to a total delta against the press-time original.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` — remove `translatedBox`
- Modify: `src/ui/src/AnnotationInteraction.cpp` — remove `translatedBox`
- Modify: `src/ui/src/ViewportWidget.cpp` — `Impl` member (line 1415), `mousePressEvent` (line 3464), `mouseMoveEvent` (lines 3611–3629)
- Test: `tests/ui/AnnotationInteractionTest.cpp` — remove the `translatedBox` cases
- Test: `tests/core/AnnotationGeometryTest.cpp` — already covers `translated()` from Task 1

**Interfaces:**
- Consumes: `core::translated` from Task 1.
- Produces: `Impl::movePressSlide` replacing `Impl::moveLastSlide`. `translatedBox` no longer exists.

- [ ] **Step 1: Delete `translatedBox`**

Remove the declaration and its doc comment from `AnnotationInteraction.h`, and the definition from `AnnotationInteraction.cpp`. Remove every `translatedBox` `TEST_CASE` from `tests/ui/AnnotationInteractionTest.cpp` — the behaviour now lives in Task 1's `translated()` tests in `core-tests`.

Find them first:

```bash
grep -n "translatedBox" tests/ui/AnnotationInteractionTest.cpp src/ui/src/ViewportWidget.cpp
```

- [ ] **Step 2: Build and verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL — `'translatedBox': identifier not found` at `ViewportWidget.cpp`.

- [ ] **Step 3: Rename the member**

In `ViewportWidget.cpp`'s `Impl`, line 1415:

```cpp
    /// Cursor position in slide space at the moment the move drag began.
    /// Written once, at press: the drag applies a total delta to
    /// moveOriginalGeometry rather than accumulating per-event increments, so
    /// every frame translates a pristine shape instead of the previous frame's
    /// output. It is also the state an undo stack will want.
    core::PointF movePressSlide;
```

In `mousePressEvent`, line 3464:

```cpp
        m_impl->movePressSlide = core::PointF{slideX, slideY};
```

- [ ] **Step 4: Rewrite the move branch of `mouseMoveEvent`**

```cpp
    if (m_impl->dragOwner == DragOwner::MoveAnnotation && m_impl->controller
        && !m_impl->movingId.empty()) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);

        // Total delta from the press, applied to the geometry captured at the
        // press -- not an increment applied to the previous frame's result.
        // Escape restores moveOriginalGeometry, so the drag and the cancel now
        // read the same single source of truth.
        const double dx = slideX - m_impl->movePressSlide.x;
        const double dy = slideY - m_impl->movePressSlide.y;

        if (m_impl->annotations->find(m_impl->movingId) != nullptr) {
            m_impl->annotations->setGeometry(
                m_impl->movingId, core::translated(m_impl->moveOriginalGeometry, dx, dy));
        }
        event->accept();
        update();
    }
```

Note the `static_assert` and the `const core::Annotation* current` lookup both go: the annotation's current geometry is no longer read, only its existence is checked.

- [ ] **Step 5: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ui tests/ui
git commit -m "Move annotations with core::translated from the press-time original

Closes foundation follow-up item 1 and retires the second static_assert.
translatedBox is gone from AnnotationInteraction -- it was the only place
the UI layer manufactured a geometry out of a bounding box, which is
precisely the defect: a polygon dragged through it would have become a
rectangle.

moveLastSlide becomes movePressSlide, written once at press. Each frame
applies a total delta to moveOriginalGeometry rather than the five-hundredth
increment in a chain, and the Escape path already restored that same value."
```

---

## Task 5: Dispatch the serializer per type

Retires the remaining three `static_assert`s. Still Rectangle-only; the shape of the code changes so Task 6 can widen the variant.

**Files:**
- Modify: `src/core/src/AnnotationSerialization.cpp` — `isRepresentable` (line 37), `geometryToJson` (line 80), `typeName` (line 90), `readGeometry` (line 265)

**Interfaces:**
- Consumes: nothing new.
- Produces: `geometryTypeName(const AnnotationGeometry&) -> const char*` replacing `typeName(AnnotationType)`. File-local.

- [ ] **Step 1: Make `isRepresentable` and `geometryToJson` visit**

Replace both functions:

```cpp
bool isRepresentableShape(const RectangleGeometry& rectangle)
{
    return isFinitePoint(rectangle.topLeft) && isFinitePoint(rectangle.bottomRight);
}

bool isRepresentable(const AnnotationGeometry& geometry)
{
    return std::visit([](const auto& shape) { return isRepresentableShape(shape); }, geometry);
}

json shapeToJson(const RectangleGeometry& rectangle)
{
    return json{
        {"type", kRectangleTypeName},
        {"topLeft", pointToJson(rectangle.topLeft)},
        {"bottomRight", pointToJson(rectangle.bottomRight)},
    };
}

json geometryToJson(const AnnotationGeometry& geometry)
{
    return std::visit([](const auto& shape) { return shapeToJson(shape); }, geometry);
}
```

- [ ] **Step 2: Derive the annotation-level type name from the geometry**

Replace `typeName(AnnotationType)` with:

```cpp
/// The annotation-level "type" string. Derived from the geometry, like
/// Annotation::type() itself, so the two fields of a written file cannot
/// disagree. On read the field is ignored and the geometry wins.
const char* geometryTypeName(const AnnotationGeometry& geometry)
{
    return std::visit([](const auto& shape) -> const char* {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, RectangleGeometry>) {
            return kRectangleTypeName;
        }
    }, geometry);
}
```

Add `#include <type_traits>`. At the call site around line 121:

```cpp
        {"type", geometryTypeName(annotation.geometry())},
```

- [ ] **Step 3: Restructure `readGeometry` as a type switch**

```cpp
bool readRectangle(const json& value, AnnotationGeometry& out, std::string& problem)
{
    RectangleGeometry rectangle;
    if (!value.contains("topLeft") || !readPoint(value.at("topLeft"), rectangle.topLeft)) {
        problem = "geometry.topLeft is not a finite point";
        return false;
    }
    if (!value.contains("bottomRight") || !readPoint(value.at("bottomRight"), rectangle.bottomRight)) {
        problem = "geometry.bottomRight is not a finite point";
        return false;
    }
    out = rectangle;
    return true;
}

bool readGeometry(const json& value, AnnotationGeometry& out, std::string& problem)
{
    if (!value.is_object() || !value.contains("type") || !value.at("type").is_string()) {
        problem = "geometry has no type";
        return false;
    }

    const std::string type = value.at("type").get<std::string>();
    if (type == kRectangleTypeName) {
        return readRectangle(value, out, problem);
    }

    // Naming the version is the whole value of this message: schemaVersion
    // stays at 1 across geometry additions on purpose (design spec 3.2), so an
    // older build meeting a newer shape fails here rather than at the version
    // check, and a user needs to be told which it was.
    problem = "unknown geometry type '" + type
            + "' -- this file may have been written by a newer version of the viewer";
    return false;
}
```

- [ ] **Step 4: Update the message assertion in the existing tests**

```bash
grep -n "unknown geometry type" tests/core/AnnotationSerializationTest.cpp
```

Any test asserting the old exact message needs its expectation widened or updated to the new text.

- [ ] **Step 5: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS. Confirm no `static_assert` on the variant survives:

```bash
grep -rn "variant_size_v" src/
```

Expected: no output.

- [ ] **Step 6: Commit**

```bash
git add src/core/src/AnnotationSerialization.cpp tests/core/AnnotationSerializationTest.cpp
git commit -m "Dispatch the annotation serializer per geometry type

Retires the last three static_asserts. isRepresentable and geometryToJson
visit the variant; typeName(AnnotationType) becomes
geometryTypeName(AnnotationGeometry), so the annotation-level type string is
derived like Annotation::type() and the two cannot disagree in a written
file; readGeometry becomes a type switch with a per-type reader.

The unknown-type message now names the likely cause. schemaVersion stays at
1 by design, so an older build meeting a newer shape fails here, and that
message is the only thing telling the user why."
```

---

## Task 6: Add `PolygonGeometry` to the variant

The widening itself. Everything above exists to make this compile.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`
- Modify: `src/core/src/AnnotationGeometry.cpp`
- Test: `tests/core/AnnotationGeometryTest.cpp`

**Interfaces:**
- Consumes: Task 1's five free functions.
- Produces: `core::PolygonGeometry{std::vector<PointF> vertices; bool closed;}`, `AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry>`, `AnnotationType::Polygon`, `AnnotationType::Polyline`, and the constants `core::kMinPolygonVertices = 3`, `core::kMinPolylineVertices = 2`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/AnnotationGeometryTest.cpp`, after the existing anonymous-namespace helper:

```cpp
namespace
{

AnnotationGeometry polygon(std::vector<PointF> vertices)
{
    return PolygonGeometry{std::move(vertices), true};
}

AnnotationGeometry polyline(std::vector<PointF> vertices)
{
    return PolygonGeometry{std::move(vertices), false};
}

// A square with a deep notch cut into its right edge. The notch lies inside
// the bounding box and outside the shape.
AnnotationGeometry notchedSquare()
{
    return polygon({PointF{0.0, 0.0}, PointF{100.0, 0.0}, PointF{50.0, 50.0},
                    PointF{100.0, 100.0}, PointF{0.0, 100.0}});
}

} // namespace

TEST_CASE("typeOf distinguishes a polygon from a polyline", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(polygon({PointF{0, 0}, PointF{10, 0}, PointF{0, 10}}))
            == AnnotationType::Polygon);
    REQUIRE(typeOf(polyline({PointF{0, 0}, PointF{10, 10}})) == AnnotationType::Polyline);
}

TEST_CASE("boundingBox spans every vertex", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(polygon({PointF{30.0, 10.0}, PointF{-5.0, 70.0},
                                           PointF{90.0, 40.0}}));
    REQUIRE(box.x == -5.0);
    REQUIRE(box.y == 10.0);
    REQUIRE(box.width == 95.0);
    REQUIRE(box.height == 60.0);
}

// Review Focus 2. This is what distinguishes real point-in-polygon from the
// bounding-box approximation the whole increment exists to remove: (75, 50)
// is squarely inside the bounding box and squarely outside the shape.
TEST_CASE("hitTest misses a point in the notch of a concave polygon",
          "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hitTest(notchedSquare(), PointF{75.0, 50.0}, 0.0));
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 50.0}, 0.0));
}

// Review Focus 1. A ray cast through a vertex is double-counted by a naive
// implementation, which then reports "outside" for a point plainly inside.
// Every vertex of the notched square sits on y = 0, 50 or 100, so these probe
// the three vertex rows directly.
TEST_CASE("hitTest is correct for a ray passing exactly through vertices",
          "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 50.0}, 0.0));
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 0.0}, 0.0));
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 100.0}, 0.0));
    REQUIRE_FALSE(hitTest(notchedSquare(), PointF{120.0, 50.0}, 0.0));
    REQUIRE_FALSE(hitTest(notchedSquare(), PointF{120.0, 0.0}, 0.0));
}

// Self-intersection is valid data -- pathologists produce it by accident when
// tracing quickly. Even-odd means the centre of a bowtie is outside.
TEST_CASE("hitTest applies the even-odd rule to a self-intersecting polygon",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry bowtie = polygon({PointF{0.0, 0.0}, PointF{100.0, 100.0},
                                               PointF{100.0, 0.0}, PointF{0.0, 100.0}});
    REQUIRE(hitTest(bowtie, PointF{10.0, 50.0}, 0.0));
    REQUIRE_FALSE(hitTest(bowtie, PointF{50.0, 20.0}, 0.0));
}

TEST_CASE("hitTest finds a point near an edge within tolerance",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry line = polyline({PointF{0.0, 0.0}, PointF{100.0, 0.0}});
    REQUIRE(hitTest(line, PointF{50.0, 3.0}, 5.0));
    REQUIRE_FALSE(hitTest(line, PointF{50.0, 9.0}, 5.0));
}

// A polyline has no interior, however tempting its shape looks.
TEST_CASE("hitTest never reports an interior hit for an open path",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry open = polyline({PointF{0.0, 0.0}, PointF{100.0, 0.0},
                                              PointF{100.0, 100.0}, PointF{0.0, 100.0}});
    REQUIRE_FALSE(hitTest(open, PointF{50.0, 50.0}, 1.0));
    REQUIRE(hitTest(open, PointF{50.0, 0.0}, 1.0));
}

// An open path must not be hit along the segment that would close it.
TEST_CASE("hitTest ignores the closing segment of an open path",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry open = polyline({PointF{0.0, 0.0}, PointF{100.0, 0.0},
                                              PointF{100.0, 100.0}});
    REQUIRE_FALSE(hitTest(open, PointF{50.0, 50.0}, 1.0));
}

// Review Focus 3. Degenerate shapes must not crash and must stay selectable
// by edge proximity -- a zero-length segment divides by zero if the distance
// maths is careless.
TEST_CASE("hitTest handles a polygon whose vertices are all identical",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry dot = polygon({PointF{10.0, 10.0}, PointF{10.0, 10.0},
                                            PointF{10.0, 10.0}});
    REQUIRE(hitTest(dot, PointF{12.0, 10.0}, 5.0));
    REQUIRE_FALSE(hitTest(dot, PointF{40.0, 10.0}, 5.0));
    const RectF box = boundingBox(dot);
    REQUIRE(box.width == 0.0);
    REQUIRE(box.height == 0.0);
}

TEST_CASE("hitTest handles three collinear vertices", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry flat = polygon({PointF{0.0, 0.0}, PointF{50.0, 0.0},
                                             PointF{100.0, 0.0}});
    REQUIRE(hitTest(flat, PointF{50.0, 2.0}, 5.0));
    REQUIRE_FALSE(hitTest(flat, PointF{50.0, 20.0}, 5.0));
}

TEST_CASE("outline of a polygon is its vertices", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(polygon({PointF{1.0, 2.0}, PointF{3.0, 4.0},
                                           PointF{5.0, 6.0}}));
    REQUIRE(shape.closed);
    REQUIRE(shape.points.size() == 3);
    REQUIRE(shape.points[1].x == 3.0);
    REQUIRE(shape.points[1].y == 4.0);
}

TEST_CASE("outline of a polyline is open", "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(outline(polyline({PointF{1.0, 2.0}, PointF{3.0, 4.0}})).closed);
}

// The regression foundation item 1 was filed to prevent: a dragged polygon
// must stay a polygon with the same vertex count, not become its bounding box.
TEST_CASE("translated moves every vertex and preserves the shape",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved =
        translated(polygon({PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{0.0, 10.0}}), 5.0, -2.0);
    REQUIRE(typeOf(moved) == AnnotationType::Polygon);
    const PolygonGeometry& shape = std::get<PolygonGeometry>(moved);
    REQUIRE(shape.vertices.size() == 3);
    REQUIRE(shape.closed);
    REQUIRE(shape.vertices[0].x == 5.0);
    REQUIRE(shape.vertices[0].y == -2.0);
    REQUIRE(shape.vertices[2].x == 5.0);
    REQUIRE(shape.vertices[2].y == 8.0);
}

TEST_CASE("translated preserves an open path's openness", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved =
        translated(polyline({PointF{0.0, 0.0}, PointF{10.0, 0.0}}), 1.0, 1.0);
    REQUIRE_FALSE(std::get<PolygonGeometry>(moved).closed);
    REQUIRE(typeOf(moved) == AnnotationType::Polyline);
}
```

Add `#include <variant>` and `#include <vector>` to the test file's include block.

- [ ] **Step 2: Build and verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL — `'PolygonGeometry': undeclared identifier`.

- [ ] **Step 3: Declare the type in `AnnotationGeometry.h`**

Replace the enum and the alias:

```cpp
enum class AnnotationType
{
    Rectangle,
    Polygon,
    Polyline,
};

/// A sequence of vertices, closed into a polygon (FR-ANN-03) or left open as a
/// freehand line (FR-ANN-04).
///
/// One type for both, because they differ only in whether the path closes:
/// the vertices, the bounding box, the translation and the edge hit-test are
/// identical, and only the closing segment, the fill and the interior test
/// differ. Two variant alternatives would duplicate all of that to express
/// one boolean.
///
/// Self-intersecting outlines are valid. Pathologists produce them by accident
/// when tracing quickly, and silently rejecting or repairing someone's outline
/// is worse than rendering what they drew; the even-odd rule handles them.
struct PolygonGeometry
{
    std::vector<PointF> vertices;
    bool closed = true;
};

/// Fewer vertices than this cannot describe the shape: a closed polygon needs
/// three, an open path two. Enforced by the drawing tool at commit and by the
/// serializer on both read and write, so the application cannot produce a file
/// it would refuse to open.
constexpr std::size_t kMinPolygonVertices = 3;
constexpr std::size_t kMinPolylineVertices = 2;

using AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry>;
```

Add `#include <cstddef>`. Delete the obsolete "The variant starts with one alternative" comment above the alias.

- [ ] **Step 4: Implement the polygon overloads**

In `AnnotationGeometry.cpp`, add `#include <cmath>` and `#include <cstddef>`, then add to the anonymous namespace:

```cpp
RectF normalized(const PolygonGeometry& poly)
{
    if (poly.vertices.empty()) {
        return RectF{0.0, 0.0, 0.0, 0.0};
    }
    double x0 = poly.vertices.front().x;
    double y0 = poly.vertices.front().y;
    double x1 = x0;
    double y1 = y0;
    for (const PointF& vertex : poly.vertices) {
        x0 = std::min(x0, vertex.x);
        y0 = std::min(y0, vertex.y);
        x1 = std::max(x1, vertex.x);
        y1 = std::max(y1, vertex.y);
    }
    return RectF{x0, y0, x1 - x0, y1 - y0};
}

/// Even-odd ray casting (Franklin's algorithm). The half-open vertical span
/// test -- strictly greater on one end, not on the other -- is what makes a
/// ray passing exactly through a vertex cross once rather than twice or zero
/// times. A horizontal tissue edge produces whole rows of vertices at the same
/// y, so this is the common case, not the corner case. `straddles` also
/// guarantees the two y values differ, so the division cannot be by zero.
bool pointInPolygon(const std::vector<PointF>& vertices, PointF point)
{
    bool inside = false;
    const std::size_t count = vertices.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const bool straddles = (vertices[i].y > point.y) != (vertices[j].y > point.y);
        if (!straddles) {
            continue;
        }
        const double t = (point.y - vertices[i].y) / (vertices[j].y - vertices[i].y);
        if (point.x < vertices[i].x + t * (vertices[j].x - vertices[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

/// Distance from `point` to the segment a-b, with a zero-length segment
/// treated as the point a rather than divided by.
double distanceToSegment(PointF a, PointF b, PointF point)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0) {
        return std::hypot(point.x - a.x, point.y - a.y);
    }
    double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

bool hitTestShape(const PolygonGeometry& poly, PointF point, double tolerance)
{
    const std::size_t count = poly.vertices.size();
    if (count == 0) {
        return false;
    }
    if (count == 1) {
        return std::hypot(point.x - poly.vertices[0].x, point.y - poly.vertices[0].y)
               <= tolerance;
    }
    // An open path has no interior, however enclosing its shape looks.
    if (poly.closed && count >= kMinPolygonVertices && pointInPolygon(poly.vertices, point)) {
        return true;
    }
    // A closed shape tests its closing segment too; an open one stops one short.
    const std::size_t segments = poly.closed ? count : count - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        if (distanceToSegment(poly.vertices[i], poly.vertices[(i + 1) % count], point)
            <= tolerance) {
            return true;
        }
    }
    return false;
}

AnnotationType typeOfShape(const PolygonGeometry& poly)
{
    return poly.closed ? AnnotationType::Polygon : AnnotationType::Polyline;
}

Outline outlineShape(const PolygonGeometry& poly)
{
    return Outline{poly.vertices, poly.closed};
}

AnnotationGeometry translatedShape(const PolygonGeometry& poly, double dx, double dy)
{
    PolygonGeometry moved = poly;
    for (PointF& vertex : moved.vertices) {
        vertex.x += dx;
        vertex.y += dy;
    }
    return moved;
}
```

The existing `boundingBox`, `hitTest`, `typeOf`, `outline` and `translated` entry points need no change — their `std::visit` lambdas already dispatch to whichever overload matches.

- [ ] **Step 5: Build and run the core tests**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure --timeout 120
```

Expected: PASS. If `hitTest` on the notched square's vertex rows fails, the half-open test in `pointInPolygon` has been written with `>=` on one side.

- [ ] **Step 6: Run the full suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS. Serialising a polygon is not yet possible — Task 7 — but nothing yet constructs one outside these tests.

- [ ] **Step 7: Commit**

```bash
git add src/core tests/core/AnnotationGeometryTest.cpp
git commit -m "Add PolygonGeometry to the annotation geometry variant

One type for the polygon (FR-ANN-03) and the freehand line (FR-ANN-04),
distinguished by a closed flag: they differ only in the closing segment, the
fill and the interior test.

hitTest uses even-odd ray casting with a half-open vertical span, so a ray
passing exactly through a vertex crosses once -- a horizontal tissue edge
produces whole rows of vertices at one y, which makes that the common case.
Self-intersecting outlines are valid data and are handled rather than
rejected. Degenerate shapes fall back to edge proximity without dividing by
a zero-length segment."
```

---

## Task 7: Serialise polygons and polylines

**Files:**
- Modify: `src/core/src/AnnotationSerialization.cpp`
- Test: `tests/core/AnnotationSerializationTest.cpp`

**Interfaces:**
- Consumes: `core::PolygonGeometry`, `kMinPolygonVertices`, `kMinPolylineVertices` from Task 6; the Task 5 shape of `isRepresentableShape` / `shapeToJson` / `readGeometry`.
- Produces: the on-disk `"polygon"` and `"polyline"` geometry shapes. No new public API.

- [ ] **Step 1: Write the failing tests**

The file already defines `makeAnnotation()` and `makeDocument()` in its anonymous namespace, and writes bad input as inline raw-string documents. Follow both idioms. The public API is `serializeAnnotationDocument` / `parseAnnotationDocument`, and the failure text is `ParseResult::message`.

Add one helper beside `makeDocument()`:

```cpp
/// A valid one-annotation document whose geometry is `geometryJson` verbatim.
/// Mirrors the inline raw strings the rest of this file uses, which is how a
/// hand-edited file reaches the parser.
std::string documentWithGeometry(const std::string& geometryJson)
{
    return std::string(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
          {"id": "a1", "geometry": )") + geometryJson + R"(}
        ]})";
}
```

Then append:

```cpp
TEST_CASE("a polygon round-trips through the serializer",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("poly-1", PolygonGeometry{{PointF{1.5, 2.0}, PointF{30.0, 4.0},
                                              PointF{10.0, 40.0}}, true}));

    const std::string text = serializeAnnotationDocument(document);
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("\"polygon\"") != std::string::npos);

    const ParseResult result = parseAnnotationDocument(text);
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);

    const Annotation& back = result.document.annotations.front();
    REQUIRE(back.type() == AnnotationType::Polygon);
    const PolygonGeometry& shape = std::get<PolygonGeometry>(back.geometry());
    REQUIRE(shape.closed);
    REQUIRE(shape.vertices.size() == 3);
    REQUIRE(shape.vertices[0].x == 1.5);
    REQUIRE(shape.vertices[2].y == 40.0);
}

TEST_CASE("a polyline round-trips and stays open",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("line-1", PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 10.0}}, false}));

    const ParseResult result =
        parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    const Annotation& back = result.document.annotations.front();
    REQUIRE(back.type() == AnnotationType::Polyline);
    REQUIRE_FALSE(std::get<PolygonGeometry>(back.geometry()).closed);
}

// schemaVersion does not move when a geometry type is added. Bumping it would
// make every shipped build refuse rectangle-only files during a staggered
// rollout; not bumping makes an old build fail on the polygon alone and leave
// the file intact.
TEST_CASE("adding a geometry type does not bump schemaVersion",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("poly-1", PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                              PointF{0.0, 10.0}}, true}));
    REQUIRE(serializeAnnotationDocument(document).find("\"schemaVersion\": 1")
            != std::string::npos);
}

// Review Focus 4. A hand-edited file must be rejected with a named problem,
// not indexed into.
TEST_CASE("an empty vertices array is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "polygon", "vertices": []})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("vertices") != std::string::npos);
}

TEST_CASE("a missing vertices key is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result =
        parseAnnotationDocument(documentWithGeometry(R"({"type": "polygon"})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("vertices") != std::string::npos);
}

TEST_CASE("a vertex that is not an object is rejected",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon", "vertices": [{"x": 0, "y": 0}, 7, {"x": 1, "y": 1}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("vertices") != std::string::npos);
}

TEST_CASE("a vertex missing a coordinate is rejected",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon", "vertices": [{"x": 0, "y": 0}, {"x": 1}, {"x": 2, "y": 2}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
}

// Note: a literal 1e400 in the file is rejected by the JSON lexer as
// MalformedJson and never reaches readPolygon -- the header documents this.
// It is pinned here so the distinction is deliberate rather than a surprise.
TEST_CASE("an out-of-range vertex literal is malformed JSON",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon",
            "vertices": [{"x": 0, "y": 0}, {"x": 1e400, "y": 1}, {"x": 2, "y": 2}]})"));
    REQUIRE(result.error == ParseError::MalformedJson);
}

TEST_CASE("a two-vertex polygon is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon", "vertices": [{"x": 0, "y": 0}, {"x": 1, "y": 1}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
}

TEST_CASE("a one-vertex polyline is rejected", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polyline", "vertices": [{"x": 0, "y": 0}]})"));
    REQUIRE(result.error == ParseError::InvalidField);
}

// Three collinear vertices are a valid polygon. Pinning this stops a later
// tightening of "validity" from silently making existing files unloadable.
TEST_CASE("a collinear polygon is accepted", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(documentWithGeometry(
        R"({"type": "polygon",
            "vertices": [{"x": 0, "y": 0}, {"x": 1, "y": 0}, {"x": 2, "y": 0}]})"));
    REQUIRE(result.error == ParseError::None);
}

// Review Focus 5. The governing rule seen from the writer's side: refusing to
// write what we would refuse to read is what guarantees the application cannot
// produce a file it cannot open.
TEST_CASE("serializing an under-minimum polygon is refused",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("bad-1", PolygonGeometry{{PointF{0.0, 0.0}, PointF{1.0, 1.0}}, true}));
    REQUIRE(serializeAnnotationDocument(document).empty());
}

TEST_CASE("serializing a polygon with a non-finite vertex is refused",
          "[core][AnnotationSerialization]")
{
    const double infinity = std::numeric_limits<double>::infinity();
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.push_back(
        Annotation("bad-2", PolygonGeometry{{PointF{0.0, 0.0}, PointF{infinity, 1.0},
                                             PointF{2.0, 2.0}}, true}));
    REQUIRE(serializeAnnotationDocument(document).empty());
}

// The whole point of holding schemaVersion at 1: an older build must fail on
// the shape, with a message that explains itself, and must not touch the file.
TEST_CASE("an unknown geometry type names the likely cause",
          "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "ellipse", "vertices": []})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("newer version") != std::string::npos);
}
```

`<limits>` is already included. Add `#include <variant>` and `#include <vector>`.

- [ ] **Step 2: Build and verify the tests fail**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure --timeout 120
```

Expected: the round-trip tests FAIL (`std::get<PolygonGeometry>` throws, or `serializeDocument` returns empty) — `shapeToJson` has no `PolygonGeometry` overload, so the build fails first with "no matching overloaded function".

- [ ] **Step 3: Write and validate the polygon**

In `AnnotationSerialization.cpp`, add beside `kRectangleTypeName`:

```cpp
constexpr const char* kPolygonTypeName = "polygon";
constexpr const char* kPolylineTypeName = "polyline";
```

Add the write-side overloads:

```cpp
bool isRepresentableShape(const PolygonGeometry& poly)
{
    // The same minimums the parser enforces. Refusing to write a shape we
    // would refuse to read is what keeps every file this application produces
    // loadable by it.
    const std::size_t minimum = poly.closed ? kMinPolygonVertices : kMinPolylineVertices;
    if (poly.vertices.size() < minimum) {
        return false;
    }
    for (const PointF& vertex : poly.vertices) {
        if (!isFinitePoint(vertex)) {
            return false;
        }
    }
    return true;
}

json shapeToJson(const PolygonGeometry& poly)
{
    json vertices = json::array();
    for (const PointF& vertex : poly.vertices) {
        vertices.push_back(pointToJson(vertex));
    }
    return json{
        {"type", poly.closed ? kPolygonTypeName : kPolylineTypeName},
        {"vertices", std::move(vertices)},
    };
}
```

And extend `geometryTypeName`'s `if constexpr` chain:

```cpp
        } else if constexpr (std::is_same_v<Shape, PolygonGeometry>) {
            return shape.closed ? kPolygonTypeName : kPolylineTypeName;
        }
```

- [ ] **Step 4: Read the polygon**

Add the reader and extend the switch in `readGeometry`:

```cpp
bool readPolygon(const json& value, bool closed, AnnotationGeometry& out, std::string& problem)
{
    if (!value.contains("vertices") || !value.at("vertices").is_array()) {
        problem = "geometry.vertices is not an array";
        return false;
    }

    PolygonGeometry poly;
    poly.closed = closed;
    poly.vertices.reserve(value.at("vertices").size());
    for (const json& entry : value.at("vertices")) {
        PointF vertex;
        if (!readPoint(entry, vertex)) {
            problem = "geometry.vertices contains something that is not a finite point";
            return false;
        }
        poly.vertices.push_back(vertex);
    }

    // No maximum. A dense freehand trace is legitimate data, and a cap here
    // would reject a clinician's outline for being careful; density is the
    // drawing tool's business. By the time this runs the array has already
    // been parsed and allocated, so a cap would not be a defence anyway.
    const std::size_t minimum = closed ? kMinPolygonVertices : kMinPolylineVertices;
    if (poly.vertices.size() < minimum) {
        problem = "geometry.vertices has fewer than " + std::to_string(minimum) + " points";
        return false;
    }

    out = std::move(poly);
    return true;
}
```

In `readGeometry`, between the rectangle branch and the unknown-type message:

```cpp
    if (type == kPolygonTypeName) {
        return readPolygon(value, true, out, problem);
    }
    if (type == kPolylineTypeName) {
        return readPolygon(value, false, out, problem);
    }
```

- [ ] **Step 5: Build and run the core tests**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure --timeout 120
```

Expected: PASS.

- [ ] **Step 6: Run the full suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/src/AnnotationSerialization.cpp tests/core/AnnotationSerializationTest.cpp
git commit -m "Serialise polygons and polylines

Two geometry type names rather than a closed field on the wire: readGeometry
already dispatches on a type string, and this keeps the file self-describing
for the text-editor reader the format was chosen for.

Writer and reader enforce the same vertex minimums, so the application cannot
write a file it would refuse to open. No maximum vertex count -- a dense
trace is legitimate data and density belongs to the drawing tool.
schemaVersion stays at 1."
```

---

## Task 8: Interaction decisions as pure functions

Everything `ViewportWidget` will need to decide in Task 9, in a form `ui-tests` can reach.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`
- Modify: `src/ui/src/AnnotationInteraction.cpp`
- Test: `tests/ui/AnnotationInteractionTest.cpp`

**Interfaces:**
- Consumes: `kHitToleranceScreenPixels`, `screenToleranceToSlide` (existing).
- Produces: `AnnotationTool::Polygon`, `AnnotationTool::Freehand`, `ui::kTrailMinScreenPixels = 2.0`, `ui::isNPointTool(AnnotationTool) -> bool`, `ui::shouldAppendTrailPoint(core::PointF lastSlide, core::PointF cursorSlide, double viewportScale) -> bool`, `ui::closesOnFirstVertex(core::PointF firstVertexSlide, core::PointF cursorSlide, double viewportScale) -> bool`, `ui::pendingShapeCommits(std::size_t vertexCount, bool closed) -> bool`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
TEST_CASE("isNPointTool identifies the multi-click tools", "[ui][AnnotationInteraction]")
{
    REQUIRE(isNPointTool(AnnotationTool::Polygon));
    REQUIRE(isNPointTool(AnnotationTool::Freehand));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Rectangle));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Pan));
}

TEST_CASE("the new tools own their drags", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Polygon, false) == DragOwner::Tool);
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Freehand, true) == DragOwner::Tool);
}

// Held space and the middle button pan whatever the tool -- the rule that lets
// a user reposition the slide without abandoning a half-traced outline.
TEST_CASE("space and the middle button still pan with an N-point tool active",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Polygon, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Polygon, false) == DragOwner::Pan);
}

// Screen-space, so a trace has the same vertex density at 1x and at 40x.
TEST_CASE("shouldAppendTrailPoint measures the gap in screen pixels",
          "[ui][AnnotationInteraction]")
{
    // scale 1.0: 1 slide unit is 1 screen pixel, so 2 units clears the 2 px bar.
    REQUIRE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{2.0, 0.0}, 1.0));
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1.0, 0.0}, 1.0));

    // scale 0.1 (zoomed out): the same 2 slide units is 0.2 screen pixels.
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{2.0, 0.0}, 0.1));
    REQUIRE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{20.0, 0.0}, 0.1));
}

TEST_CASE("shouldAppendTrailPoint measures diagonally", "[ui][AnnotationInteraction]")
{
    REQUIRE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1.5, 1.5}, 1.0));
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}, 1.0));
}

// A non-positive scale means the viewport has no usable transform; appending
// on every event would bury the shape in duplicate vertices.
TEST_CASE("shouldAppendTrailPoint refuses a non-positive scale",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1e6, 0.0}, 0.0));
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1e6, 0.0}, -1.0));
}

TEST_CASE("closesOnFirstVertex uses the click target radius",
          "[ui][AnnotationInteraction]")
{
    // kHitToleranceScreenPixels is 6.0.
    REQUIRE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{5.0, 0.0}, 1.0));
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{7.0, 0.0}, 1.0));
    // At 40x the same physical target is a fraction of a slide unit.
    REQUIRE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{0.1, 0.0}, 40.0));
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{1.0, 0.0}, 40.0));
}

TEST_CASE("closesOnFirstVertex refuses a non-positive scale",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{0.0, 0.0}, 0.0));
}

TEST_CASE("pendingShapeCommits enforces the geometry minimums",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(pendingShapeCommits(0, true));
    REQUIRE_FALSE(pendingShapeCommits(2, true));
    REQUIRE(pendingShapeCommits(3, true));
    REQUIRE_FALSE(pendingShapeCommits(1, false));
    REQUIRE(pendingShapeCommits(2, false));
}
```

- [ ] **Step 2: Build and verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL — `'Polygon' is not a member of 'AnnotationTool'`.

- [ ] **Step 3: Declare the API**

In `AnnotationInteraction.h`, extend the enum:

```cpp
enum class AnnotationTool
{
    Pan,
    Rectangle,
    Polygon,
    Freehand,
};
```

Add after `kHitToleranceScreenPixels`:

```cpp
/// Minimum screen-pixel gap between consecutive points of a trailed trace.
/// Screen-space, like every other tolerance here, so a trace has the same
/// density at 1x and at 40x. This is the only control on how many vertices a
/// freehand outline accumulates -- the serializer deliberately imposes no
/// maximum, because a dense trace is legitimate data.
constexpr double kTrailMinScreenPixels = 2.0;

/// True for the tools that build a shape across several press/release pairs.
/// Those tools need pending-shape state that outlives a drag, which the
/// Rectangle tool does not.
[[nodiscard]] bool isNPointTool(AnnotationTool tool);

/// True when the cursor has moved far enough from the last placed vertex to
/// deserve another one. False for a non-positive scale: with no usable
/// transform, appending on every event would bury the shape in duplicates.
[[nodiscard]] bool shouldAppendTrailPoint(core::PointF lastSlide, core::PointF cursorSlide,
                                          double viewportScale);

/// True when a click at `cursorSlide` lands on the shape's first vertex and so
/// closes it. Reuses the click-target radius rather than introducing a second
/// screen-space constant for the same kind of question.
[[nodiscard]] bool closesOnFirstVertex(core::PointF firstVertexSlide, core::PointF cursorSlide,
                                       double viewportScale);

/// True when a pending shape has enough vertices to become an annotation.
/// Mirrors core::kMinPolygonVertices / kMinPolylineVertices: a commit below
/// them is discarded, the way an undersized rectangle drag already is.
[[nodiscard]] bool pendingShapeCommits(std::size_t vertexCount, bool closed);
```

Add `#include <cstddef>`.

- [ ] **Step 4: Implement them**

In `AnnotationInteraction.cpp`, add `#include <cmath>` and `#include "slideio/viewer/core/AnnotationGeometry.h"` if not already pulled in by the header, then:

```cpp
bool isNPointTool(AnnotationTool tool)
{
    return tool == AnnotationTool::Polygon || tool == AnnotationTool::Freehand;
}

bool shouldAppendTrailPoint(core::PointF lastSlide, core::PointF cursorSlide,
                            double viewportScale)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double dxScreen = (cursorSlide.x - lastSlide.x) * viewportScale;
    const double dyScreen = (cursorSlide.y - lastSlide.y) * viewportScale;
    return std::hypot(dxScreen, dyScreen) >= kTrailMinScreenPixels;
}

bool closesOnFirstVertex(core::PointF firstVertexSlide, core::PointF cursorSlide,
                         double viewportScale)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double dxScreen = (cursorSlide.x - firstVertexSlide.x) * viewportScale;
    const double dyScreen = (cursorSlide.y - firstVertexSlide.y) * viewportScale;
    return std::hypot(dxScreen, dyScreen) <= kHitToleranceScreenPixels;
}

bool pendingShapeCommits(std::size_t vertexCount, bool closed)
{
    return vertexCount >= (closed ? core::kMinPolygonVertices : core::kMinPolylineVertices);
}
```

`resolveDragOwner` needs no change: its final `return DragOwner::Tool;` already covers every non-Pan tool.

- [ ] **Step 5: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS. Adding enum values may trigger `-Wswitch` warnings on any `switch (AnnotationTool)` without a `default`; find and complete them:

```bash
grep -rn "AnnotationTool" src/ui/src/MainWindow.cpp src/ui/src/ViewportWidget.cpp
```

`ViewportWidget::updateCursor` is the likely site. Give the new tools the same cursor as `Rectangle` for now; Task 9 revisits it only if the crosshair is wrong.

- [ ] **Step 6: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        src/ui/src/AnnotationInteraction.cpp \
        tests/ui/AnnotationInteractionTest.cpp
git commit -m "Add the N-point tools and their interaction decisions

AnnotationTool gains Polygon and Freehand, plus four pure predicates:
isNPointTool, shouldAppendTrailPoint, closesOnFirstVertex and
pendingShapeCommits.

They are free functions over plain data because no test binary in this
repository creates a QApplication, so anything decided inside ViewportWidget
cannot be covered at all. Trail density and close-on-first-vertex are both
screen-space, so they behave the same at 1x and at 40x."
```

---

## Task 9: Pending-shape state and the multi-click handlers

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` — `Impl` (around line 1410), `mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent`, `mouseDoubleClickEvent` (new), `keyPressEvent`, `focusOutEvent`, `setActiveTool`, `resetAnnotationState`, `paintEvent`
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — declare `mouseDoubleClickEvent`

**Interfaces:**
- Consumes: Task 8's predicates; `core::PolygonGeometry`; `AnnotationModel::add(AnnotationGeometry)` from Task 2.
- Produces: `Impl::PendingShape pending` and the private helpers `commitPendingShape()` and `discardPendingShape()`. Task 10 reads `m_impl->pending` to paint the preview.

No automated test: this is event plumbing in an unconstructible widget. Every decision it makes is covered by Task 8; correctness here is verified by the Task 12 manual checks.

- [ ] **Step 1: Add the state**

In `Impl`, beside `drawing` (around line 1410):

```cpp
    /// A shape being built across several press/release pairs.
    ///
    /// Every other piece of in-flight drawing state is bounded by one press
    /// and its release -- mousePressEvent early-returns while dragOwner is set,
    /// and `drawing` cannot outlive a release. A polygon lives across many of
    /// them, with dragOwner back at None in between, so it needs state of its
    /// own rather than a flag on the drag.
    struct PendingShape
    {
        std::vector<core::PointF> vertices;
        core::PointF cursorSlide;
        bool closed = true;
        bool active = false;
    };
    PendingShape pending;
```

Add `#include <vector>` if absent.

- [ ] **Step 2: Add the two helpers**

Declare `void commitPendingShape();` and `void discardPendingShape();` in the private section of `ViewportWidget.h`, and define them beside `resetAnnotationState`:

```cpp
void ViewportWidget::commitPendingShape()
{
    if (!m_impl->pending.active) {
        return;
    }
    const bool closed = m_impl->pending.closed;
    std::vector<core::PointF> vertices = std::move(m_impl->pending.vertices);
    discardPendingShape();

    // Below the minimum this is not a shape, and is discarded in silence --
    // the same way an undersized rectangle drag already is.
    if (!pendingShapeCommits(vertices.size(), closed) || !m_impl->annotationsEditable()) {
        update();
        return;
    }
    const std::string id =
        m_impl->annotations->add(core::PolygonGeometry{std::move(vertices), closed});
    m_impl->annotations->setSelected(id);
    update();
}

void ViewportWidget::discardPendingShape()
{
    m_impl->pending.vertices.clear();
    m_impl->pending.active = false;
}
```

- [ ] **Step 3: Place vertices on press**

In `mousePressEvent`, inside the `owner == DragOwner::Tool && m_impl->controller` branch, **after** its existing `screenToSlide` call (which declares `slideX` and `slideY`) and before the rectangle setup that follows:

```cpp
        if (isNPointTool(m_impl->activeTool)) {
            m_impl->dragOwner = DragOwner::Tool;
            m_impl->dragButton = event->button();
            m_impl->pressPos = event->pos();
            m_impl->movingId.clear();

            const core::PointF cursor{slideX, slideY};
            const double scale =
                m_impl->controller ? m_impl->controller->viewport().scale() : 0.0;

            if (m_impl->pending.active && m_impl->pending.closed
                && m_impl->pending.vertices.size() >= core::kMinPolygonVertices
                && closesOnFirstVertex(m_impl->pending.vertices.front(), cursor, scale)) {
                commitPendingShape();
                updateCursor();
                event->accept();
                return;
            }

            if (!m_impl->pending.active) {
                m_impl->pending.active = true;
                m_impl->pending.closed = (m_impl->activeTool == AnnotationTool::Polygon);
                m_impl->pending.vertices.clear();
            }
            m_impl->pending.vertices.push_back(cursor);
            m_impl->pending.cursorSlide = cursor;
            updateCursor();
            event->accept();
            update();
            return;
        }
```

The existing rectangle block follows unchanged.

- [ ] **Step 4: Trail on move, and track the cursor when idle**

In `mouseMoveEvent`, before the existing `dragOwner == DragOwner::Tool && m_impl->drawing` branch:

```cpp
    if (m_impl->pending.active && m_impl->controller) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);
        const core::PointF cursor{slideX, slideY};
        m_impl->pending.cursorSlide = cursor;

        // Trailing only while the button is down: with it up, the move only
        // drives the rubber band.
        if (m_impl->dragOwner == DragOwner::Tool && !m_impl->pending.vertices.empty()) {
            const double scale = m_impl->controller->viewport().scale();
            if (shouldAppendTrailPoint(m_impl->pending.vertices.back(), cursor, scale)) {
                m_impl->pending.vertices.push_back(cursor);
            }
        }
        event->accept();
        update();
    }
```

- [ ] **Step 5: Keep the shape alive across the release**

In `mouseReleaseEvent`, immediately after the early-return guard:

```cpp
    if (m_impl->pending.active) {
        // Releasing does not commit. The shape stays pending so the user can
        // keep tracing or switch to clicking individual vertices.
        m_impl->dragOwner = DragOwner::None;
        m_impl->dragButton = Qt::NoButton;
        updateCursor();
        event->accept();
        update();
        return;
    }
```

Placed before the rectangle commit block, so the selection-click path below cannot fire and clear the selection mid-shape.

- [ ] **Step 6: Commit on double-click**

Declare `void mouseDoubleClickEvent(QMouseEvent* event) override;` in `ViewportWidget.h` beside the other event handlers, and define:

```cpp
void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_impl->pending.active || event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }

    // Qt delivers press, release, doubleClick, release -- so the second press
    // has already appended a vertex at the closing position. Committing without
    // dropping it leaves a duplicate final vertex on every polygon closed this
    // way: invisible on screen, permanent in the file, and an off-by-one in
    // every vertex count a later sub-project reads.
    if (!m_impl->pending.vertices.empty()) {
        m_impl->pending.vertices.pop_back();
    }
    m_impl->dragOwner = DragOwner::None;
    m_impl->dragButton = Qt::NoButton;
    commitPendingShape();
    updateCursor();
    event->accept();
}
```

- [ ] **Step 7: Enter commits, Backspace undoes, Escape discards**

In `keyPressEvent`, before the `Qt::Key_Delete` branch:

```cpp
    if (m_impl->pending.active
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        commitPendingShape();
        event->accept();
        return;
    }

    if (m_impl->pending.active && event->key() == Qt::Key_Backspace) {
        if (!m_impl->pending.vertices.empty()) {
            m_impl->pending.vertices.pop_back();
        }
        if (m_impl->pending.vertices.empty()) {
            discardPendingShape();
        }
        event->accept();
        update();
        return;
    }
```

And make the pending shape the first arm of the Escape cascade:

```cpp
        if (m_impl->pending.active) {
            discardPendingShape();
            m_impl->dragOwner = DragOwner::None;
            m_impl->dragButton = Qt::NoButton;
            cancelled = true;
        } else if (m_impl->drawing) {
```

- [ ] **Step 8: Wire the lifecycle**

In `resetAnnotationState()`, beside `m_impl->drawing = false;`:

```cpp
    // A half-traced shape is never committed on slide close. It is not in the
    // model, persistence never sees it, and materialising a partial outline
    // into a saved file would be worse than losing it.
    discardPendingShape();
```

In `setActiveTool()`, after the equality guard and before `m_impl->activeTool = tool;`:

```cpp
    // Tool changes need not touch `drawing`, which cannot outlive a press. The
    // pending shape can, so it has to be dropped here explicitly.
    discardPendingShape();
```

In `focusOutEvent`, beside the existing `m_impl->drawing = false;`, add **only a comment** — the pending shape deliberately survives:

```cpp
    // The pending shape is deliberately NOT discarded here. `drawing` is,
    // because a drag whose release went to another window is unrecoverable; a
    // pending polygon holds no button and is not in that position. Alt-tabbing
    // to check a report mid-trace and coming back to an empty canvas would
    // destroy minutes of careful work for no reason.
```

In `paintEvent`, extend the guard that decides whether to construct the `QPainter`:

```cpp
    if (!m_impl->annotations->annotations().empty() || m_impl->drawing
        || m_impl->pending.active) {
```

- [ ] **Step 9: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS.

- [ ] **Step 10: Commit**

```bash
git add src/ui
git commit -m "Build polygons and freehand lines across press/release pairs

A PendingShape in ViewportWidget::Impl, because every other piece of
in-flight drawing state is bounded by one press and its release while this
is not. Click places a vertex, drag trails them, double-click or Enter
commits, a click on vertex 1 closes a polygon, Backspace drops the last
vertex, Escape discards the shape.

Double-click pops the vertex the second press already placed: Qt delivers
press, release, doubleClick, release, so committing as-is would leave a
duplicate final vertex in every file.

The shape is discarded on slide close and on a tool change, and deliberately
survives focus loss -- the comment at focusOutEvent says why, since the line
above it does the opposite."
```

---

## Task 10: Draw the in-progress shape

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` — the preview block at the end of `paintAnnotations`

**Interfaces:**
- Consumes: `m_impl->pending` from Task 9; `toScreen` from `paintAnnotations`.
- Produces: nothing outside the function.

- [ ] **Step 1: Replace the preview block**

Keep the existing `if (m_impl->drawing)` rectangle preview exactly as it is, and add below it:

```cpp
    if (m_impl->pending.active && !m_impl->pending.vertices.empty()) {
        const QColor previewColor(0xE6, 0x7E, 0x22);
        QPen previewPen(previewColor, 2.0, Qt::DashLine);
        previewPen.setJoinStyle(Qt::RoundJoin);
        previewPen.setCapStyle(Qt::RoundCap);
        painter.setPen(previewPen);
        painter.setBrush(Qt::NoBrush);

        QPolygonF placed;
        placed.reserve(static_cast<int>(m_impl->pending.vertices.size()));
        for (const core::PointF& vertex : m_impl->pending.vertices) {
            placed.append(toScreen(vertex));
        }
        if (placed.size() > 1) {
            painter.drawPolyline(placed);
        }

        // The rubber band from the last placed vertex to the cursor.
        const QPointF cursor = toScreen(m_impl->pending.cursorSlide);
        painter.drawLine(placed.back(), cursor);

        if (m_impl->pending.closed && placed.size() >= 2) {
            // A fainter segment back to the first vertex, so the user can see
            // the shape they will get rather than guess it.
            QColor ghost = previewColor;
            ghost.setAlpha(0x60);
            painter.setPen(QPen(ghost, 2.0, Qt::DotLine));
            painter.drawLine(cursor, placed.front());
            painter.setPen(previewPen);
        }

        // The close target. This marker is the only thing that makes
        // click-to-close discoverable -- without it the gesture exists but
        // nobody finds it.
        if (m_impl->pending.closed && m_impl->controller
            && m_impl->pending.vertices.size() >= core::kMinPolygonVertices
            && closesOnFirstVertex(m_impl->pending.vertices.front(),
                                   m_impl->pending.cursorSlide,
                                   m_impl->controller->viewport().scale())) {
            painter.setBrush(QBrush(previewColor));
            painter.drawEllipse(placed.front(), kHitToleranceScreenPixels,
                                kHitToleranceScreenPixels);
            painter.setBrush(Qt::NoBrush);
        }
    }
```

Add `#include <QPolygonF>` if not already present.

- [ ] **Step 2: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS.

- [ ] **Step 3: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Preview the shape being traced

Placed vertices in the existing orange dashed preview pen, a rubber band to
the cursor, a fainter closing segment so the user sees the shape they will
get, and a filled marker on vertex 1 whenever the cursor is close enough to
close there. The marker is what makes click-to-close discoverable."
```

---

## Task 11: Toolbar actions and the status hint

**Files:**
- Modify: `src/ui/src/MainWindow.cpp` — `Impl` members (near line 131), action construction (near line 259), menu (near line 351), `viewportWidget->addAction` (near line 455), `triggered` handlers (near line 460), `activeToolChanged` and `annotationsAvailableChanged` handlers (near lines 500 and 505), the disable at line 767

**Interfaces:**
- Consumes: `AnnotationTool::Polygon`, `AnnotationTool::Freehand`, `isNPointTool` from Task 8.
- Produces: `Impl::polygonToolAction`, `Impl::freehandToolAction`.

- [ ] **Step 1: Declare the actions**

Beside `QAction* rectangleToolAction = nullptr;`:

```cpp
    QAction* polygonToolAction = nullptr;
    QAction* freehandToolAction = nullptr;
```

- [ ] **Step 2: Construct them**

After the `rectangleToolAction` block:

```cpp
        polygonToolAction = new QAction("&Polygon", owner);
        polygonToolAction->setCheckable(true);
        polygonToolAction->setShortcut(QKeySequence("P"));
        polygonToolAction->setStatusTip(
            "Draw a polygon by clicking points, or by dragging to trace");

        freehandToolAction = new QAction("&Freehand Line", owner);
        freehandToolAction->setCheckable(true);
        freehandToolAction->setShortcut(QKeySequence("F"));
        freehandToolAction->setStatusTip("Draw an open freehand line");
```

Mirror every subsequent line that mentions `rectangleToolAction` for both new actions: `setShortcutContext(Qt::WidgetWithChildrenShortcut)`, `toolGroup->addAction(...)`, `setEnabled(false)`, `toolsMenu->addAction(...)`, `viewportWidget->addAction(...)`, and the `setEnabled(available)` in the `annotationsAvailableChanged` handler and the `setEnabled(false)` at line 767.

- [ ] **Step 3: Wire triggered and checked state**

Beside the `rectangleToolAction` `triggered` connection:

```cpp
        QObject::connect(polygonToolAction, &QAction::triggered, owner, [this]() {
            m_impl->viewportWidget->setActiveTool(AnnotationTool::Polygon);
        });
        QObject::connect(freehandToolAction, &QAction::triggered, owner, [this]() {
            m_impl->viewportWidget->setActiveTool(AnnotationTool::Freehand);
        });
```

In the `activeToolChanged` handler beside line 500:

```cpp
                             polygonToolAction->setChecked(tool == AnnotationTool::Polygon);
                             freehandToolAction->setChecked(tool == AnnotationTool::Freehand);
```

Adapt the capture style to whatever the surrounding lambda already uses.

- [ ] **Step 4: Check the availability handler for the known regression**

Read `MainWindow.cpp:659-665` before touching the `annotationsAvailableChanged` handler. `closeSlide()` emits an unavailable signal on **every** slide switch, so a handler that resets the active tool when annotations become unavailable drops the user back to Pan on every slide of a series. That regression is recorded there as deliberately avoided, behind an `isSlideOpen()` guard.

The two new actions must only have their `setEnabled(available)` mirrored. Do not add any `setActiveTool` call to that handler.

- [ ] **Step 5: Add the status hint**

In the `activeToolChanged` handler, after the `setChecked` lines:

```cpp
                             if (isNPointTool(tool)) {
                                 // A user who clicks once and does not know how
                                 // to finish has no affordance at all otherwise.
                                 statusBar()->showMessage(
                                     tr("Click to add points, double-click or Enter to "
                                        "finish, Esc to cancel"));
                             }
```

Match the surrounding code's way of reaching the status bar — if `MainWindow` already holds a status-label member rather than calling `statusBar()`, use that.

- [ ] **Step 6: Build and run everything**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure --timeout 120
```

Expected: all four suites PASS.

- [ ] **Step 7: Install and launch the viewer**

```bash
cmake --install build/build --config Release
```

Then run `build/install/release/bin/slideio-viewer.exe`. The build-tree executable will not start.

Confirm: with no slide open, Polygon and Freehand are disabled; with a slide open, both are enabled, `P` and `F` select them, and they are mutually exclusive with Pan and Rectangle.

- [ ] **Step 8: Commit**

```bash
git add src/ui/src/MainWindow.cpp
git commit -m "Add the Polygon and Freehand tool actions

Gated exactly as rectangleToolAction is, and deliberately without any
setActiveTool call in the availability handler: closeSlide() now emits an
unavailable signal on every slide switch, so resetting the tool there drops
the user back to Pan on every slide of a series -- the regression
MainWindow.cpp:659-665 records as avoided.

A status-bar hint while an N-point tool is active, which with the
first-vertex marker is what makes a modal gesture discoverable."
```

---

## Task 12: Manual verification and documentation

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md`
- Modify: `docs/superpowers/specs/2026-10-10-annotation-foundation-followups.md`

- [ ] **Step 1: Install and run the viewer**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

Run `build/install/release/bin/slideio-viewer.exe` and open a slide.

- [ ] **Step 2: Work through the manual checks**

Record the result of each. These cannot be automated — no suite creates a `QApplication`.

1. **Trace a polygon and close it three ways:** click on vertex 1 (the marker should appear as you approach it), double-click, and Enter. Each produces a filled outline following the points you placed.
2. **Double-click has no duplicate vertex.** Close one polygon by double-click, save, and open the `.annotations.json` file in the workspace folder. Count the entries in `vertices`. It must equal the number of points you placed. The duplicate is invisible on screen, so this must be checked in the file.
3. **Escape mid-trace** discards the shape; **Backspace** drops one vertex at a time and the preview shortens.
4. **Alt-tab away mid-trace and come back.** The pending shape is still there.
5. **Switch tools mid-trace.** The pending shape is gone.
6. **Drag a committed polygon.** It keeps its shape and vertex count — it does not become a rectangle. This is the regression foundation item 1 was filed to prevent.
7. **Click inside the notch of a concave polygon.** Nothing selects. Click inside the solid part: it selects, and the selection outline follows the shape rather than a box.
8. **Close the slide and reopen it.** The polygon is back, unchanged.
9. **Trace a freehand line.** It is not filled, and its two ends are not joined.
10. **Trail density:** hold the button and trace quickly. The outline follows the cursor without visible corners, and the saved file's vertex count is proportional to the on-screen length, not to the time taken.

- [ ] **Step 3: Record the outcome in the follow-ups**

In `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md`, under item 0, append the results of Step 2 in the same style as the existing "Verified by the user" line. Note anything that failed as a new numbered item rather than fixing it here.

Update item 16 to reflect §3.5 of the design spec: the annotation-level `"type"` field is still ignored on read, but is now derived from the geometry on write, so writer and reader cannot disagree about it.

- [ ] **Step 4: Close the foundation items**

In `docs/superpowers/specs/2026-10-10-annotation-foundation-followups.md`, mark items 1, 2, 3 and 12 closed with the date and this plan's commits, in the same style the file already uses for items 4, 5 and 6 ("Closed 2026-10-11 by E1"). Keep the original text — each records why the decision went the way it did.

Item 9 stays open: the per-annotation cull added in Task 3 is unrelated to annotations vanishing when no tile is visible.

- [ ] **Step 5: Update `CLAUDE.md`**

Correct these, which are now stale:

- **Current state** says the viewer "supports rectangle annotations (draw, select, move, delete)". It now supports rectangles, polygons and freehand lines.
- **Not built yet** lists "annotation shapes other than rectangles". Remove it; replace with what remains unbuilt — vertex editing, ellipse, circle, point, arrow and text.
- **Key Design Decisions** says annotation geometry "uses `std::variant`, currently with one alternative (Rectangle)… Two sites rebuild a shape from its bounding box and `static_assert` the variant's size". That mechanism has done its job and is gone. Replace with the invariants that now hold:

```markdown
- **Annotation geometry** uses `std::variant` over `RectangleGeometry` and
  `PolygonGeometry`, with free functions — `boundingBox`, `hitTest`, `typeOf`,
  `outline`, `translated` — dispatching by `std::visit`. One `PolygonGeometry`
  covers both the closed polygon and the open freehand line; they differ only
  in a `closed` flag. Adding a shape means adding an overload to each of those
  five, and nothing else.
- **`AnnotationType` is derived from the geometry**, never stored beside it.
  `Annotation::type()` delegates to `core::typeOf()`, and the annotation-level
  `"type"` field in a saved file is written from the geometry and ignored on
  read. The two cannot disagree.
- **The painted interior and the hit-tested interior must match.**
  `core::hitTest` uses even-odd ray casting; `paintAnnotations` sets
  `Qt::OddEvenFill`. A self-intersecting trace is valid data — pathologists
  produce them — and these two must agree about which parts of one are inside.
- **A pending multi-click shape is not an annotation.** It lives in
  `ViewportWidget::Impl::pending`, never reaches `AnnotationModel`, and is
  therefore never saved. It is discarded on slide close and on a tool change,
  and deliberately survives focus loss.
```

Add to the **Annotation files are keyed by** group:

```markdown
- **`schemaVersion` does not move when a geometry type is added.** A higher
  version is refused outright, so bumping it would make every shipped build
  refuse rectangle-only files during a staggered rollout. An old build meeting
  a new shape fails on that shape alone, names it, and leaves the file intact.
```

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs
git commit -m "Record the geometry widening and close the foundation items

Foundation follow-up items 1, 2, 3 and 12 are closed; item 9 stays open --
the per-annotation cull is unrelated to annotations vanishing when no tile is
visible. Persistence item 16 shifts rather than closes.

CLAUDE.md's annotation section described a one-alternative variant guarded by
static_asserts, which was true until this work and is the mechanism that
forced it."
```

---

## Notes for the executor

- **The ordering is the design.** Tasks 1–5 deliberately change no behaviour: they replace faked per-type dispatch with real dispatch while the variant still holds one alternative. Task 6 then widens it and everything already compiles. If a task is attempted out of order, the `static_assert`s will stop you — that is what they are for.
- **`core-tests` is the only test executable safe to run directly.** Everything else must go through `ctest`, always with `--timeout 120`.
- **The build-tree viewer will not start.** Install first and run from `build/install/release/bin/`.
- **If Conan is needed,** `conan.exe` is not on the default PATH; prepend `/c/Users/Stanislav/anaconda3/envs/conan2/Scripts` before invoking `build.sh`.
- **Tasks 3, 9, 10 and 11 have no automated tests** because they live inside a widget no suite can construct. Their decisions are covered by Tasks 1, 6 and 8; their integration is covered by Task 12's manual checks. Do not invent a test that constructs a `QApplication` to close the gap — that would be a new, larger decision about the test harness.
