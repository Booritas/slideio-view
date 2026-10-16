# Annotation Ellipse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `EllipseGeometry` to the annotation domain model, the drawing tool that produces it, and the Shift/Alt drag modifiers both it and the rectangle have always been specified to have.

**Architecture:** The ellipse is stored as a bounding box — two points, exactly like `RectangleGeometry` — and `outline()` expands it into a fixed 512-vertex closed polygon. That polygon *is* the shape: the painter strokes it, `hitTest` ray-casts it and `measure()` integrates it, so the three cannot disagree. The modifiers live in one pure, shape-agnostic function over a `RectF`, which both the ellipse and the rectangle tool call.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+, Conan 2, Catch2 v3, four ctest suites.

**Spec:** `docs/superpowers/specs/2026-10-16-annotation-ellipse-design.md` (committed `ac87ce7`)

## Global Constraints

- Strict downward layering: `core` (Qt-free, links no Qt target) → `app` → `infra` → `ui`. Nothing in `core` may include a Qt header.
- **No test binary creates a `QApplication`, so no test may construct a widget.** Logic that needs testing is a pure function over plain data.
- **Only `core-tests` is safe to run as a bare `.exe`.** `app-tests`, `infra-tests` and `ui-tests` find their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property; running the `.exe` pops a modal Windows error box that blocks until a human dismisses it. Always go through `ctest` for those three.
- `schemaVersion` **stays at 1**. Adding a geometry type never moves it.
- Naming: `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, 4-space indent, 120-char lines, `#pragma once`.
- **No new files are created by this plan**, so no `CMakeLists.txt` is edited and no `cmake` reconfigure is needed. Every change lands in a file already listed.
- A running viewer turns `cmake --install` into a silent no-op that still exits 0. Verify the installed artifact by timestamp or hash, never by exit code.
- All AI-generated code must be human-reviewed before commit.

## Review Focus

Input classes the spec implies that are most likely to bite, each pinned by a test in the task that owns the code:

1. **A stored ellipse with zero extent on one axis** (hand-edited file). Spec §2.2 says it loads, measures zero and stays clickable — it must never make the whole document unloadable. → Task 1 (load, hit-test) and Task 2 (area).
2. **A degenerate ellipse through `measure()`.** Its 512 vertices are collinear, which trips `isSelfIntersecting`'s collinear-overlap rule and reports "self-intersecting, area unknown" for a shape whose area is plainly 0. → Task 2.
3. **Modifiers other than Shift and Alt held during a drag** (Ctrl, Meta, Shift+Ctrl). Must leave the box untouched rather than falling into a constraint branch. → Task 3.
4. **A zero-length drag with Shift or Alt held** (press and release without moving). Must not produce NaN, an inverted box, or a division by zero. → Task 3.
5. **An inverted bounding box arriving from a file** — `topLeft` below and right of `bottomRight`. Outline, bounding box, hit-test and area must all match the normalised form. → Task 1.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/core/include/slideio/viewer/core/AnnotationGeometry.h` | `EllipseGeometry`, `AnnotationType::Ellipse`, `kEllipseOutlineVertices`, `isConvexByConstruction` declaration | 1 |
| `src/core/src/AnnotationGeometry.cpp` | the six dispatch overloads for the ellipse | 1 |
| `src/core/src/AnnotationSerialization.cpp` | `"ellipse"` type name, `shapeToJson`, `readEllipse`, the `readGeometry` branch | 1 |
| `tests/core/AnnotationGeometryTest.cpp` | geometry and convexity cases | 1 |
| `tests/core/AnnotationSerializationTest.cpp` | the round-trip that forces the `readGeometry` branch to exist | 1 |
| `src/core/src/AnnotationMeasurement.cpp` | the convexity short-circuit in `measure()` | 2 |
| `tests/core/AnnotationMeasurementTest.cpp` | ellipse area, and the degenerate case | 2 |
| `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` | `applyDragConstraints` declaration; `AnnotationTool::Ellipse` | 3, 4 |
| `src/ui/src/AnnotationInteraction.cpp` | `applyDragConstraints` implementation | 3 |
| `tests/ui/AnnotationInteractionTest.cpp` | modifier cases; tool-routing pins | 3, 4 |
| `src/ui/src/ViewportWidget.cpp` | commit path, preview, stored drag modifiers | 4 |
| `src/ui/src/MainWindow.cpp` | `ellipseToolAction`, shortcut `E`, checked-state sync | 4 |
| `CLAUDE.md` | the "Not built yet" list and the geometry decision note | 4 |

**Why Task 1 is one task and not three.** Adding a member to `AnnotationGeometry` breaks the build at `geometryTypeName`'s `static_assert` and at the `shapeToJson` overload set. The library therefore does not compile between adding the variant member and finishing the serializer, so they cannot be separate commits. The spec says the same thing from the other direction (§5.3): the round-trip test must land "in the same commit that adds the variant member".

---

## Task 1: `EllipseGeometry` in the domain model and the file format

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`
- Modify: `src/core/src/AnnotationGeometry.cpp`
- Modify: `src/core/src/AnnotationSerialization.cpp`
- Test: `tests/core/AnnotationGeometryTest.cpp`
- Test: `tests/core/AnnotationSerializationTest.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `struct EllipseGeometry { PointF topLeft; PointF bottomRight; };`
  - `AnnotationType::Ellipse`
  - `constexpr std::size_t kEllipseOutlineVertices = 512;`
  - `[[nodiscard]] bool isConvexByConstruction(const AnnotationGeometry& geom);`
  - the JSON type name `"ellipse"`

- [ ] **Step 1: Write the failing geometry tests**

Append to `tests/core/AnnotationGeometryTest.cpp`. Add `#include <cmath>` to the include block at the top if it is not already there, and add this helper to the existing anonymous namespace beside `rect`:

```cpp
AnnotationGeometry ellipseGeom(double x0, double y0, double x1, double y1)
{
    return EllipseGeometry{PointF{x0, y0}, PointF{x1, y1}};
}
```

Then the cases:

```cpp
TEST_CASE("an ellipse outline has the fixed vertex count and is closed",
          "[core][AnnotationGeometry]")
{
    const Outline shape = outline(ellipseGeom(0.0, 0.0, 100.0, 50.0));
    REQUIRE(shape.points.size() == kEllipseOutlineVertices);
    REQUIRE(shape.closed);
}

// The first vertex is the rightmost point of the curve -- theta = 0. Fixing
// this pins the parameterisation, so a later change to the loop that happened
// to preserve the vertex count would still be caught.
TEST_CASE("an ellipse outline starts at the rightmost point", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(ellipseGeom(0.0, 0.0, 100.0, 50.0));
    REQUIRE(std::abs(shape.points.front().x - 100.0) < 1e-9);
    REQUIRE(std::abs(shape.points.front().y - 25.0) < 1e-9);
}

// Every vertex lies ON the curve. The polygon is inscribed, which is what makes
// the measured area slightly low in a known, documented direction (spec 3.2).
TEST_CASE("every ellipse outline vertex satisfies the ellipse equation",
          "[core][AnnotationGeometry]")
{
    const Outline shape = outline(ellipseGeom(10.0, 20.0, 110.0, 70.0));
    const double cx = 60.0;
    const double cy = 45.0;
    const double a = 50.0;
    const double b = 25.0;
    for (const PointF& point : shape.points) {
        const double nx = (point.x - cx) / a;
        const double ny = (point.y - cy) / b;
        REQUIRE(std::abs(nx * nx + ny * ny - 1.0) < 1e-9);
    }
}

TEST_CASE("boundingBox normalises an inverted ellipse drag", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(ellipseGeom(110.0, 70.0, 10.0, 20.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

// Review Focus 5: a file may hold either winding. Both must describe one shape.
TEST_CASE("an inverted ellipse has the same outline as the normalised one",
          "[core][AnnotationGeometry]")
{
    const Outline normal = outline(ellipseGeom(10.0, 20.0, 110.0, 70.0));
    const Outline inverted = outline(ellipseGeom(110.0, 70.0, 10.0, 20.0));
    REQUIRE(normal.points.size() == inverted.points.size());
    for (std::size_t i = 0; i < normal.points.size(); ++i) {
        REQUIRE(std::abs(normal.points[i].x - inverted.points[i].x) < 1e-9);
        REQUIRE(std::abs(normal.points[i].y - inverted.points[i].y) < 1e-9);
    }
}

TEST_CASE("typeOf reports an ellipse", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(ellipseGeom(0.0, 0.0, 10.0, 20.0)) == AnnotationType::Ellipse);
}

TEST_CASE("translating an ellipse moves it and preserves its extents",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved = translated(ellipseGeom(10.0, 20.0, 110.0, 70.0), 5.0, -7.0);
    const RectF box = boundingBox(moved);
    REQUIRE(box.x == 15.0);
    REQUIRE(box.y == 13.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
    REQUIRE(typeOf(moved) == AnnotationType::Ellipse);
}

// The whole point of the shape: the corner of the bounding box is outside the
// curve. A rectangle of the same box would be hit here.
TEST_CASE("an ellipse is not hit in the corner of its bounding box",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry shape = ellipseGeom(0.0, 0.0, 100.0, 50.0);
    REQUIRE(hitTest(shape, PointF{50.0, 25.0}, 0.0));
    REQUIRE_FALSE(hitTest(shape, PointF{0.0, 0.0}, 0.0));
    REQUIRE(hitTest(shape, PointF{100.0, 25.0}, 0.0));
}

TEST_CASE("a point just outside an ellipse is hit within the tolerance band",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry shape = ellipseGeom(0.0, 0.0, 100.0, 50.0);
    REQUIRE_FALSE(hitTest(shape, PointF{103.0, 25.0}, 0.0));
    REQUIRE(hitTest(shape, PointF{103.0, 25.0}, 4.0));
}

// The header promises a negative tolerance is treated as zero. An unclamped
// one would shrink the shape instead of growing it.
TEST_CASE("a negative tolerance on an ellipse behaves as zero",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry shape = ellipseGeom(0.0, 0.0, 100.0, 50.0);
    REQUIRE(hitTest(shape, PointF{50.0, 25.0}, -10.0));
    REQUIRE_FALSE(hitTest(shape, PointF{0.0, 0.0}, -10.0));
}

// Review Focus 1: a hand-edited file may hold one. It must still be clickable
// rather than becoming an invisible, unselectable annotation.
TEST_CASE("a degenerate ellipse is still hit along its line", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry flat = ellipseGeom(0.0, 25.0, 100.0, 25.0);
    REQUIRE(hitTest(flat, PointF{50.0, 25.0}, 0.5));
}

TEST_CASE("convexity by construction is known per shape", "[core][AnnotationGeometry]")
{
    REQUIRE(isConvexByConstruction(ellipseGeom(0.0, 0.0, 100.0, 50.0)));
    REQUIRE(isConvexByConstruction(rect(0.0, 0.0, 100.0, 50.0)));
    // A polygon's vertices are the user's; nothing can promise they are convex.
    REQUIRE_FALSE(isConvexByConstruction(
        PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{10.0, 10.0}}, true}));
}
```

- [ ] **Step 2: Write the failing serializer tests**

Append to `tests/core/AnnotationSerializationTest.cpp`:

```cpp
// Spec 5.3: readGeometry is a string chain with no compile-time link to the
// variant. Nothing in the compiler forces its ellipse branch to exist -- this
// test is what forces it. Without the branch the application writes files it
// then refuses to open.
TEST_CASE("an ellipse round-trips through JSON", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "e1", EllipseGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));

    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);

    const AnnotationGeometry& geometry = result.document.annotations.front().geometry();
    REQUIRE(std::holds_alternative<EllipseGeometry>(geometry));
    const EllipseGeometry& ellipse = std::get<EllipseGeometry>(geometry);
    REQUIRE(ellipse.topLeft.x == 10.0);
    REQUIRE(ellipse.topLeft.y == 20.0);
    REQUIRE(ellipse.bottomRight.x == 110.0);
    REQUIRE(ellipse.bottomRight.y == 70.0);
    REQUIRE(result.document.annotations.front().type() == AnnotationType::Ellipse);
}

TEST_CASE("an ellipse is written with the ellipse type name",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "e1", EllipseGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"ellipse\"") != std::string::npos);
}

// Spec 2.2: accepted, not rejected. Refusing would make the whole document
// permanently unloadable -- and by the governing rule, never again writable --
// over one shape the user can see and fix.
TEST_CASE("a degenerate ellipse loads rather than failing the document",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "e1", EllipseGeometry{PointF{10.0, 25.0}, PointF{110.0, 25.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);
}

// The staggered-rollout contract (spec 5.2): an older build must fail on the
// shape alone and name it, not on the version.
TEST_CASE("an unknown geometry type is refused by name",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    std::string json = serializeAnnotationDocument(document);
    const std::size_t pos = json.find("\"rectangle\"");
    REQUIRE(pos != std::string::npos);
    json.replace(pos, std::string("\"rectangle\"").size(), "\"trapezoid\"");

    const ParseResult result = parseAnnotationDocument(json);
    REQUIRE(result.error != ParseError::None);
    REQUIRE(result.message.find("trapezoid") != std::string::npos);
}
```

Add `#include <variant>` to that file's include block if it is not already there.

- [ ] **Step 3: Run both test files to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `EllipseGeometry` as undeclared in `tests/core/AnnotationGeometryTest.cpp` and `tests/core/AnnotationSerializationTest.cpp`. This is the C++ red state — the type does not exist yet.

- [ ] **Step 4: Add the type, the enum value and the constant to the header**

In `src/core/include/slideio/viewer/core/AnnotationGeometry.h`, after `RectangleGeometry`:

```cpp
/// An axis-aligned ellipse, stored as the bounding box of the drag that made
/// it (FR-ANN-02).
///
/// The box rather than a centre and radii, so RectangleGeometry's
/// normalisation of an inverted drag, ui::dragExceedsMinimumSize and
/// ui::withMinimumExtent all apply unchanged rather than through conversions --
/// and so the written file reads the same way a rectangle's does.
struct EllipseGeometry
{
    PointF topLeft;
    PointF bottomRight;
};
```

Add `Ellipse,` to `AnnotationType`, after `Rectangle,`.

Add `EllipseGeometry` to the variant:

```cpp
using AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry, EllipseGeometry>;
```

Add the constant beside `kMinPolygonVertices`:

```cpp
/// Vertices in an ellipse's outline. Fixed, never adaptive and never derived
/// from the shape's size or the current zoom: an adaptive count would make a
/// stored annotation's measured area depend on the zoom it happened to be
/// measured at, and sub-project G exists to make measurements reproducible.
///
/// At 512 the area reads about 0.0025% under the true ellipse and the facet
/// bulge is about 0.19 px on a shape 20000 screen pixels wide. See the design
/// spec 3.2 before changing it -- both numbers move with it.
constexpr std::size_t kEllipseOutlineVertices = 512;
```

Declare the predicate beside the other free functions:

```cpp
/// True when the shape cannot self-intersect whatever its values -- a property
/// of the TYPE, not of the instance.
///
/// core::measure() uses it to skip the O(n^2) self-intersection test, which at
/// kEllipseOutlineVertices costs about 131000 segment-pair comparisons to
/// answer a question that is structurally always false. A polygon's vertices
/// are the user's, so nothing can promise anything about them.
[[nodiscard]] bool isConvexByConstruction(const AnnotationGeometry& geom);
```

- [ ] **Step 5: Add the six dispatch overloads**

In `src/core/src/AnnotationGeometry.cpp`, inside the anonymous namespace. The ellipse overloads of `hitTestShape` and `outlineShape` must be placed **after** the `PolygonGeometry` ones, because the ellipse delegates to them and C++ resolves by declaration order.

Add near the top of the anonymous namespace:

```cpp
constexpr double kPi = 3.14159265358979323846;
```

Beside `normalized(const RectangleGeometry&)`:

```cpp
RectF normalized(const EllipseGeometry& ellipse)
{
    const double x0 = std::min(ellipse.topLeft.x, ellipse.bottomRight.x);
    const double y0 = std::min(ellipse.topLeft.y, ellipse.bottomRight.y);
    const double x1 = std::max(ellipse.topLeft.x, ellipse.bottomRight.x);
    const double y1 = std::max(ellipse.topLeft.y, ellipse.bottomRight.y);
    return RectF{x0, y0, x1 - x0, y1 - y0};
}
```

After `outlineShape(const PolygonGeometry&)`:

```cpp
Outline outlineShape(const EllipseGeometry& ellipse)
{
    const RectF box = normalized(ellipse);
    const double cx = box.x + box.width * 0.5;
    const double cy = box.y + box.height * 0.5;
    const double a = box.width * 0.5;
    const double b = box.height * 0.5;

    Outline result;
    result.closed = true;
    result.points.reserve(kEllipseOutlineVertices);
    for (std::size_t i = 0; i < kEllipseOutlineVertices; ++i) {
        const double theta = 2.0 * kPi * static_cast<double>(i)
                           / static_cast<double>(kEllipseOutlineVertices);
        result.points.push_back(PointF{cx + a * std::cos(theta), cy + b * std::sin(theta)});
    }
    return result;
}

// The outline IS the shape (design spec 3), so the hit test runs over exactly
// the points the painter strokes. Deriving both from one source is what stops
// the painted interior and the hit-tested interior from disagreeing.
bool hitTestShape(const EllipseGeometry& ellipse, PointF point, double tolerance)
{
    const Outline shape = outlineShape(ellipse);
    return hitTestShape(PolygonGeometry{shape.points, true}, point, tolerance);
}

AnnotationType typeOfShape(const EllipseGeometry&)
{
    return AnnotationType::Ellipse;
}

AnnotationGeometry translatedShape(const EllipseGeometry& ellipse, double dx, double dy)
{
    return EllipseGeometry{PointF{ellipse.topLeft.x + dx, ellipse.topLeft.y + dy},
                           PointF{ellipse.bottomRight.x + dx, ellipse.bottomRight.y + dy}};
}

bool convexShape(const RectangleGeometry&)
{
    return true;
}

bool convexShape(const EllipseGeometry&)
{
    return true;
}

bool convexShape(const PolygonGeometry&)
{
    return false;
}
```

And outside the anonymous namespace, beside the other dispatchers:

```cpp
bool isConvexByConstruction(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return convexShape(shape); }, geom);
}
```

Add `#include <variant>` to the file's include block if it is not already there.

- [ ] **Step 6: Run the build to watch the serializer guard fire**

Run: `cmake --build build/build --config Release`

Expected: **compile failure in `src/core/src/AnnotationSerialization.cpp`**, at the `static_assert` in `geometryTypeName` reading "A new geometry alternative needs its own type name here.", and at the `shapeToJson` overload set. This is the write-side guard doing its job; seeing it fire is the point of this step.

- [ ] **Step 7: Add the serializer write and read sides**

In `src/core/src/AnnotationSerialization.cpp`:

Beside the other type-name constants near line 32:

```cpp
constexpr const char* kEllipseTypeName = "ellipse";
```

After `shapeToJson(const RectangleGeometry&)`:

```cpp
json shapeToJson(const EllipseGeometry& ellipse)
{
    return json{
        {"type", kEllipseTypeName},
        {"topLeft", pointToJson(ellipse.topLeft)},
        {"bottomRight", pointToJson(ellipse.bottomRight)},
    };
}
```

In `geometryTypeName`, add a branch before the `else`:

```cpp
        } else if constexpr (std::is_same_v<Shape, EllipseGeometry>) {
            return kEllipseTypeName;
```

Beside `readRectangle`:

```cpp
bool readEllipse(const json& value, AnnotationGeometry& out, std::string& problem)
{
    EllipseGeometry ellipse;
    if (!value.contains("topLeft") || !readPoint(value.at("topLeft"), ellipse.topLeft)) {
        problem = "geometry.topLeft is not a finite point";
        return false;
    }
    if (!value.contains("bottomRight") || !readPoint(value.at("bottomRight"), ellipse.bottomRight)) {
        problem = "geometry.bottomRight is not a finite point";
        return false;
    }
    // A zero-extent ellipse is accepted, exactly as a degenerate rectangle is
    // (design spec 2.2): refusing it would make the whole document unloadable,
    // and by the governing rule never again writable, over one shape the user
    // can see and fix.
    out = ellipse;
    return true;
}
```

And in `readGeometry`, after the rectangle branch:

```cpp
    if (type == kEllipseTypeName) {
        return readEllipse(value, out, problem);
    }
```

- [ ] **Step 8: Run the core tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`build/build/Release/slideio-viewer-core-tests.exe "[AnnotationGeometry],[AnnotationSerialization]"`

Expected: PASS, all cases, no warnings in the build output.

- [ ] **Step 9: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass. Nothing outside `core` was touched, so a failure here is a real regression — most likely an exhaustive `std::visit` somewhere that now has an unhandled alternative.

- [ ] **Step 10: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationGeometry.h \
        src/core/src/AnnotationGeometry.cpp \
        src/core/src/AnnotationSerialization.cpp \
        tests/core/AnnotationGeometryTest.cpp \
        tests/core/AnnotationSerializationTest.cpp
git commit -m "Add the ellipse geometry and its file format"
```

---

## Task 2: The convexity short-circuit in `measure()`

**Files:**
- Modify: `src/core/src/AnnotationMeasurement.cpp`
- Test: `tests/core/AnnotationMeasurementTest.cpp`

**Interfaces:**
- Consumes: `EllipseGeometry`, `kEllipseOutlineVertices`, `isConvexByConstruction(const AnnotationGeometry&)` from Task 1.
- Produces: nothing new. `measure()` keeps its signature `ShapeMetrics measure(const AnnotationGeometry&, PixelSize)`.

**Why this is its own task.** After Task 1 an ellipse already measures correctly — it is a closed shape and the shoelace formula over its outline is what the properties panel already runs for a polygon. What is wrong is the cost and one edge case: `measure()` asks `isSelfIntersecting`, which is O(n²), and at 512 vertices that is roughly 131000 segment-pair tests to answer a question that is structurally always false. The edge case is Review Focus 2 and it is a real behaviour change, which is what makes this independently reviewable.

- [ ] **Step 1: Write the failing test**

Append to `tests/core/AnnotationMeasurementTest.cpp`. Add `#include "slideio/viewer/core/AnnotationGeometry.h"` to the include block if it is not already there.

```cpp
// A degenerate ellipse's 512 vertices are collinear, which trips
// isSelfIntersecting's collinear-overlap rule. Before the convexity
// short-circuit that made measure() report "self-intersecting, area unknown"
// for a shape whose area is plainly zero -- a stated reason that is false,
// which the header on isSelfIntersecting calls worse than stating none.
TEST_CASE("a degenerate ellipse measures zero rather than self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry flat =
        core::EllipseGeometry{core::PointF{0.0, 25.0}, core::PointF{100.0, 25.0}};
    const core::ShapeMetrics metrics = core::measure(flat, core::PixelSize{});

    REQUIRE_FALSE(metrics.selfIntersecting);
    REQUIRE(metrics.closed);
    REQUIRE(metrics.area.has_value());
    REQUIRE(std::abs(*metrics.area) < 1e-9);
}

TEST_CASE("an ellipse is never reported as self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry shape =
        core::EllipseGeometry{core::PointF{0.0, 0.0}, core::PointF{100.0, 50.0}};
    const core::ShapeMetrics metrics = core::measure(shape, core::PixelSize{});

    REQUIRE_FALSE(metrics.selfIntersecting);
    REQUIRE(metrics.area.has_value());
}

// The inscribed polygon is the definition of the shape, so this is the exact
// expected value, not an approximation of one. The second assertion documents
// the relationship design spec 3.2 claims and fails if kEllipseOutlineVertices
// changes without that spec changing.
TEST_CASE("an ellipse measures its inscribed polygon's area",
          "[core][AnnotationMeasurement]")
{
    const double a = 50.0;
    const double b = 25.0;
    const core::AnnotationGeometry shape =
        core::EllipseGeometry{core::PointF{0.0, 0.0}, core::PointF{2.0 * a, 2.0 * b}};
    const core::ShapeMetrics metrics = core::measure(shape, core::PixelSize{});

    REQUIRE(metrics.area.has_value());

    const double n = static_cast<double>(core::kEllipseOutlineVertices);
    const double exactPolygonArea = 0.5 * n * a * b * std::sin(2.0 * 3.14159265358979323846 / n);
    REQUIRE(std::abs(*metrics.area - exactPolygonArea) < 1e-6);

    const double trueArea = 3.14159265358979323846 * a * b;
    REQUIRE(std::abs(*metrics.area - trueArea) / trueArea < 0.0001);
}

TEST_CASE("a circle measures pi r squared within the documented tolerance",
          "[core][AnnotationMeasurement]")
{
    const double r = 40.0;
    const core::AnnotationGeometry circle =
        core::EllipseGeometry{core::PointF{0.0, 0.0}, core::PointF{2.0 * r, 2.0 * r}};
    const core::ShapeMetrics metrics = core::measure(circle, core::PixelSize{});

    REQUIRE(metrics.area.has_value());
    const double trueArea = 3.14159265358979323846 * r * r;
    REQUIRE(std::abs(*metrics.area - trueArea) / trueArea < 0.0001);
}
```

- [ ] **Step 2: Run the tests to verify the degenerate case fails**

Run: `cmake --build build/build --config Release` then
`build/build/Release/slideio-viewer-core-tests.exe "[AnnotationMeasurement]"`

Expected: FAIL. "a degenerate ellipse measures zero rather than self-intersecting" fails on `REQUIRE_FALSE(metrics.selfIntersecting)` — the collinear-overlap rule reports true and the area is absent. The other three cases pass already, which is correct and expected: the ellipse's measurement is right after Task 1, and only the degenerate case and the cost are wrong.

- [ ] **Step 3: Add the short-circuit**

In `src/core/src/AnnotationMeasurement.cpp`, replace the body of `measure()`:

```cpp
ShapeMetrics measure(const AnnotationGeometry& geometry, PixelSize pixelSize)
{
    // A shape that cannot self-intersect whatever its values does not need the
    // O(n^2) test. An ellipse carries kEllipseOutlineVertices points, so asking
    // costs about 131000 segment-pair comparisons to produce a constant -- and
    // on a degenerate ellipse the collinear-overlap rule would answer TRUE,
    // withholding an area that is plainly zero.
    if (isConvexByConstruction(geometry)) {
        return measureKnown(geometry, pixelSize, false);
    }
    // The expensive question, asked once, then delegated. An open path never
    // needs it: its area is absent either way, and open freehand traces carry
    // the most vertices by far.
    const Outline shape = outline(geometry);
    // Tested UNSCALED. Self-intersection is a property of the shape: positive
    // scaling cannot create or remove a crossing, and degenerate scaling only
    // destroys the information. A slide with one malformed resolution axis
    // would otherwise collapse every annotation onto a line, trip the
    // collinear-overlap rule, and report "self-intersecting" as the reason an
    // area is missing -- a stated reason that is false, which is worse than
    // stating none.
    const bool crosses = shape.closed && isSelfIntersecting(shape);
    return measureKnown(geometry, pixelSize, crosses);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`build/build/Release/slideio-viewer-core-tests.exe "[AnnotationMeasurement]"`

Expected: PASS, all cases.

- [ ] **Step 5: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass. A rectangle now also skips the self-intersection test; its four corners could never cross, so no existing expectation changes.

- [ ] **Step 6: Commit**

```bash
git add src/core/src/AnnotationMeasurement.cpp tests/core/AnnotationMeasurementTest.cpp
git commit -m "Skip the self-intersection test for a shape that cannot cross"
```

---

## Task 3: `applyDragConstraints` — the Shift and Alt modifiers

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`
- Modify: `src/ui/src/AnnotationInteraction.cpp`
- Test: `tests/ui/AnnotationInteractionTest.cpp`

**Interfaces:**
- Consumes: `core::RectF { double x, y, width, height; }` and `core::PointF { double x, y; }`.
- Produces: `[[nodiscard]] core::RectF applyDragConstraints(core::RectF box, core::PointF anchor, Qt::KeyboardModifiers mods);`

**Semantics, fixed here so Task 4 does not have to decide them.** `box` is the normalised bounding box of the drag, so `anchor` — the press point — is one of its four corners. Shift squares the box about that corner, taking the **larger** extent so the shape never shrinks under the cursor, and extending in the direction the drag went. Alt re-centres the box on the anchor and **doubles** both extents, so the drag distance becomes a radius rather than a diameter. Applied in that order, Shift then Alt, so the pair yields a circle centred on the press. Any other modifier is ignored.

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
TEST_CASE("no modifier leaves the drag box untouched", "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result = applyDragConstraints(box, core::PointF{10.0, 20.0}, Qt::NoModifier);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == 20.0);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 40.0);
}

// Review Focus 3: a modifier this function does not implement must not fall
// into a constraint branch.
TEST_CASE("an unrelated modifier leaves the drag box untouched",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    for (const Qt::KeyboardModifiers mods :
         {Qt::KeyboardModifiers(Qt::ControlModifier), Qt::KeyboardModifiers(Qt::MetaModifier)}) {
        const core::RectF result = applyDragConstraints(box, core::PointF{10.0, 20.0}, mods);
        REQUIRE(result.width == 100.0);
        REQUIRE(result.height == 40.0);
    }
}

// The anchor is the top-left corner, so the square grows right and down.
TEST_CASE("shift squares the box about an anchor dragged down-right",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{10.0, 20.0}, Qt::ShiftModifier);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == 20.0);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
}

// The anchor is the bottom-right corner, so the square grows left and up and
// the box origin moves.
TEST_CASE("shift squares the box about an anchor dragged up-left",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{110.0, 60.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == -40.0);
}

// The anchor is the bottom-left corner: the square grows right and up.
TEST_CASE("shift squares the box about an anchor dragged up-right",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{10.0, 60.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == -40.0);
}

// The anchor is the top-right corner: the square grows left and down.
TEST_CASE("shift squares the box about an anchor dragged down-left",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{110.0, 20.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == 20.0);
}

TEST_CASE("shift takes the larger extent so the shape never shrinks",
          "[ui][AnnotationInteraction]")
{
    const core::RectF tall{0.0, 0.0, 10.0, 90.0};
    const core::RectF result =
        applyDragConstraints(tall, core::PointF{0.0, 0.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 90.0);
    REQUIRE(result.height == 90.0);
}

// Alt makes the press point the centre and the drag a radius, so both extents
// double.
TEST_CASE("alt re-centres the box on the anchor and doubles it",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{10.0, 20.0}, Qt::AltModifier);
    REQUIRE(result.width == 200.0);
    REQUIRE(result.height == 80.0);
    REQUIRE(result.x == -90.0);
    REQUIRE(result.y == -20.0);
    REQUIRE(result.x + result.width * 0.5 == 10.0);
    REQUIRE(result.y + result.height * 0.5 == 20.0);
}

TEST_CASE("shift and alt together give a square centred on the anchor",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result = applyDragConstraints(
        box, core::PointF{10.0, 20.0}, Qt::ShiftModifier | Qt::AltModifier);
    REQUIRE(result.width == 200.0);
    REQUIRE(result.height == 200.0);
    REQUIRE(result.x + result.width * 0.5 == 10.0);
    REQUIRE(result.y + result.height * 0.5 == 20.0);
}

// Review Focus 4: a press and release without moving. No NaN, no inverted box.
TEST_CASE("a zero-length drag stays zero under either modifier",
          "[ui][AnnotationInteraction]")
{
    const core::RectF empty{50.0, 50.0, 0.0, 0.0};
    for (const Qt::KeyboardModifiers mods :
         {Qt::KeyboardModifiers(Qt::ShiftModifier), Qt::KeyboardModifiers(Qt::AltModifier),
          Qt::KeyboardModifiers(Qt::ShiftModifier | Qt::AltModifier)}) {
        const core::RectF result = applyDragConstraints(empty, core::PointF{50.0, 50.0}, mods);
        REQUIRE(result.width == 0.0);
        REQUIRE(result.height == 0.0);
        REQUIRE(result.x == 50.0);
        REQUIRE(result.y == 50.0);
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `applyDragConstraints` as undeclared.

- [ ] **Step 3: Declare the function**

In `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`, after `withMinimumExtent`:

```cpp
/// `box` constrained by the modifiers held during the drag that produced it.
///
/// `box` is the normalised bounding box of the drag, so `anchor` -- the press
/// point -- is one of its four corners, and which corner is what tells this
/// function which way the drag went.
///
///   * Shift squares the box about `anchor`, taking the LARGER of the two
///     extents so the shape never shrinks away from the cursor.
///   * Alt re-centres the box on `anchor` and doubles both extents, making the
///     press point the centre and the drag a radius rather than a diameter.
///   * Both compose, Shift first, so the pair gives a circle centred on the
///     press.
///   * Any other modifier is ignored.
///
/// Shape-agnostic on purpose: it constrains a box and knows nothing about what
/// will be built from it, which is what lets the rectangle tool use it too
/// (FR-ANN-01's "Shift constrains to square").
[[nodiscard]] core::RectF applyDragConstraints(core::RectF box, core::PointF anchor,
                                               Qt::KeyboardModifiers mods);
```

- [ ] **Step 4: Implement it**

In `src/ui/src/AnnotationInteraction.cpp`, after `withMinimumExtent`:

```cpp
core::RectF applyDragConstraints(core::RectF box, core::PointF anchor,
                                 Qt::KeyboardModifiers mods)
{
    if (mods.testFlag(Qt::ShiftModifier)) {
        // Which corner the anchor sits on says which way the drag went. The
        // midpoint comparison rather than an equality test on doubles: the box
        // came from the anchor by arithmetic, so exact equality is not
        // guaranteed, and a zero-extent box answers "extends positive" either
        // way, which is harmless because there is nothing to extend.
        const bool extendsRight = anchor.x <= box.x + box.width * 0.5;
        const bool extendsDown = anchor.y <= box.y + box.height * 0.5;
        const double side = std::max(box.width, box.height);
        box.x = extendsRight ? anchor.x : anchor.x - side;
        box.y = extendsDown ? anchor.y : anchor.y - side;
        box.width = side;
        box.height = side;
    }
    if (mods.testFlag(Qt::AltModifier)) {
        box.x = anchor.x - box.width;
        box.y = anchor.y - box.height;
        box.width *= 2.0;
        box.height *= 2.0;
    }
    return box;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`

Expected: PASS. **Do not run `slideio-viewer-ui-tests.exe` directly** — it pops a modal Windows error box that blocks until a human dismisses it.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        src/ui/src/AnnotationInteraction.cpp \
        tests/ui/AnnotationInteractionTest.cpp
git commit -m "Constrain a drag box with the shift and alt modifiers"
```

---

## Task 4: The ellipse tool, its action, and the preview

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` (the `AnnotationTool` enum)
- Modify: `src/ui/src/ViewportWidget.cpp` (commit path ~4089-4120, preview ~3131-3139, `Impl` ~1527)
- Modify: `src/ui/src/MainWindow.cpp` (actions ~344-373, `selectDrawingTool` ~608-614, checked sync ~639-641)
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-10-16-annotation-ellipse-design.md`
- Test: `tests/ui/AnnotationInteractionTest.cpp`

**Interfaces:**
- Consumes: `core::EllipseGeometry` from Task 1; `applyDragConstraints(core::RectF, core::PointF, Qt::KeyboardModifiers)` from Task 3.
- Produces: `AnnotationTool::Ellipse`.

**How FR-ANN-01 gets closed here.** `pendingDragBox()` sits in the single-drag path that the rectangle and the ellipse share, so routing the commit and the preview through it gives the **rectangle** Shift-square and Alt-from-centre at the same time, with no rectangle-specific code. That is spec §6.3, and manual check Y7 is what confirms it.

**What is and is not testable here.** Only the enum's effect on `isNPointTool` and `resolveDragOwner` is reachable from a test — no test binary creates a `QApplication`, so the action, the preview and the commit path cannot be. Those are covered by manual group Y in the spec, which this task records as pending.

**An accepted limitation to state in the commit.** The modifiers are read from the mouse events, so pressing Shift *without moving the mouse* mid-drag does not reshape the preview until the next move event. Every gesture in `documents/02-user-interface-design.md` §6.6 is phrased as holding the modifier *while dragging*, so the common path is covered; handling bare key events during a drag is not in this plan.

- [ ] **Step 1: Write the failing routing tests**

Append to `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
// The ellipse is a single-drag tool, like the rectangle. isNPointTool
// returning false is the whole of what routes it down that path rather than
// the multi-click one.
TEST_CASE("the ellipse tool is not an n-point tool", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Ellipse));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Rectangle));
    REQUIRE(isNPointTool(AnnotationTool::Polygon));
    REQUIRE(isNPointTool(AnnotationTool::Freehand));
}

TEST_CASE("a left drag with the ellipse tool belongs to the tool",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false, AnnotationTool::Ellipse,
                             false) == DragOwner::Tool);
    // Shift is the circle constraint, not a selection modifier, so it must not
    // divert the drag away from the tool.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::ShiftModifier, false, AnnotationTool::Ellipse,
                             true) == DragOwner::Tool);
    // Space and the middle button still pan, whatever the tool.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true, AnnotationTool::Ellipse,
                             false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false, AnnotationTool::Ellipse,
                             false) == DragOwner::Pan);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `AnnotationTool::Ellipse` as not a member.

- [ ] **Step 3: Add the enum value**

In `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`:

```cpp
enum class AnnotationTool
{
    Pan,
    Rectangle,
    Ellipse,
    Polygon,
    Freehand,
};
```

`isNPointTool` needs no change — it tests for `Polygon` and `Freehand` by name, so `Ellipse` is already false. The test above pins that rather than leaving it to inspection.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`

Expected: PASS.

- [ ] **Step 5: Store the drag modifiers on the widget**

In `src/ui/src/ViewportWidget.cpp`, in `Impl` beside `drawAnchorSlide` and `drawCurrentSlide` (~line 1527):

```cpp
    // Read from the mouse events rather than queried globally, so the preview
    // and the committed shape are constrained by the same value.
    Qt::KeyboardModifiers drawModifiers = Qt::NoModifier;
```

Set it in `mousePressEvent` beside `m_impl->drawAnchorSlide = ...`, in `mouseMoveEvent` beside the `drawCurrentSlide` update (~line 4229), and in `mouseReleaseEvent` beside the one at ~line 4096, each as:

```cpp
    m_impl->drawModifiers = event->modifiers();
```

Add a private helper to `Impl`, so the preview and the commit path can never disagree about the box:

```cpp
    /// The box the pending drag describes, with its modifiers applied. One
    /// source for the preview and the commit: computing it twice is how a
    /// preview comes to lie about what the release will produce.
    core::RectF pendingDragBox() const
    {
        const core::RectF raw =
            core::boundingBox(core::RectangleGeometry{drawAnchorSlide, drawCurrentSlide});
        return applyDragConstraints(raw, drawAnchorSlide, drawModifiers);
    }
```

- [ ] **Step 6: Build the right shape on release**

In `src/ui/src/ViewportWidget.cpp`, replace the `rawBox` line (~4099) and the geometry construction inside the `dragExceedsMinimumSize` branch (~4112-4116):

```cpp
        const core::RectF rawBox = m_impl->pendingDragBox();
```

and

```cpp
            const core::RectF box = withMinimumExtent(rawBox, screenToleranceToSlide(1.0, scale));
            const core::PointF topLeft{box.x, box.y};
            const core::PointF bottomRight{box.x + box.width, box.y + box.height};
            const core::AnnotationGeometry geometry =
                m_impl->activeTool == AnnotationTool::Ellipse
                    ? core::AnnotationGeometry{core::EllipseGeometry{topLeft, bottomRight}}
                    : core::AnnotationGeometry{core::RectangleGeometry{topLeft, bottomRight}};
```

- [ ] **Step 7: Draw the right preview**

In `src/ui/src/ViewportWidget.cpp`, replace the pending-rectangle preview block (~3131-3139):

```cpp
    // The rectangle or ellipse being dragged right now is not in the model yet.
    if (m_impl->drawing) {
        const core::RectF box = m_impl->pendingDragBox();
        const core::PointF topLeft{box.x, box.y};
        const core::PointF bottomRight{box.x + box.width, box.y + box.height};
        painter.setPen(QPen(QColor(0xE6, 0x7E, 0x22), 2.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        if (m_impl->activeTool == AnnotationTool::Ellipse) {
            // Stroked from core::outline rather than drawEllipse, so the preview
            // is the same 512-gon the release will commit -- design spec 6.1.
            const core::Outline shape =
                core::outline(core::EllipseGeometry{topLeft, bottomRight});
            QPainterPath path;
            bool first = true;
            for (const core::PointF& vertex : shape.points) {
                const QPointF screen = toScreen(vertex);
                if (first) {
                    path.moveTo(screen);
                    first = false;
                } else {
                    path.lineTo(screen);
                }
            }
            path.closeSubpath();
            painter.drawPath(path);
        } else {
            painter.drawRect(QRectF(toScreen(topLeft), toScreen(bottomRight)).normalized());
        }
    }
```

- [ ] **Step 8: Add the toolbar action and its shortcut**

`rectangleToolAction` is touched at **twelve** sites in `src/ui/src/MainWindow.cpp`. The ellipse needs a line at each; four of them are enable/disable and registration sites that are easy to miss and whose omission leaves a tool that is permanently greyed out or whose shortcut never fires. Add, in order:

```cpp
// 1. Member declaration, beside line 184:
    QAction* ellipseToolAction = nullptr;

// 2. Construction, after the rectangleToolAction block at ~350:
        ellipseToolAction = new QAction("&Ellipse", owner);
        ellipseToolAction->setCheckable(true);
        ellipseToolAction->setShortcut(QKeySequence("E"));
        ellipseToolAction->setStatusTip(
            "Draw an elliptical annotation by dragging; hold Shift for a circle");

// 3. Shortcut scope, beside line 371 (the comment above it explains why these
//    bare-letter shortcuts must be viewport-scoped -- "E" joins "V", "R", "P"
//    and "F" in being stolen from every text field otherwise):
        ellipseToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);

// 4. Exclusive group, beside line 378:
        toolGroup->addAction(ellipseToolAction);

// 5. Initially disabled until a slide is open, beside line 385:
        ellipseToolAction->setEnabled(false);

// 6. Tools menu, beside line 474:
        toolsMenu->addAction(ellipseToolAction);

// 7. Viewport action registration -- without this the shortcut never fires,
//    because of the WidgetWithChildrenShortcut scope set in 3. Beside line 582:
        viewportWidget->addAction(ellipseToolAction);

// 8. Triggered wiring, beside line 607:
        QObject::connect(ellipseToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Ellipse);
        });

// 9. Checked-state sync, beside line 639:
                             ellipseToolAction->setChecked(tool == AnnotationTool::Ellipse);

// 10. Disabled on slide close, beside line 922:
            ellipseToolAction->setEnabled(false);

// 11. Availability gate, in applyAnnotationAvailability beside line 1652:
    ellipseToolAction->setEnabled(available);
```

That is eleven edits across the twelve sites — site 349's `setShortcut` is folded into the construction block at 2.

**Toolbar placement:** the tool actions reach the toolbar through the Tools menu and the viewport, not a separate `QToolBar` population list, so sites 6 and 7 are the whole of it. Grep `rectangleToolAction` after the change and confirm `ellipseToolAction` appears at the same count of sites.

- [ ] **Step 9: Build, run the whole suite, and install**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

Then close any running viewer and install:

Run: `cmake --install build/build --config Release`

Verify by artifact, not exit code — a running viewer makes the install a silent no-op that still exits 0:

Run: `ls -l build/install/release/bin/slideio-viewer.exe build/build/Release/slideio-viewer.exe`

Expected: the installed binary's timestamp is not older than the built one. If it is, the viewer was running; close it and install again.

- [ ] **Step 10: Record the documentation**

In `CLAUDE.md`, in the **Not built yet** paragraph, remove "the ellipse, circle" from the shape list, leaving "vertex editing and the point, arrow and text shapes". In the **Key Design Decisions** list, after the `std::variant` entry, add:

```markdown
- **An ellipse is its inscribed 512-gon.** `EllipseGeometry` stores a bounding
  box; `outline()` expands it into `kEllipseOutlineVertices` points, and the
  painter, `hitTest` and `measure` all read that one outline, so they cannot
  disagree. The count is fixed and never adaptive: an adaptive one would make a
  stored annotation's measured area depend on the zoom it was measured at. The
  area therefore reads about 0.0025% under the true ellipse — deterministic,
  documented, and far inside the scanner's own calibration uncertainty. See
  `docs/superpowers/specs/2026-10-16-annotation-ellipse-design.md` §3.
```

In `docs/superpowers/specs/2026-10-16-annotation-ellipse-design.md`, under §7.3, add one line above the table:

```markdown
**Not yet run.** Group Y is the list to work through against the installed
build; results are recorded here afterwards, as groups U, V, W and X were.
```

- [ ] **Step 11: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        src/ui/src/ViewportWidget.cpp \
        src/ui/src/MainWindow.cpp \
        tests/ui/AnnotationInteractionTest.cpp \
        CLAUDE.md \
        docs/superpowers/specs/2026-10-16-annotation-ellipse-design.md
git commit -m "Add the ellipse drawing tool and its modifiers"
```

---

## After the plan

Group Y in the spec is fifteen manual checks that no suite can reach. Run them against the installed build and record the results in the spec, as groups U, V, W and X were recorded. **Y14 needs a viewer binary built before this work** — keep one, or check out `3ba835a`, build it to a scratch prefix, and show it a file containing an ellipse: it must refuse the file naming `'ellipse'` and leave it byte-identical.
