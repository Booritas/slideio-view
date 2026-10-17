# Point Marker and Arrow Annotations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `PointGeometry` and `ArrowGeometry` to `core::AnnotationGeometry`, with their tools, their screen-space glyphs and their JSON form, completing FR-ANN-05 and FR-ANN-06.

**Architecture:** Two new alternatives are appended to the `std::variant`, which fires twelve compile-guarded dispatch sites plus one unguarded string chain in the parser. Screen-space sizing — the marker's disc and the arrow's head — is computed by a new Qt-free core function `screenDecoration`, so the only real arithmetic in the round sits where a test can reach it; the painter gains one shape-blind "draw a glyph of this kind, this many pixels across" branch. The two tools reuse the existing press/drag/release path, differing only in what they commit.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+ with Conan 2, Catch2 v3, four ctest suites.

**Spec:** `docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md` (commit `5747572`)

## Global Constraints

- **Core is Qt-free.** `slideio-viewer-core` links no Qt target. Nothing added under `src/core/` may include a Qt header or use a Qt type.
- **Strict downward layering:** core → app → infra → ui. Core cannot name anything in `ui`, which is why the disc radius is a parameter (spec §3.3).
- **No test binary creates a `QApplication`**, so no test may construct a widget. Pure functions over plain data are the only testable surface.
- **Never run `slideio-viewer-app-tests`, `-infra-tests` or `-ui-tests` as a bare `.exe`** — they find their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property and a direct run pops a modal Windows error box. Only `slideio-viewer-core-tests.exe` is safe to run directly, at `build/build/tests/Release/slideio-viewer-core-tests.exe`.
- **`schemaVersion` stays 1.** `core::kCurrentSchemaVersion` is not touched by this plan.
- **Every `.cpp` and every test file is listed explicitly in CMake** (`src/core/CMakeLists.txt`, `tests/CMakeLists.txt`). CMake's generate step requires every listed source to exist, so a new `.cpp` must be created on disk **before** it is added to a source list and the project reconfigured.
- **Append to the variant, never insert.** `AnnotationGeometry` default-constructs to its first alternative, and `AnnotationSerialization.cpp` relies on that (`Annotation annotation("", RectangleGeometry{...})`). `PointGeometry` and `ArrowGeometry` go at the end.
- **An arrow's endpoint pair is ordered and must never be normalised.** `tail` and `head` are not corners. Normalising them silently reverses arrows.
- Coding conventions: 4-space indent, 120-char lines, Allman braces for functions, K&R for control flow, `kPascalCase` constants, `m_camelCase` members, `#pragma once`.
- Workflow: direct commits to `main` (unprotected, no PR, no feature branch). All AI-generated code must be human-reviewed before it is merged into anyone else's work.

## Plan-level rulings against the spec

Two points the spec leaves under-determined, settled here so no task has to guess:

1. **`kArrowHeadScreenPixels` and `kArrowHeadMaxShaftFraction` live in `core`**, declared in `AnnotationDecoration.h` beside the function that uses them — not in `ui`. The arithmetic that reads them is in core by §3.4's own argument ("under the rejected painter-special-case approach it would have lived in the only layer no test can reach"), and core cannot read a ui constant. This is the same reasoning §3.3 applies to the disc radius, which reaches the opposite conclusion only because its ui constant already exists and has other callers.

2. **`commitDrawDrag` has no read-only gate today, and this plan adds one.** Spec §4.4 says read-only slides "refuse creation through the existing `annotationsEditable()` gate, as every other tool does". That is true of `commitPendingShape` (polygon, freehand) but **not** of `commitDrawDrag` (rectangle, ellipse), which calls `annotations->add()` with no check. The tool actions are disabled on a read-only slide, so this is normally unreachable — but the active tool is deliberately not reset when a slide is opened, so selecting a drawing tool on an editable slide and then opening a read-only one leaves it live. AB19 requires the refusal, so Task 4 adds the gate. It incidentally fixes the same latent defect for the rectangle and ellipse tools.

## Review Focus

Five input classes the spec implies but that no task's own narrative covers. Each has its test added to the task that owns the code.

1. **An arrow whose tail and head are the same point.** The tool refuses to create one, but the parser accepts it from disk. `hitTest` must still find it at that point, `measure` must report length 0 with no area, and `screenDecoration` must give a head of size 0 rather than a NaN direction. → Tasks 1 and 2.
2. **A point or arrow carrying a non-finite coordinate must refuse to serialize.** Writing it would produce a file the parser rejects, breaking "never write a file we cannot read back". → Task 1.
3. **An arrow object in JSON missing `tail` or `head`** must be refused with a message naming the field, not silently defaulted to the origin — which would move someone's annotation to the slide's corner. → Task 1.
4. **`arrowExceedsMinimumLength` at a non-positive viewport scale must be false**, and a purely diagonal drag of exactly the threshold length must be accepted — the case `dragExceedsMinimumSize` gets wrong and the whole reason this predicate exists. → Task 3.
5. **`screenDecoration` must answer `nullopt` for a rectangle, an ellipse and a polygon**, so the painter never draws a glyph on a shape that has none. → Task 2.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/core/include/slideio/viewer/core/AnnotationGeometry.h` | The two new structs, two new `AnnotationType` values, the widened variant | 1 |
| `src/core/src/AnnotationGeometry.cpp` | Six dispatch overloads per new type | 1 |
| `src/core/src/AnnotationSerialization.cpp` | Write (`shapeToJson`, `isRepresentableShape`, `geometryTypeName`) and read (`readGeometry`, two new readers) | 1 |
| `src/core/src/BoxResize.cpp` | `replaceBox` answers `nullopt` for both — neither is box-shaped | 1 |
| `src/ui/src/AnnotationShapeName.cpp` | `ShapeNameVisitor` and the `AnnotationType` switch gain both | 1 |
| `src/core/include/slideio/viewer/core/AnnotationDecoration.h` (new) | `ScreenDecoration`, `screenDecoration`, the two arrowhead constants | 2 |
| `src/core/src/AnnotationDecoration.cpp` (new) | Its overload set | 2 |
| `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` | `AnnotationTool::Point`, `::Arrow`, `arrowExceedsMinimumLength` | 3 |
| `src/ui/src/AnnotationInteraction.cpp` | `arrowExceedsMinimumLength` | 3 |
| `src/ui/src/ViewportWidget.cpp` | Commit, glyph painting, selection ring, arrow preview, read-only gate | 4 |
| `src/ui/src/MainWindow.cpp` | The two tool actions, their menu entries, shortcuts and enablement | 5 |
| `tests/core/AnnotationGeometryTest.cpp` | The six dispatches for both types | 1 |
| `tests/core/AnnotationMeasurementTest.cpp` | `measure` for both, proving no new code is needed | 1 |
| `tests/core/AnnotationSerializationTest.cpp` | Round-trips — the only thing forcing `readGeometry`'s branches | 1 |
| `tests/core/AnnotationDecorationTest.cpp` (new) | `screenDecoration` | 2 |
| `tests/ui/AnnotationInteractionTest.cpp` | `arrowExceedsMinimumLength`, the two new tool values | 3 |
| `tests/ui/AnnotationShapeNameTest.cpp` (new) | `shapeName` and `shapeTypeName` for both new types | 3 |

`tests/ui/AnnotationListPolicyTest.cpp` has a local `testTypeName` switch over `AnnotationType` with a fallback return. It needs no change: the tests in that file exercise only the existing types, and `tests/` is not built with `set_project_warnings`. Leave it alone.

---

### Task 1: The two geometry types, across every dispatch site

Adding to the variant is atomic: the twelve compile-guarded sites all break at once, so the build is green only when every one of them answers. The thirteenth — `readGeometry` — is a string chain the compiler says nothing about, and it lands in the same task because a type whose writer exists and whose reader does not produces files the application refuses to open.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`
- Modify: `src/core/src/AnnotationGeometry.cpp`
- Modify: `src/core/src/AnnotationSerialization.cpp`
- Modify: `src/core/src/BoxResize.cpp`
- Modify: `src/ui/src/AnnotationShapeName.cpp`
- Test: `tests/core/AnnotationGeometryTest.cpp`
- Test: `tests/core/AnnotationMeasurementTest.cpp`
- Test: `tests/core/AnnotationSerializationTest.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `struct core::PointGeometry { PointF position; };`
  - `struct core::ArrowGeometry { PointF tail; PointF head; };`
  - `core::AnnotationType::Point`, `core::AnnotationType::Arrow`
  - `using core::AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry, EllipseGeometry, PointGeometry, ArrowGeometry>;`
  - The geometry JSON type names `"point"` and `"arrow"`.

- [ ] **Step 1: Write the failing geometry tests**

Append to `tests/core/AnnotationGeometryTest.cpp`. Add the two helpers next to the existing `rect` and `ellipseGeom` inside the file's anonymous namespace:

```cpp
AnnotationGeometry pointGeom(double x, double y)
{
    return PointGeometry{PointF{x, y}};
}

AnnotationGeometry arrowGeom(double tailX, double tailY, double headX, double headY)
{
    return ArrowGeometry{PointF{tailX, tailY}, PointF{headX, headY}};
}
```

Then append these test cases at the end of the file:

```cpp
TEST_CASE("a point's bounding box has zero extent at its position",
          "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(pointGeom(120.0, 340.0));
    REQUIRE(box.x == 120.0);
    REQUIRE(box.y == 340.0);
    REQUIRE(box.width == 0.0);
    REQUIRE(box.height == 0.0);
}

// The pair is ORDERED -- head and tail are not corners -- but the bounding box
// is a box, so it normalises like any other.
TEST_CASE("an arrow's bounding box normalises its endpoints",
          "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(arrowGeom(110.0, 70.0, 10.0, 20.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("a point is hit within the tolerance of its position and not beyond",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry marker = pointGeom(100.0, 100.0);
    REQUIRE(hitTest(marker, PointF{100.0, 100.0}, 0.0));
    REQUIRE(hitTest(marker, PointF{103.0, 104.0}, 5.0));
    REQUIRE_FALSE(hitTest(marker, PointF{103.0, 104.0}, 4.9));
}

TEST_CASE("an arrow is hit along its shaft and not off it",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry arrow = arrowGeom(0.0, 0.0, 100.0, 0.0);
    REQUIRE(hitTest(arrow, PointF{50.0, 0.0}, 0.0));
    REQUIRE(hitTest(arrow, PointF{50.0, 2.0}, 2.0));
    REQUIRE_FALSE(hitTest(arrow, PointF{50.0, 10.0}, 2.0));
    // Past the head, not on the infinite line through it.
    REQUIRE_FALSE(hitTest(arrow, PointF{140.0, 0.0}, 2.0));
}

// Review Focus 1: the tool refuses to create this, but the parser accepts it
// from disk, so every dispatch has to answer for it.
TEST_CASE("a zero-length arrow is hit at its own point",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry arrow = arrowGeom(40.0, 40.0, 40.0, 40.0);
    REQUIRE(hitTest(arrow, PointF{40.0, 40.0}, 0.0));
    REQUIRE_FALSE(hitTest(arrow, PointF{60.0, 40.0}, 1.0));
}

TEST_CASE("typeOf names the two new shapes", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(pointGeom(1.0, 2.0)) == AnnotationType::Point);
    REQUIRE(typeOf(arrowGeom(1.0, 2.0, 3.0, 4.0)) == AnnotationType::Arrow);
}

TEST_CASE("a point's outline is one open point", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(pointGeom(7.0, 9.0));
    REQUIRE(shape.points.size() == 1);
    REQUIRE(shape.points[0].x == 7.0);
    REQUIRE(shape.points[0].y == 9.0);
    REQUIRE_FALSE(shape.closed);
}

// Tail first, head second, in the stored order. The painter reads the shaft
// direction from these two points, so reversing them would reverse the head.
TEST_CASE("an arrow's outline is tail then head, open",
          "[core][AnnotationGeometry]")
{
    const Outline shape = outline(arrowGeom(10.0, 20.0, 90.0, 60.0));
    REQUIRE(shape.points.size() == 2);
    REQUIRE(shape.points[0].x == 10.0);
    REQUIRE(shape.points[0].y == 20.0);
    REQUIRE(shape.points[1].x == 90.0);
    REQUIRE(shape.points[1].y == 60.0);
    REQUIRE_FALSE(shape.closed);
}

TEST_CASE("translating a point moves its position", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved = translated(pointGeom(10.0, 20.0), 5.0, -3.0);
    REQUIRE(typeOf(moved) == AnnotationType::Point);
    const PointGeometry& marker = std::get<PointGeometry>(moved);
    REQUIRE(marker.position.x == 15.0);
    REQUIRE(marker.position.y == 17.0);
}

// Both endpoints move by the same delta, and the ORDER survives: a translated
// arrow that came back reversed would point the wrong way after every drag.
TEST_CASE("translating an arrow moves both endpoints and keeps their order",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved = translated(arrowGeom(10.0, 20.0, 90.0, 60.0), 5.0, -3.0);
    REQUIRE(typeOf(moved) == AnnotationType::Arrow);
    const ArrowGeometry& arrow = std::get<ArrowGeometry>(moved);
    REQUIRE(arrow.tail.x == 15.0);
    REQUIRE(arrow.tail.y == 17.0);
    REQUIRE(arrow.head.x == 95.0);
    REQUIRE(arrow.head.y == 57.0);
}

// Neither has an area at all, so neither type can promise one. The predicate
// is about the TYPE, not the instance -- see the header.
TEST_CASE("neither a point nor an arrow has a well-defined area",
          "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hasWellDefinedArea(pointGeom(1.0, 2.0)));
    REQUIRE_FALSE(hasWellDefinedArea(arrowGeom(1.0, 2.0, 3.0, 4.0)));
}
```

- [ ] **Step 2: Run the build to verify the tests fail**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests`

Expected: FAIL at compile time, with errors naming `PointGeometry` and `ArrowGeometry` as undeclared identifiers (MSVC: `error C2065: 'PointGeometry': undeclared identifier`), and `AnnotationType::Point` / `::Arrow` as not members of the enum. That compile failure is the RED: the types do not exist yet.

- [ ] **Step 3: Declare the two types in the core header**

In `src/core/include/slideio/viewer/core/AnnotationGeometry.h`, add the two structs after `EllipseGeometry` and before the `AnnotationType` enum:

```cpp
/// A single marked position (FR-ANN-05). Not a zero-extent rectangle: a box
/// promises two corners that could differ, and normalisation, withBoxReplaced,
/// withMinimumExtent and the resize handles would all apply to it, each
/// meaninglessly. A position is what a marker is.
///
/// It has no slide-space extent at all, so what the user sees is sized in
/// screen pixels by core::screenDecoration rather than by outline().
struct PointGeometry
{
    PointF position;
};

/// A directed arrow from `tail` to `head` (FR-ANN-06).
///
/// Not a two-vertex PolygonGeometry: AnnotationType is derived from the
/// geometry and never stored beside it, so typeOf() must be able to tell an
/// arrow from a two-point freehand line by its geometry alone, and those two
/// would be indistinguishable.
///
/// The pair is ORDERED, and the order is the meaning -- the head is the end
/// that carries the glyph. Never normalise it the way a rectangle's corners
/// are normalised: that silently reverses arrows. boundingBox() normalises the
/// BOX it derives, which is a different thing and is fine.
struct ArrowGeometry
{
    PointF tail;
    PointF head;
};
```

In the same header, add the two enumerators at the end of `AnnotationType`:

```cpp
enum class AnnotationType
{
    Rectangle,
    Ellipse,
    Polygon,
    Polyline,
    Point,
    Arrow,
};
```

And widen the variant, appending — never inserting, because the variant default-constructs to its first alternative and `AnnotationSerialization.cpp` relies on that:

```cpp
using AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry, EllipseGeometry,
                                        PointGeometry, ArrowGeometry>;
```

- [ ] **Step 4: Add the six dispatch overloads**

In `src/core/src/AnnotationGeometry.cpp`, append to the **end of the anonymous namespace**, after `bool wellDefinedArea(const PolygonGeometry&)` and before the closing `} // namespace`. Position matters: `hitTestShape` for an arrow calls `distanceToSegment`, which is an ordinary (non-template) function defined earlier in that namespace, so these overloads must come after it.

```cpp
RectF normalized(const PointGeometry& marker)
{
    return RectF{marker.position.x, marker.position.y, 0.0, 0.0};
}

RectF normalized(const ArrowGeometry& arrow)
{
    const double x0 = std::min(arrow.tail.x, arrow.head.x);
    const double y0 = std::min(arrow.tail.y, arrow.head.y);
    const double x1 = std::max(arrow.tail.x, arrow.head.x);
    const double y1 = std::max(arrow.tail.y, arrow.head.y);
    return RectF{x0, y0, x1 - x0, y1 - y0};
}

bool hitTestShape(const PointGeometry& marker, PointF point, double tolerance)
{
    return std::hypot(point.x - marker.position.x, point.y - marker.position.y) <= tolerance;
}

// The shaft, and only the shaft. The head is decoration: it sits at the
// shaft's end and is therefore already within tolerance of it, so hit-testing
// it separately would duplicate an answer this already gives.
//
// distanceToSegment clamps to the segment rather than its infinite line, and
// treats a zero-length segment as its own endpoint -- which is what makes a
// degenerate arrow from disk hit at its point instead of dividing by zero.
bool hitTestShape(const ArrowGeometry& arrow, PointF point, double tolerance)
{
    return distanceToSegment(arrow.tail, arrow.head, point) <= tolerance;
}

AnnotationType typeOfShape(const PointGeometry&)
{
    return AnnotationType::Point;
}

AnnotationType typeOfShape(const ArrowGeometry&)
{
    return AnnotationType::Arrow;
}

// One point, open. measure() reads this and reports length 0 with no area,
// which is the truth about a marker and needs no code of its own.
Outline outlineShape(const PointGeometry& marker)
{
    return Outline{{marker.position}, false};
}

// Tail first, head second: the painter derives the head's direction from these
// two points once it has converted them to screen space, so their order is
// load-bearing.
Outline outlineShape(const ArrowGeometry& arrow)
{
    return Outline{{arrow.tail, arrow.head}, false};
}

AnnotationGeometry translatedShape(const PointGeometry& marker, double dx, double dy)
{
    return PointGeometry{PointF{marker.position.x + dx, marker.position.y + dy}};
}

AnnotationGeometry translatedShape(const ArrowGeometry& arrow, double dx, double dy)
{
    return ArrowGeometry{PointF{arrow.tail.x + dx, arrow.tail.y + dy},
                         PointF{arrow.head.x + dx, arrow.head.y + dy}};
}

// Neither has an area at all. The predicate promises that a TYPE always has a
// well-defined area, and "none" is not a value measure() can stand behind.
bool wellDefinedArea(const PointGeometry&)
{
    return false;
}

bool wellDefinedArea(const ArrowGeometry&)
{
    return false;
}
```

- [ ] **Step 5: Answer at the three remaining compile-guarded sites outside this file**

These will not compile until they do. In `src/core/src/BoxResize.cpp`, add to its anonymous namespace after `replaceBox(const PolygonGeometry&, const RectF&)`:

```cpp
// Neither is defined by a box, so neither is resizable by one. Reached through
// std::visit rather than a get_if chain precisely so these two had to be
// written -- a chain would have let both fall silently through to "not
// resizable", which is the right answer by accident rather than on purpose.
std::optional<AnnotationGeometry> replaceBox(const PointGeometry&, const RectF&)
{
    return std::nullopt;
}

std::optional<AnnotationGeometry> replaceBox(const ArrowGeometry&, const RectF&)
{
    return std::nullopt;
}
```

In `src/ui/src/AnnotationShapeName.cpp`, add two name helpers beside the existing ones:

```cpp
QString pointName()
{
    return QCoreApplication::translate("AnnotationShapeName", "Point");
}

QString arrowName()
{
    return QCoreApplication::translate("AnnotationShapeName", "Arrow");
}
```

add two operators to `ShapeNameVisitor`:

```cpp
    QString operator()(const core::PointGeometry&) const
    {
        return pointName();
    }

    QString operator()(const core::ArrowGeometry&) const
    {
        return arrowName();
    }
```

and two cases to `shapeTypeName`'s switch, before its trailing `return QString();`:

```cpp
    case core::AnnotationType::Point:
        return pointName();
    case core::AnnotationType::Arrow:
        return arrowName();
```

In `src/core/src/AnnotationSerialization.cpp`, add the two type-name constants beside the existing four:

```cpp
constexpr const char* kPointTypeName = "point";
constexpr const char* kArrowTypeName = "arrow";
```

two `isRepresentableShape` overloads after the ellipse one:

```cpp
// Finite coordinates and nothing more. There is no minimum extent to enforce:
// any finite position is a valid marker, and any finite pair is a valid arrow
// -- including a zero-length one, which readArrow accepts, and the two sides
// must agree or this application writes a file it cannot open.
bool isRepresentableShape(const PointGeometry& marker)
{
    return isFinitePoint(marker.position);
}

bool isRepresentableShape(const ArrowGeometry& arrow)
{
    return isFinitePoint(arrow.tail) && isFinitePoint(arrow.head);
}
```

two `shapeToJson` overloads after the ellipse one:

```cpp
json shapeToJson(const PointGeometry& marker)
{
    return json{
        {"type", kPointTypeName},
        {"position", pointToJson(marker.position)},
    };
}

// tail and head, not topLeft and bottomRight: the names say the pair is
// ordered, so a human reading the file can see which end carries the head.
json shapeToJson(const ArrowGeometry& arrow)
{
    return json{
        {"type", kArrowTypeName},
        {"tail", pointToJson(arrow.tail)},
        {"head", pointToJson(arrow.head)},
    };
}
```

and two branches in `geometryTypeName`'s `if constexpr` chain, before the `else` that holds the `static_assert`:

```cpp
        } else if constexpr (std::is_same_v<Shape, PointGeometry>) {
            return kPointTypeName;
        } else if constexpr (std::is_same_v<Shape, ArrowGeometry>) {
            return kArrowTypeName;
```

- [ ] **Step 6: Run the geometry tests to verify they pass**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[core][AnnotationGeometry]"`

Expected: PASS, all assertions in `[core][AnnotationGeometry]` green.

- [ ] **Step 7: Write the failing measurement tests**

Spec §2.3 claims `measure` needs no new code because it is `outline()`-driven. These tests are what make that claim checkable rather than asserted. Append to `tests/core/AnnotationMeasurementTest.cpp`:

```cpp
// Spec 2.3: measure() is driven by outline(), so a new geometry type needs no
// overload here. These two tests are what make that a checked claim rather
// than an assumption.
TEST_CASE("a point measures length zero with no area",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics metrics =
        core::measure(core::PointGeometry{core::PointF{50.0, 50.0}}, core::PixelSize{});
    REQUIRE(metrics.outlineLength == 0.0);
    REQUIRE_FALSE(metrics.area.has_value());
    REQUIRE_FALSE(metrics.closed);
    REQUIRE_FALSE(metrics.selfIntersecting);
}

// closed == false is what makes the properties panel caption this "Length"
// rather than "Perimeter".
TEST_CASE("an arrow measures its tail-to-head distance with no area",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics metrics = core::measure(
        core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{30.0, 40.0}},
        core::PixelSize{});
    REQUIRE(metrics.outlineLength == 50.0);
    REQUIRE_FALSE(metrics.area.has_value());
    REQUIRE_FALSE(metrics.closed);
}

// Review Focus 1: accepted from disk, and it must measure rather than produce
// a NaN.
TEST_CASE("a zero-length arrow measures length zero",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics metrics = core::measure(
        core::ArrowGeometry{core::PointF{12.0, 34.0}, core::PointF{12.0, 34.0}},
        core::PixelSize{});
    REQUIRE(metrics.outlineLength == 0.0);
    REQUIRE_FALSE(metrics.area.has_value());
}
```

- [ ] **Step 8: Run the measurement tests to verify they pass**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[core][AnnotationMeasurement]"`

Expected: PASS with no change to `AnnotationMeasurement.cpp`. If any of these three fails, §2.3's claim is wrong and that is a finding, not a licence to add an overload — stop and say so.

- [ ] **Step 9: Write the failing serializer tests**

These are the only thing that forces `readGeometry`'s two branches to exist. Append to `tests/core/AnnotationSerializationTest.cpp`:

```cpp
// Spec 2.6: readGeometry is a string chain with no compile-time link to the
// variant. Nothing in the compiler forces its point branch to exist -- this
// test is what forces it. Without the branch the application writes files it
// then refuses to open.
TEST_CASE("a point round-trips through JSON", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("p1", PointGeometry{PointF{1200.5, 800.25}});

    const std::string text = serializeAnnotationDocument(document);
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("\"point\"") != std::string::npos);

    const ParseResult result = parseAnnotationDocument(text);
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.annotations.size() == 1);

    const AnnotationGeometry& geometry = result.document.annotations.front().geometry();
    REQUIRE(std::holds_alternative<PointGeometry>(geometry));
    REQUIRE(std::get<PointGeometry>(geometry).position.x == 1200.5);
    REQUIRE(std::get<PointGeometry>(geometry).position.y == 800.25);
    REQUIRE(result.document.annotations.front().type() == AnnotationType::Point);
}

// Same reasoning as the point above, and with one thing more to prove: the
// pair is ordered, so a round-trip that came back reversed would silently turn
// every arrow in a file around.
TEST_CASE("an arrow round-trips with its direction intact",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{90.0, 60.0}, PointF{10.0, 20.0}});

    const std::string text = serializeAnnotationDocument(document);
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("\"arrow\"") != std::string::npos);

    const ParseResult result = parseAnnotationDocument(text);
    REQUIRE(result.error == ParseError::None);

    const AnnotationGeometry& geometry = result.document.annotations.front().geometry();
    REQUIRE(std::holds_alternative<ArrowGeometry>(geometry));
    const ArrowGeometry& arrow = std::get<ArrowGeometry>(geometry);
    // Tail right-of and below head: an arrow drawn right-to-left and
    // bottom-to-top. Normalising anywhere in the round trip would swap these.
    REQUIRE(arrow.tail.x == 90.0);
    REQUIRE(arrow.tail.y == 60.0);
    REQUIRE(arrow.head.x == 10.0);
    REQUIRE(arrow.head.y == 20.0);
    REQUIRE(result.document.annotations.front().type() == AnnotationType::Arrow);
}

// Accepted from disk even though the tool refuses to create one: refusing it
// would make the whole document unloadable, and by the governing rule never
// again writable, over one shape the user can see and delete.
TEST_CASE("a zero-length arrow round-trips", "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{40.0, 40.0}, PointF{40.0, 40.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    REQUIRE(std::holds_alternative<ArrowGeometry>(
        result.document.annotations.front().geometry()));
}

// Review Focus 2: never write a file we cannot read back. A non-finite
// coordinate has no JSON form the parser would accept, so the write is refused
// outright rather than producing `null` or `NaN` in the file.
TEST_CASE("a non-finite point or arrow refuses to serialize",
          "[core][AnnotationSerialization]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();

    AnnotationDocument withPoint = makeDocument();
    withPoint.annotations.clear();
    withPoint.annotations.emplace_back("p1", PointGeometry{PointF{nan, 0.0}});
    REQUIRE(serializeAnnotationDocument(withPoint).empty());

    AnnotationDocument withArrow = makeDocument();
    withArrow.annotations.clear();
    withArrow.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{0.0, 0.0}, PointF{infinity, 0.0}});
    REQUIRE(serializeAnnotationDocument(withArrow).empty());
}

// Review Focus 3: a missing endpoint must be named, not defaulted. Silently
// reading an absent head as the origin would move someone's arrow to the
// corner of the slide and then autosave it there.
TEST_CASE("an arrow missing an endpoint is refused by name",
          "[core][AnnotationSerialization]")
{
    const ParseResult missingHead = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "arrow", "tail": {"x": 1, "y": 2}})"));
    REQUIRE(missingHead.error == ParseError::InvalidField);
    REQUIRE(missingHead.message.find("head") != std::string::npos);

    const ParseResult missingTail = parseAnnotationDocument(
        documentWithGeometry(R"({"type": "arrow", "head": {"x": 1, "y": 2}})"));
    REQUIRE(missingTail.error == ParseError::InvalidField);
    REQUIRE(missingTail.message.find("tail") != std::string::npos);
}

TEST_CASE("a point missing its position is refused by name",
          "[core][AnnotationSerialization]")
{
    const ParseResult result =
        parseAnnotationDocument(documentWithGeometry(R"({"type": "point"})"));
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("position") != std::string::npos);
}

// schemaVersion does not move when a geometry type is added. Bumping it would
// make every shipped build refuse rectangle-only files during a staggered
// rollout; not bumping makes an old build fail on the new shape alone and
// leave the file intact.
TEST_CASE("adding the point and arrow types does not bump schemaVersion",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document = makeDocument();
    document.annotations.clear();
    document.annotations.emplace_back("p1", PointGeometry{PointF{5.0, 6.0}});
    document.annotations.emplace_back(
        "a1", ArrowGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.schemaVersion == 1);
}
```

`documentWithGeometry` and `makeDocument` already exist in that file. `<limits>` is already included.

- [ ] **Step 10: Run the serializer tests to verify they fail**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[core][AnnotationSerialization]"`

Expected: FAIL. The round-trip cases fail with `result.error == ParseError::None` not satisfied — the writer produces `"type": "point"` and the reader does not know it, so parsing reports `unknown geometry type 'point'`. That is exactly the defect §2.6 warns about, reproduced before it is fixed. The "refused by name" cases fail for the same reason, with the message naming the type rather than the field.

- [ ] **Step 11: Add the two reader branches**

In `src/core/src/AnnotationSerialization.cpp`, add two readers after `readEllipse` and before `readPolygon`:

```cpp
bool readPoint(const json& value, AnnotationGeometry& out, std::string& problem)
{
    PointGeometry marker;
    if (!value.contains("position") || !readPoint(value.at("position"), marker.position)) {
        problem = "geometry.position is not a finite point";
        return false;
    }
    out = marker;
    return true;
}

bool readArrow(const json& value, AnnotationGeometry& out, std::string& problem)
{
    ArrowGeometry arrow;
    if (!value.contains("tail") || !readPoint(value.at("tail"), arrow.tail)) {
        problem = "geometry.tail is not a finite point";
        return false;
    }
    if (!value.contains("head") || !readPoint(value.at("head"), arrow.head)) {
        problem = "geometry.head is not a finite point";
        return false;
    }
    // A zero-length arrow is accepted, exactly as a degenerate ellipse is:
    // refusing it would make the whole document unloadable, and by the
    // governing rule never again writable, over one shape the user can see and
    // delete. The endpoints are NOT normalised -- the order is the direction.
    out = arrow;
    return true;
}
```

The name `readPoint` is already taken by the `(const json&, PointF&)` overload that reads a coordinate pair; the new one takes `(const json&, AnnotationGeometry&, std::string&)` and overloads cleanly on its second parameter. Both are used inside `readPoint(const json&, AnnotationGeometry&, std::string&)` itself, where the inner call passes a `PointF&`, so overload resolution picks the existing one.

Then add the two branches to `readGeometry`'s chain, after the polyline branch and before the unknown-type message:

```cpp
    if (type == kPointTypeName) {
        return readPoint(value, out, problem);
    }
    if (type == kArrowTypeName) {
        return readArrow(value, out, problem);
    }
```

- [ ] **Step 12: Run the serializer tests to verify they pass**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[core][AnnotationSerialization]"`

Expected: PASS, every assertion in `[core][AnnotationSerialization]` green.

- [ ] **Step 13: Run the whole suite**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 tests passed. The ui suite is included because `AnnotationShapeName.cpp` changed; if `slideio-viewer-ui` fails to compile, a guarded site was missed and the error names it.

- [ ] **Step 14: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationGeometry.h src/core/src/AnnotationGeometry.cpp src/core/src/AnnotationSerialization.cpp src/core/src/BoxResize.cpp src/ui/src/AnnotationShapeName.cpp tests/core/AnnotationGeometryTest.cpp tests/core/AnnotationMeasurementTest.cpp tests/core/AnnotationSerializationTest.cpp
git commit -m "Add point marker and arrow geometry types

Two new alternatives of core::AnnotationGeometry, answering at all twelve
compile-guarded dispatch sites and at readGeometry, which the compiler cannot
find. The round-trip tests are what force readGeometry's two branches to
exist; without them the application writes files it then refuses to open.

measure() needed no overload, as the spec predicted: it is outline()-driven.
Three tests now check that rather than assuming it."
```

---

### Task 2: `core::screenDecoration`

A marker has no slide-space extent and an arrowhead sized in slide units vanishes when zoomed out. This is the one piece of real arithmetic in the round, and it goes in core precisely so a test can reach it — under the rejected painter-special-case approach it would have lived in the only layer no test can reach.

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationDecoration.h`
- Create: `src/core/src/AnnotationDecoration.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/core/AnnotationDecorationTest.cpp` (create)

**Interfaces:**
- Consumes: `core::PointGeometry`, `core::ArrowGeometry`, `core::AnnotationGeometry` from Task 1.
- Produces:
  - `struct core::ScreenDecoration { enum class Kind { Disc, ArrowHead }; Kind kind; PointF anchor; double sizePixels; };`
  - `std::optional<core::ScreenDecoration> core::screenDecoration(const AnnotationGeometry& geom, double viewportScale, double discRadiusScreenPixels);`
  - `constexpr double core::kArrowHeadScreenPixels = 12.0;`
  - `constexpr double core::kArrowHeadMaxShaftFraction = 0.5;`

- [ ] **Step 1: Create the two source files and wire them into CMake**

CMake's generate step requires every listed source to exist, so the files are created first and added to the lists second. Create `src/core/include/slideio/viewer/core/AnnotationDecoration.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <optional>

namespace slideio::viewer::core
{

/// Target size of an arrowhead along its shaft, in screen pixels.
///
/// In core rather than ui, unlike the screen-space tolerances in
/// AnnotationInteraction.h, because the function that reads it is here: core
/// cannot name a ui constant, and the arithmetic belongs where a test can
/// reach it. The disc radius goes the other way for the mirror-image reason --
/// its constant already exists in ui with other callers, so it is passed in.
constexpr double kArrowHeadScreenPixels = 12.0;

/// The most of a shaft's length an arrowhead may occupy.
///
/// The head's size is the SMALLER of the fixed size above and this fraction of
/// the shaft. Fixed alone means the head outgrows its shaft on a short arrow
/// or at low zoom; proportional alone means it disappears.
constexpr double kArrowHeadMaxShaftFraction = 0.5;

/// A glyph that must keep its size on SCREEN, for a shape whose slide-space
/// outline cannot express it.
///
/// Sizes only, never screen coordinates: `anchor` stays in slide units and the
/// painter converts it, because it already converts every other point. The
/// arrowhead's direction is likewise the painter's to derive, from the two
/// shaft points it has converted -- correct because the viewport only pans and
/// zooms, so it preserves direction.
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

/// The glyph `geom` needs, or nothing when it needs none.
///
/// `std::nullopt` for a rectangle, an ellipse and a polygon -- their outlines
/// say everything about them -- and for a `viewportScale` that is not
/// positive, consistent with every other screen-space function in this
/// codebase, which returns a safe value rather than dividing by a scale it
/// cannot use.
///
/// What `sizePixels` measures, stated once so the painter is determined:
///
///   * `Disc`: the RADIUS, and `anchor` is the centre -- the marker's own
///     position. `discRadiusScreenPixels` is returned as given (clamped at
///     zero), so the caller decides it; ViewportWidget passes the same value
///     it passes to the hit test, which is what makes the marker you see the
///     target you click, by construction rather than by coincidence.
///   * `ArrowHead`: the head's LENGTH ALONG THE SHAFT, and `anchor` is the
///     arrow's head point. The barbs are half that length to either side,
///     giving a head as wide as it is long.
///
/// outline() is untouched and still yields slide-space geometry: nothing here
/// makes a measurement depend on zoom.
[[nodiscard]] std::optional<ScreenDecoration> screenDecoration(const AnnotationGeometry& geom,
                                                               double viewportScale,
                                                               double discRadiusScreenPixels);

} // namespace slideio::viewer::core
```

Create `src/core/src/AnnotationDecoration.cpp`:

```cpp
#include "slideio/viewer/core/AnnotationDecoration.h"

namespace slideio::viewer::core
{

} // namespace slideio::viewer::core
```

Then add `src/AnnotationDecoration.cpp` to the source list in `src/core/CMakeLists.txt`, on the line after `src/BoxResize.cpp`:

```cmake
    src/VertexEditing.cpp
    src/BoxResize.cpp
    src/AnnotationDecoration.cpp
    src/LevelRaster.cpp
```

Create `tests/core/AnnotationDecorationTest.cpp` with only its includes for now:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationDecoration.h"

#include <cmath>
#include <vector>

using namespace slideio::viewer::core;
```

and add it to `tests/CMakeLists.txt`, after `core/BoxResizeTest.cpp`:

```cmake
    core/VertexEditingTest.cpp
    core/BoxResizeTest.cpp
    core/AnnotationDecorationTest.cpp
    core/WorkspaceSwitchPolicyTest.cpp
```

- [ ] **Step 2: Reconfigure and verify the build is still green**

Run: `cmake -S . -B build/build` then `cmake --build build/build --config Release --target slideio-viewer-core-tests`

Expected: the configure step reports `-- Generating done` and `-- Build files have been written to:`, and the build succeeds. If it reports `CMake Generate step failed` naming a missing source, a file was listed before it existed — create it and rerun.

- [ ] **Step 3: Write the failing tests**

Replace the body of `tests/core/AnnotationDecorationTest.cpp` with:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationDecoration.h"

#include <cmath>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

constexpr double kRadius = 6.0;

} // namespace

// Review Focus 5: a glyph on a shape that has none would be a dot or a
// triangle drawn on a rectangle. The painter asks this question about every
// annotation it draws, so every other type has to say no.
TEST_CASE("the three existing shapes need no decoration",
          "[core][AnnotationDecoration]")
{
    REQUIRE_FALSE(screenDecoration(
        RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}}, 1.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(
        EllipseGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}}, 1.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(
        PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{0.0, 10.0}}, true},
        1.0, kRadius).has_value());
}

TEST_CASE("a point decorates as a disc at its position",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph =
        screenDecoration(PointGeometry{PointF{120.0, 340.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->kind == ScreenDecoration::Kind::Disc);
    REQUIRE(glyph->anchor.x == 120.0);
    REQUIRE(glyph->anchor.y == 340.0);
    REQUIRE(glyph->sizePixels == kRadius);
}

// The radius is returned as given, at every zoom. That is what makes it the
// caller's number and lets ViewportWidget pass the same value it passes to the
// hit test -- so the marker you see is the target you click.
TEST_CASE("a disc keeps the radius it was given at any scale",
          "[core][AnnotationDecoration]")
{
    for (const double scale : std::vector<double>{0.01, 1.0, 40.0}) {
        const std::optional<ScreenDecoration> glyph =
            screenDecoration(PointGeometry{PointF{1.0, 2.0}}, scale, kRadius);
        REQUIRE(glyph.has_value());
        REQUIRE(glyph->sizePixels == kRadius);
    }
}

TEST_CASE("an arrow decorates as a head at its head point",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{100.0, 0.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->kind == ScreenDecoration::Kind::ArrowHead);
    REQUIRE(glyph->anchor.x == 100.0);
    REQUIRE(glyph->anchor.y == 0.0);
}

// A long shaft: the fixed size is the smaller of the two and wins, so the head
// does not grow without limit on a long arrow.
TEST_CASE("a long shaft takes the fixed head size",
          "[core][AnnotationDecoration]")
{
    // 100 slide units at scale 1 is 100 screen pixels; half of that is 50,
    // which is larger than kArrowHeadScreenPixels.
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{100.0, 0.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == kArrowHeadScreenPixels);
}

// A short shaft -- or a long one zoomed out, which is the same thing on screen
// -- takes the fraction, so the head never swallows its own arrow.
TEST_CASE("a short shaft takes the fraction of its length",
          "[core][AnnotationDecoration]")
{
    // 10 slide units at scale 1 is 10 screen pixels; half is 5, smaller than
    // kArrowHeadScreenPixels.
    const std::optional<ScreenDecoration> shortArrow = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{10.0, 0.0}}, 1.0, kRadius);
    REQUIRE(shortArrow.has_value());
    REQUIRE(shortArrow->sizePixels == 10.0 * kArrowHeadMaxShaftFraction);

    // The same long arrow as above, zoomed out until its shaft is 10 pixels.
    const std::optional<ScreenDecoration> zoomedOut = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{100.0, 0.0}}, 0.1, kRadius);
    REQUIRE(zoomedOut.has_value());
    REQUIRE(zoomedOut->sizePixels == 10.0 * kArrowHeadMaxShaftFraction);
}

// The shaft is measured as a hypotenuse, not per axis: a 3-4-5 diagonal is 50
// screen pixels long at scale 10, so the fixed size wins.
TEST_CASE("a diagonal shaft is measured along its length",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{3.0, 4.0}}, 10.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == kArrowHeadScreenPixels);

    // The same diagonal at scale 1 is 5 pixels long: the fraction wins.
    const std::optional<ScreenDecoration> small = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{3.0, 4.0}}, 1.0, kRadius);
    REQUIRE(small.has_value());
    REQUIRE(small->sizePixels == 5.0 * kArrowHeadMaxShaftFraction);
}

// Review Focus 1: the tool refuses to create this, but the parser accepts it
// from disk. The answer is a head of size 0, not a NaN the painter would then
// turn into an undrawable polygon.
TEST_CASE("a zero-length arrow gets a head of size zero",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{40.0, 40.0}, PointF{40.0, 40.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == 0.0);
    REQUIRE(std::isfinite(glyph->sizePixels));
}

// No usable transform, no screen size. Returning something would mean dividing
// by -- or multiplying through -- a scale that means nothing.
TEST_CASE("a non-positive scale yields no decoration",
          "[core][AnnotationDecoration]")
{
    REQUIRE_FALSE(screenDecoration(PointGeometry{PointF{1.0, 2.0}}, 0.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(PointGeometry{PointF{1.0, 2.0}}, -1.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{10.0, 0.0}}, 0.0, kRadius).has_value());
}

// A caller that hands over a negative radius gets zero, not a disc drawn
// inside out.
TEST_CASE("a negative disc radius clamps to zero",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph =
        screenDecoration(PointGeometry{PointF{1.0, 2.0}}, 1.0, -4.0);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == 0.0);
}
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[core][AnnotationDecoration]"`

Expected: FAIL at link time — `unresolved external symbol ... screenDecoration` — because the header declares it and the `.cpp` is an empty namespace.

- [ ] **Step 5: Implement `screenDecoration`**

Replace the body of `src/core/src/AnnotationDecoration.cpp`:

```cpp
#include "slideio/viewer/core/AnnotationDecoration.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

// An overload set visited by std::visit, the same shape as every other
// dispatch over this variant, so a shape added later has to answer here too
// rather than falling silently through to "no glyph".
std::optional<ScreenDecoration> decorationFor(const RectangleGeometry&, double, double)
{
    return std::nullopt;
}

std::optional<ScreenDecoration> decorationFor(const EllipseGeometry&, double, double)
{
    return std::nullopt;
}

std::optional<ScreenDecoration> decorationFor(const PolygonGeometry&, double, double)
{
    return std::nullopt;
}

std::optional<ScreenDecoration> decorationFor(const PointGeometry& marker, double,
                                              double discRadiusScreenPixels)
{
    // The radius is the caller's, returned as given. That is the whole point
    // of it being a parameter: ViewportWidget passes the same value it passes
    // to the hit test, so the marker and its grab target cannot drift apart.
    return ScreenDecoration{ScreenDecoration::Kind::Disc, marker.position,
                            std::max(0.0, discRadiusScreenPixels)};
}

std::optional<ScreenDecoration> decorationFor(const ArrowGeometry& arrow, double viewportScale,
                                              double)
{
    const double shaftScreenPixels =
        std::hypot(arrow.head.x - arrow.tail.x, arrow.head.y - arrow.tail.y) * viewportScale;
    // The smaller of the two rules. Fixed alone outgrows its shaft on a short
    // arrow or at low zoom; proportional alone disappears. A zero-length arrow
    // -- which the tool refuses but the parser accepts from disk -- gives 0
    // here rather than a direction that cannot be normalised.
    const double size =
        std::min(kArrowHeadScreenPixels, shaftScreenPixels * kArrowHeadMaxShaftFraction);
    return ScreenDecoration{ScreenDecoration::Kind::ArrowHead, arrow.head, std::max(0.0, size)};
}

} // namespace

std::optional<ScreenDecoration> screenDecoration(const AnnotationGeometry& geom,
                                                 double viewportScale,
                                                 double discRadiusScreenPixels)
{
    // Written as the negation of "is positive" rather than as "<= 0" so that a
    // NaN scale is refused too: NaN fails every comparison, and `NaN <= 0.0`
    // is false, which would let it through to multiply the shaft length into a
    // NaN size the painter cannot use.
    if (!(viewportScale > 0.0)) {
        return std::nullopt;
    }
    return std::visit(
        [&](const auto& shape) {
            return decorationFor(shape, viewportScale, discRadiusScreenPixels);
        },
        geom);
}

} // namespace slideio::viewer::core
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release --target slideio-viewer-core-tests` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[core][AnnotationDecoration]"`

Expected: PASS, every assertion in `[core][AnnotationDecoration]` green.

- [ ] **Step 7: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 tests passed.

- [ ] **Step 8: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationDecoration.h src/core/src/AnnotationDecoration.cpp src/core/CMakeLists.txt tests/CMakeLists.txt tests/core/AnnotationDecorationTest.cpp
git commit -m "Size the marker disc and the arrowhead in screen pixels

A new Qt-free core file. The disc radius is a parameter rather than a core
constant because kHitToleranceScreenPixels lives in ui and core cannot reach
it -- one constant, no coincidence to maintain. The arrowhead constants go the
other way and live here, because the arithmetic that reads them is here and
could not otherwise be tested at all."
```

---

### Task 3: `ui::arrowExceedsMinimumLength` and the two new tools

`dragExceedsMinimumSize` tests either dimension of a bounding box, so a diagonal drag of 10 screen pixels presents a 7x7 box and is rejected against a 10-pixel threshold. An arrow's gesture is a length, so its minimum is the hypotenuse.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`
- Modify: `src/ui/src/AnnotationInteraction.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/ui/AnnotationInteractionTest.cpp`
- Test: `tests/ui/AnnotationShapeNameTest.cpp` (create)

**Interfaces:**
- Consumes: `core::PointF` (unchanged), `ui::kHitToleranceScreenPixels` (unchanged).
- Produces:
  - `enum class ui::AnnotationTool { Pan, Rectangle, Ellipse, Polygon, Freehand, Point, Arrow };`
  - `bool ui::arrowExceedsMinimumLength(core::PointF tailSlide, core::PointF headSlide, double viewportScale, double minimumScreenPixels);`

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
// A box minimum is the wrong test for an arrow. dragExceedsMinimumSize asks
// whether EITHER dimension of a bounding box reaches the threshold, so a
// diagonal drag of exactly 10 screen pixels presents a 7.07 x 7.07 box and is
// rejected -- a gesture the user plainly made.
TEST_CASE("a diagonal drag at the threshold is accepted as an arrow",
          "[ui][AnnotationInteraction]")
{
    // 3-4-5 at scale 2 is a 10-pixel shaft, in a 6 x 8 box.
    REQUIRE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{3.0, 4.0},
                                          2.0, 10.0));
    // The box test, for contrast: neither dimension reaches 10.
    REQUIRE_FALSE(ui::dragExceedsMinimumSize(
        core::boundingBox(core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                  core::PointF{3.0, 4.0}}),
        2.0, 10.0));
}

TEST_CASE("an arrow shorter than the minimum is refused",
          "[ui][AnnotationInteraction]")
{
    // 3-4-5 at scale 1 is a 5-pixel shaft.
    REQUIRE_FALSE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{3.0, 4.0},
                                                1.0, 10.0));
}

TEST_CASE("an arrow is measured the same in every direction",
          "[ui][AnnotationInteraction]")
{
    const double scale = 1.0;
    const double minimum = 10.0;
    // Right, left, down, up and both diagonals, each 20 slide units long or
    // longer, so direction cannot change the answer.
    REQUIRE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{20.0, 0.0},
                                          scale, minimum));
    REQUIRE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{-20.0, 0.0},
                                          scale, minimum));
    REQUIRE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{0.0, 20.0},
                                          scale, minimum));
    REQUIRE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{0.0, -20.0},
                                          scale, minimum));
    REQUIRE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{-20.0, -20.0},
                                          scale, minimum));
}

TEST_CASE("a zero-length arrow is refused", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(ui::arrowExceedsMinimumLength(core::PointF{40.0, 40.0},
                                                core::PointF{40.0, 40.0}, 1.0, 10.0));
}

// Review Focus 4: with no usable transform there is no screen distance to
// compare, and inventing one would let a focus loss before the first frame
// commit an arrow out of nothing. False, like every other predicate here.
TEST_CASE("a non-positive scale refuses an arrow", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0},
                                                core::PointF{1000.0, 1000.0}, 0.0, 10.0));
    REQUIRE_FALSE(ui::arrowExceedsMinimumLength(core::PointF{0.0, 0.0},
                                                core::PointF{1000.0, 1000.0}, -2.0, 10.0));
}

// The two new tools are single-gesture tools, not multi-click ones: they
// commit on the release of the press that started them, so no pending-shape
// state outlives a drag.
TEST_CASE("the point and arrow tools are not n-point tools",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(ui::isNPointTool(ui::AnnotationTool::Point));
    REQUIRE_FALSE(ui::isNPointTool(ui::AnnotationTool::Arrow));
}

// Both are drawing tools, so a left press belongs to the tool and never to a
// move -- exactly as it does for the rectangle.
TEST_CASE("a left press under the point or arrow tool belongs to the tool",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(ui::resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                                 ui::AnnotationTool::Point, true) == ui::DragOwner::Tool);
    REQUIRE(ui::resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                                 ui::AnnotationTool::Arrow, false) == ui::DragOwner::Tool);
    // Held space still pans, whatever the tool.
    REQUIRE(ui::resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                                 ui::AnnotationTool::Point, false) == ui::DragOwner::Pan);
}
```

The file already uses the `ui::` and `core::` qualifications shown; if it instead opens with `using namespace slideio::viewer;` and unqualified names, match the surrounding style rather than this snippet. `AnnotationInteraction.h` already includes `AnnotationGeometry.h`, so `core::boundingBox` and `core::RectangleGeometry` are available.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release --target slideio-viewer-ui-tests`

Expected: FAIL at compile time — `'Point': is not a member of 'slideio::viewer::ui::AnnotationTool'` and `'arrowExceedsMinimumLength': identifier not found`.

- [ ] **Step 3: Add the enumerators and the declaration**

In `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`, append to `AnnotationTool` — at the end, so no existing value changes:

```cpp
enum class AnnotationTool
{
    Pan,
    Rectangle,
    Ellipse,
    Polygon,
    Freehand,
    Point,
    Arrow,
};
```

Declare the predicate immediately after `dragExceedsMinimumSize`:

```cpp
/// True when a tail-to-head drag is long enough on screen to be a deliberate
/// arrow rather than a misclick.
///
/// Not dragExceedsMinimumSize, and the difference is the whole reason this
/// exists: that one tests either DIMENSION of a bounding box, so a diagonal
/// drag of exactly 10 screen pixels presents a 7.07 x 7.07 box and is
/// rejected. An arrow's gesture is a length, so the test is the hypotenuse,
/// and the answer is then the same in every direction.
///
/// False for a non-positive scale, like every other predicate here: with no
/// usable transform there is no screen distance to compare.
[[nodiscard]] bool arrowExceedsMinimumLength(core::PointF tailSlide, core::PointF headSlide,
                                             double viewportScale, double minimumScreenPixels);
```

- [ ] **Step 4: Implement it**

In `src/ui/src/AnnotationInteraction.cpp`, add after `dragExceedsMinimumSize`:

```cpp
bool arrowExceedsMinimumLength(core::PointF tailSlide, core::PointF headSlide,
                               double viewportScale, double minimumScreenPixels)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double dxScreen = (headSlide.x - tailSlide.x) * viewportScale;
    const double dyScreen = (headSlide.y - tailSlide.y) * viewportScale;
    return std::hypot(dxScreen, dyScreen) >= minimumScreenPixels;
}
```

`<cmath>` is already included.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`

Expected: `ui-tests` passes. Run it under ctest, never as a bare `.exe`.

- [ ] **Step 6: Cover the two new shape names**

Spec §7.2 asks that `shapeName` answer for both new types. The names are written in Task 1, where the compiler forces them, but nothing checks that the answers are right — and a shape the user sees labelled as nothing at all is exactly what `shapeTypeName`'s empty-string fallback produces when a case is missed.

`QCoreApplication::translate` is a static that returns its source text when no translator is installed and needs no `QCoreApplication` instance, so this is reachable from `ui-tests`. Create `tests/ui/AnnotationShapeNameTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationShapeName.h"

using namespace slideio::viewer;

namespace
{

core::Annotation annotationWith(core::AnnotationGeometry geometry)
{
    return core::Annotation("id-1", std::move(geometry));
}

} // namespace

// shapeName dispatches over the variant and so cannot be forgotten silently.
// shapeTypeName is a switch with an empty-string fallback, which CAN be: a
// missed case shows the user a blank Type column rather than a compile error.
TEST_CASE("every geometry type has a user-visible name", "[ui][AnnotationShapeName]")
{
    REQUIRE(ui::shapeName(annotationWith(
                core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}}))
            == QStringLiteral("Rectangle"));
    REQUIRE(ui::shapeName(annotationWith(core::PointGeometry{core::PointF{1.0, 2.0}}))
            == QStringLiteral("Point"));
    REQUIRE(ui::shapeName(annotationWith(
                core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}}))
            == QStringLiteral("Arrow"));
}

TEST_CASE("shapeTypeName answers for the point and arrow types",
          "[ui][AnnotationShapeName]")
{
    REQUIRE(ui::shapeTypeName(core::AnnotationType::Point) == QStringLiteral("Point"));
    REQUIRE(ui::shapeTypeName(core::AnnotationType::Arrow) == QStringLiteral("Arrow"));
    // Not the empty string the fallback would give, which is what a missed
    // case looks like.
    REQUIRE_FALSE(ui::shapeTypeName(core::AnnotationType::Point).isEmpty());
    REQUIRE_FALSE(ui::shapeTypeName(core::AnnotationType::Arrow).isEmpty());
}

// The two words must be the same whichever route a caller takes, or the list
// panel's Type column and the properties panel's Type row disagree about one
// annotation.
TEST_CASE("shapeName and shapeTypeName agree", "[ui][AnnotationShapeName]")
{
    const core::Annotation marker = annotationWith(core::PointGeometry{core::PointF{1.0, 2.0}});
    REQUIRE(ui::shapeName(marker) == ui::shapeTypeName(marker.type()));

    const core::Annotation arrow = annotationWith(
        core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(ui::shapeName(arrow) == ui::shapeTypeName(arrow.type()));
}
```

Add it to `tests/CMakeLists.txt`, in the `slideio-viewer-ui-tests` source list after `ui/AnnotationPropertiesEditTest.cpp`:

```cmake
    ui/AnnotationPropertiesEditTest.cpp
    ui/AnnotationShapeNameTest.cpp
    ui/AnnotationSettingsTest.cpp
```

- [ ] **Step 7: Reconfigure, then run the ui suite**

Run: `cmake -S . -B build/build` then `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`

Expected: `ui-tests` passes, including the three new `[ui][AnnotationShapeName]` cases. The reconfigure is needed because a test source was added; CMake's generate step requires the file to exist first, which Step 6 did.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h src/ui/src/AnnotationInteraction.cpp tests/CMakeLists.txt tests/ui/AnnotationInteractionTest.cpp tests/ui/AnnotationShapeNameTest.cpp
git commit -m "Add the Point and Arrow tools and the arrow's length minimum

An arrow's minimum is the hypotenuse, not a box dimension: dragExceedsMinimumSize
would reject a deliberate 10-pixel diagonal drag, which presents a 7x7 box. One
test contrasts the two directly so the distinction cannot be refactored away.

Also covers shapeName and shapeTypeName for the two new types. shapeName
dispatches over the variant and could not have been forgotten; shapeTypeName is
a switch with an empty-string fallback and could, showing the user a blank Type
column instead of a compile error."
```

---

### Task 4: `ViewportWidget` — the two tools, their glyphs and their preview

Nothing in this task is reachable from any test suite: no test binary creates a `QApplication`, so no widget can be constructed. It is covered by manual group AB in spec §7.3, which Task 6 prepares.

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp`

**Interfaces:**
- Consumes: `core::PointGeometry`, `core::ArrowGeometry` (Task 1); `core::screenDecoration`, `core::ScreenDecoration` (Task 2); `ui::arrowExceedsMinimumLength`, `ui::AnnotationTool::Point`, `::Arrow` (Task 3).
- Produces: nothing other tasks consume.

- [ ] **Step 1: Include the new core header**

In `src/ui/src/ViewportWidget.cpp`, add to the project-header include block, beside the other `slideio/viewer/core/Annotation*` includes:

```cpp
#include "slideio/viewer/core/AnnotationDecoration.h"
```

- [ ] **Step 2: Commit the two new shapes in `commitDrawDrag`**

`commitDrawDrag` currently begins by computing two boxes. Insert the two new branches at the very top of the function body, before `const core::RectF gestureBox = ...`:

```cpp
    const double toolScale = m_impl->controller ? m_impl->controller->viewport().scale() : 0.0;

    // The Point tool is the exact inverse of every other: a click with no
    // movement is the whole gesture, and there is no size that could be
    // insufficient, so dragExceedsMinimumSize is never consulted.
    //
    // Created on the RELEASE, at the PRESS position. Both halves matter and
    // they are not the same choice. On the release, because every other tool
    // commits there and because a press that turns into something else --
    // held space taking over to pan -- must not have already left a marker
    // behind. At the press, because a hand twitching during a click must not
    // shift the mark, and because the press is the point the user aimed at.
    if (m_impl->activeTool == AnnotationTool::Point) {
        if (!m_impl->annotationsEditable()) {
            emit annotationEditBlocked(readOnlyEditMessage());
            return false;
        }
        const std::string id = m_impl->annotations->add(
            core::PointGeometry{m_impl->drawAnchorSlide});
        pushCreateCommand(id);
        m_impl->annotations->setSelected(id);
        return true;
    }

    if (m_impl->activeTool == AnnotationTool::Arrow) {
        // The hypotenuse, not a box dimension: a deliberate 10-pixel diagonal
        // drag presents a 7x7 box and dragExceedsMinimumSize would refuse it.
        // Below the minimum this is a misclick and is discarded in silence,
        // exactly as an undersized rectangle drag is, and the release handler
        // then treats the press as a selection click.
        if (!arrowExceedsMinimumLength(m_impl->drawAnchorSlide, m_impl->drawCurrentSlide,
                                       toolScale, kHitToleranceScreenPixels)) {
            return false;
        }
        if (!m_impl->annotationsEditable()) {
            emit annotationEditBlocked(readOnlyEditMessage());
            return false;
        }
        // Press to release, in that order, and never normalised: the order is
        // the direction, and swapping it would reverse every arrow drawn
        // right-to-left.
        const std::string id = m_impl->annotations->add(
            core::ArrowGeometry{m_impl->drawAnchorSlide, m_impl->drawCurrentSlide});
        pushCreateCommand(id);
        m_impl->annotations->setSelected(id);
        return true;
    }
```

In the existing box path further down, insert the same read-only gate immediately after the `dragExceedsMinimumSize` refusal and before `withMinimumExtent` is applied:

```cpp
    if (!dragExceedsMinimumSize(gestureBox, scale, kHitToleranceScreenPixels)) {
        return false;
    }

    // A valid shape refused for editability is "you cannot do this here",
    // unlike the silent misclick above, so the user is told. commitPendingShape
    // has had this gate since persistence landed; this path never did. It is
    // normally unreachable because a read-only slide disables the drawing tool
    // ACTIONS -- but the active tool is deliberately not reset when a slide
    // opens, so picking a tool on an editable slide and then opening a
    // read-only one leaves it live, and the shape was being created.
    if (!m_impl->annotationsEditable()) {
        emit annotationEditBlocked(readOnlyEditMessage());
        return false;
    }
```

The existing `const double scale = ...` line below is now a second local computing the same value as `toolScale`. Delete the later declaration and use `toolScale` in its two uses (`dragExceedsMinimumSize(gestureBox, toolScale, ...)` and `screenToleranceToSlide(1.0, toolScale)`), so one name holds the scale for the whole function.

- [ ] **Step 3: Paint the glyphs**

In `paintAnnotations`, add a third lambda after the existing `screenPath` lambda:

```cpp
    // One glyph of the kind core::screenDecoration asked for. The painter stays
    // shape-blind: it is told a kind, an anchor and a size in pixels, and never
    // which geometry type produced them.
    //
    // The arrowhead's direction comes from the two screen points of the shaft,
    // which is correct because the viewport only pans and zooms and so
    // preserves direction -- so core never needs screen coordinates, only
    // screen sizes.
    const auto paintDecoration = [&painter, &toScreen](const core::ScreenDecoration& glyph,
                                                       const core::Outline& shape,
                                                       const QColor& color) {
        const QPointF anchor = toScreen(glyph.anchor);
        if (glyph.kind == core::ScreenDecoration::Kind::Disc) {
            // Black outline under a fill in the annotation's own colour, the
            // same two-tone treatment the vertex and resize handles use:
            // brightfield H&E glass and tissue are near-white and paintGL
            // clears the slide rect to white, so a pale marker alone would
            // vanish on the commonest slide type.
            painter.setPen(QPen(QColor(0x00, 0x00, 0x00), 1.0));
            painter.setBrush(QBrush(color));
            painter.drawEllipse(anchor, glyph.sizePixels, glyph.sizePixels);
            return;
        }
        // A head needs a shaft to point along, and a size to be drawn at. A
        // zero-length arrow from disk has neither and is left as its (invisible)
        // shaft rather than being given an arbitrary direction.
        if (shape.points.size() < 2 || glyph.sizePixels <= 0.0) {
            return;
        }
        const QPointF tail = toScreen(shape.points.front());
        const double dx = anchor.x() - tail.x();
        const double dy = anchor.y() - tail.y();
        const double length = std::hypot(dx, dy);
        if (length <= 0.0) {
            return;
        }
        const double ux = dx / length;
        const double uy = dy / length;
        // sizePixels is the head's length ALONG the shaft; the barbs are half
        // that to either side, giving a head as wide as it is long.
        const QPointF base(anchor.x() - ux * glyph.sizePixels,
                           anchor.y() - uy * glyph.sizePixels);
        const QPointF barb(-uy * glyph.sizePixels * 0.5, ux * glyph.sizePixels * 0.5);
        QPolygonF head;
        head << anchor << (base + barb) << (base - barb);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(color));
        painter.drawPolygon(head);
    };
```

Then, inside the per-annotation loop, immediately after `painter.drawPath(screenPath(shape));`:

```cpp
        // The same radius the hit test uses, so the marker you see is the
        // target you click -- by construction, rather than by two numbers that
        // happen to agree.
        if (const std::optional<core::ScreenDecoration> glyph = core::screenDecoration(
                annotation.geometry(), vp.scale(), kHitToleranceScreenPixels)) {
            paintDecoration(*glyph, shape, stroke);
        }
```

- [ ] **Step 4: Ring a selected marker**

A point's `outline()` is a single vertex, so `screenPath` produces a path with only a `moveTo` and the selection pass paints nothing at all — a selected marker would show no selection. Inside the selection block in `paintAnnotations`, after the two `painter.drawPath(path)` calls and before the block closes:

```cpp
            // A point's outline is one vertex, so the path above paints
            // nothing and the selection would be invisible. Ring the disc
            // instead, in the same two tones and just outside it, so the user
            // can see what Delete would remove. An arrow needs none of this:
            // its shaft is a real open path and takes the ordinary treatment.
            if (const std::optional<core::ScreenDecoration> glyph = core::screenDecoration(
                    selected->geometry(), vp.scale(), kHitToleranceScreenPixels)) {
                if (glyph->kind == core::ScreenDecoration::Kind::Disc) {
                    const QPointF centre = toScreen(glyph->anchor);
                    const double radius = glyph->sizePixels + 3.0;
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor(0x00, 0x00, 0x00), 3.0));
                    painter.drawEllipse(centre, radius, radius);
                    painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1.0, Qt::DashLine));
                    painter.drawEllipse(centre, radius, radius);
                }
            }
```

- [ ] **Step 5: Preview the arrow drag, and nothing for the point**

The live-preview block currently opens with `if (m_impl->drawing) {` and computes `pendingDragBox()`. Replace that opening line with two guarded branches, leaving the existing body — the ellipse 512-gon and the rectangle — untouched inside the second:

```cpp
    // The rectangle, ellipse or arrow being dragged right now is not in the
    // model yet. The Point tool has no drag and so no preview: the marker
    // appears on the release, and the degenerate box the generic path would
    // compute paints a dashed artefact under the cursor instead.
    if (m_impl->drawing && m_impl->activeTool == AnnotationTool::Arrow) {
        // Tail to cursor, with no head. The head's size is derived from the
        // shaft's length, so a head that grew through the drag would read as a
        // bug; the committed arrow gets one.
        painter.setPen(QPen(QColor(0xE6, 0x7E, 0x22), 2.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(toScreen(m_impl->drawAnchorSlide),
                         toScreen(m_impl->drawCurrentSlide));
    } else if (m_impl->drawing && m_impl->activeTool != AnnotationTool::Point) {
        const core::RectF box = m_impl->pendingDragBox();
        // ... existing body unchanged ...
    }
```

Note what is deliberately NOT done here: `applyDragConstraints` is not consulted for an arrow. Shift squares a box about an anchor and Alt doubles it from a centre; neither means anything for a tail-to-head drag, and FR-ANN-06 asks for no modifiers (spec §4.3).

- [ ] **Step 6: Build and run the whole suite**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: the build succeeds and 4/4 tests pass. No test exercises this file; a green suite here means only that nothing else broke.

- [ ] **Step 7: Verify by reading what is deliberately untouched**

Confirm each of these by reading the code, and record the line you checked. None of them should need an edit, and an edit to any of them is a finding worth raising rather than a change to make quietly:

- `Impl::resizableBoxGeometry` asks `core::withBoxReplaced`, which answers `nullopt` for both new types — so no handles are painted and no press is intercepted (spec §4.4).
- `Impl::vertexEditPolygon` uses `std::get_if<core::PolygonGeometry>`, so double-clicking a marker or an arrow enters no mode (AB13).
- The `Qt::Key_Escape` cascade reaches `setActiveTool(AnnotationTool::Pan)` when nothing is in flight, so Escape reverts either new tool (AB20).
- `updateCursor` gives a cross cursor for every tool that is not `Pan`, so both get one.
- `mouseMoveEvent`'s `DragOwner::Tool && drawing` branch already refreshes `drawCurrentSlide`, which is what feeds the arrow preview and its commit.
- `core::Viewport::fitToRect` contributes a candidate scale only from an axis that has extent, and keeps the current scale when both are degenerate while still recentring — its comment names "a point" explicitly. A marker's bounding box is degenerate on both axes and an axis-aligned arrow's on one, so the show-annotation action recentres on them without zooming (AB16). Spec §2.4 says this is verified, not assumed: **no change is required here, and none should be made.** If AB16 fails, that is a finding about `fitToRect`, not a licence to special-case a geometry type in `ViewportController::showRect`.
- `paintAnnotations`' culling test pads each annotation's screen bounds by `lineWidth / 2 + 1` before calling `QRectF::intersects`, which is false for a rect of zero width or height. A marker's padded bounds are therefore at least 2 pixels across and it is not culled. `kMinLineWidth` is 0.01, so the padding can never be zero.
- The release handler's selection-click block runs after `commitDrawDrag`. For a newly placed marker it re-hit-tests at the release position and finds the same marker — `AnnotationModel::hitTest` scans in reverse, and the new annotation is last — so the selection does not change. No guard is needed.

- [ ] **Step 8: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Place markers and draw arrows in the viewport

The marker commits on the release at the press position, so a hand twitching
during a click does not shift the mark and a press that becomes a pan leaves
nothing behind. Glyphs are painted from core::screenDecoration, so the painter
is told a kind, an anchor and a pixel size and still has no idea what shape it
is drawing.

Also adds the annotationsEditable() gate to commitDrawDrag, which never had
one: commitPendingShape has refused a read-only slide since persistence
landed, and this path was silently creating the shape instead. It is reachable
by picking a drawing tool on an editable slide and then opening a read-only
one, which leaves the tool active."
```

---

### Task 5: `MainWindow` — the two tool actions

Eight sites, all mechanical, all mirroring what `ellipseToolAction` already does.

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: `ui::AnnotationTool::Point`, `::Arrow` (Task 3); `ViewportWidget::setActiveTool` (unchanged).
- Produces: nothing other tasks consume.

- [ ] **Step 1: Declare the two actions**

In the `Impl` member block, after `QAction* freehandToolAction = nullptr;`:

```cpp
    QAction* pointToolAction = nullptr;
    QAction* arrowToolAction = nullptr;
```

- [ ] **Step 2: Create them**

After the `freehandToolAction` creation block:

```cpp
        // "M" for marker: "P" belongs to Polygon, whose own mnemonic is the
        // "o". These are the only bare-letter shortcuts in the application
        // besides the five already here, and they carry the same
        // WidgetWithChildrenShortcut scope for the same reason -- at window
        // scope "m" and "a" would be stolen from every text field in the
        // window.
        pointToolAction = new QAction("&Point Marker", owner);
        pointToolAction->setCheckable(true);
        pointToolAction->setShortcut(QKeySequence("M"));
        pointToolAction->setStatusTip(
            "Place a point marker with a single click; the tool stays active for the next one");

        arrowToolAction = new QAction("&Arrow", owner);
        arrowToolAction->setCheckable(true);
        arrowToolAction->setShortcut(QKeySequence("A"));
        arrowToolAction->setStatusTip("Draw an arrow by dragging from its tail to its head");
```

- [ ] **Step 3: Scope, group and disable them**

Beside the five existing `setShortcutContext` calls:

```cpp
        pointToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        arrowToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
```

Beside the five `toolGroup->addAction` calls:

```cpp
        toolGroup->addAction(pointToolAction);
        toolGroup->addAction(arrowToolAction);
```

Beside the four `setEnabled(false)` calls that follow them:

```cpp
        pointToolAction->setEnabled(false);
        arrowToolAction->setEnabled(false);
```

- [ ] **Step 4: Add them to the Tools menu**

After `toolsMenu->addAction(freehandToolAction);`:

```cpp
        toolsMenu->addAction(pointToolAction);
        toolsMenu->addAction(arrowToolAction);
```

- [ ] **Step 5: Associate, connect and mirror the checked state**

In `connectSignals`, beside the five `viewportWidget->addAction(...)` calls:

```cpp
        viewportWidget->addAction(pointToolAction);
        viewportWidget->addAction(arrowToolAction);
```

After the `freehandToolAction` connection:

```cpp
        QObject::connect(pointToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Point);
        });
        QObject::connect(arrowToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Arrow);
        });
```

Going through `selectDrawingTool` rather than straight to `setActiveTool` is what keeps FR-USER-01's identity gate in front of both: annotations record who created them, and these two create annotations like any other tool.

In the `activeToolChanged` handler, beside the five `setChecked` lines:

```cpp
                             pointToolAction->setChecked(tool == AnnotationTool::Point);
                             arrowToolAction->setChecked(tool == AnnotationTool::Arrow);
```

- [ ] **Step 6: Enable and disable them with the others**

In the `slideClosed` handler, beside the four `setEnabled(false)` calls:

```cpp
            pointToolAction->setEnabled(false);
            arrowToolAction->setEnabled(false);
```

In `applyAnnotationAvailability`, beside the four `setEnabled(available)` calls:

```cpp
    pointToolAction->setEnabled(available);
    arrowToolAction->setEnabled(available);
```

- [ ] **Step 7: Build and run the whole suite**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: the build succeeds and 4/4 tests pass.

- [ ] **Step 8: Verify every site was covered**

Run: `grep -c "pointToolAction\|arrowToolAction" src/ui/src/MainWindow.cpp`

Expected: 28 — fourteen lines each, matching `ellipseToolAction`'s count. Confirm the
baseline with `grep -c "ellipseToolAction" src/ui/src/MainWindow.cpp`, which prints 14.
A lower count means a site was missed; `grep -n "freehandToolAction" src/ui/src/MainWindow.cpp`
lists every place one belongs. (Fourteen because the creation block spans four lines —
`new QAction`, `setCheckable`, `setShortcut`, `setStatusTip` — and the other ten sites are
one line each.)

- [ ] **Step 9: Commit**

```bash
git add src/ui/src/MainWindow.cpp
git commit -m "Add Point Marker and Arrow to the Tools menu

Both go through selectDrawingTool, so FR-USER-01's identity gate stands in
front of them as it does every other tool that creates an annotation. M and A,
scoped to the viewport like the other bare-letter shortcuts."
```

---

### Task 6: Documentation, and a verified build for the manual run

Group AB's 22 checks are the only coverage this feature's interaction layer will ever get, and they need a binary that is actually the code just written.

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md`

`documents/implementation-stages.md` is deliberately **not** touched. CLAUDE.md describes it as tracking which stages have landed, but it carries no per-item status notation and has not been modified since the initial commit — the ellipse, vertex-editing and box-resize rounds all left it alone. Adding a marker for one item would invent a convention for a document that is the original plan, not a status board. Landed state is recorded in CLAUDE.md, which is where this task records it.

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: nothing.

- [ ] **Step 1: Install, and verify the install actually happened**

A running viewer locks the `.exe`, so `cmake --install` skips the copy, still exits 0, and leaves the previous build in place. The exit code is not the thing to check; the artifact is.

```bash
cmake --install build/build --config Release
sha256sum build/build/Release/slideio-viewer.exe build/install/release/bin/slideio-viewer.exe
```

Expected: two identical hashes. If they differ, a viewer is running — close it and install again. Record the hash; it goes in the spec in Step 4.

- [ ] **Step 2: Update `CLAUDE.md`**

In the "Not built yet" paragraph, replace `the point, arrow and text shapes` with `the text shape`.

In the **Annotation geometry** bullet under Key Design Decisions, replace the sentence `One `PolygonGeometry` covers both the closed polygon and the open freehand line; they differ only in a `closed` flag.` with:

```markdown
One `PolygonGeometry` covers both the closed polygon and the open freehand
line; they differ only in a `closed` flag. `PointGeometry` and `ArrowGeometry`
are separate alternatives rather than degenerate cases of those: a marker is
not a zero-extent rectangle, and an arrow cannot be a two-vertex polyline
because `typeOf()` derives the type from the geometry and the two would be
indistinguishable. **An arrow's endpoint pair is ORDERED** — `tail` then
`head` — and must never be normalised the way a rectangle's corners are, or
every arrow drawn right-to-left silently reverses.
```

In the same bullet, replace this sentence —

```markdown
**Adding the ellipse in 2026-10 fired ten such sites**, of which the design spec had enumerated five: the six in `AnnotationGeometry.cpp`, then `shapeToJson`, `isRepresentableShape` and `geometryTypeName`'s `static_assert` in `AnnotationSerialization.cpp`, and `ShapeNameVisitor` in `src/ui/src/AnnotationShapeName.cpp` — which also has a `switch` over `AnnotationType` beside it.
```

— with:

```markdown
**Adding the ellipse in 2026-10 fired ten such sites**, of which the design spec had enumerated five: the six in `AnnotationGeometry.cpp`, then `shapeToJson`, `isRepresentableShape` and `geometryTypeName`'s `static_assert` in `AnnotationSerialization.cpp`, and `ShapeNameVisitor` in `src/ui/src/AnnotationShapeName.cpp` — which also has a `switch` over `AnnotationType` beside it. **Adding the point and arrow a fortnight later fired twelve**, the eleven above — counting `ShapeNameVisitor` and the `AnnotationType` switch beside it separately — plus `replaceBox` in `BoxResize.cpp`, which box resizing had added in between. That twelfth is the first demonstration that `withBoxReplaced`'s `std::visit` earns what it costs: both new types were forced to answer `std::nullopt` where a `get_if` chain would have let them fall silently through to "not resizable". **The next shape will fire thirteen** — `decorationFor` in `AnnotationDecoration.cpp` is a new one, deliberately a visited overload set rather than a `get_if` chain for the same reason.
```

Finally, in the same bullet, replace the closing sentence `**One `static_assert` remains**, in `geometryTypeName` (`AnnotationSerialization.cpp:167`), and it is one of the ten sites above.` with the same sentence ending `one of the twelve sites above.` — the line number moves with this round's edits, so re-grep it rather than copying 167 forward:

```bash
grep -n "A new geometry alternative needs its own type name here" src/core/src/AnnotationSerialization.cpp
```

Add a new bullet after the box-resize one:

```markdown
- **A marker and an arrowhead are sized in screen pixels, by core.**
  `core::screenDecoration` (`AnnotationDecoration.{h,cpp}`) answers, for one
  geometry, what glyph it needs and how many pixels across — a `Disc` whose
  size is a radius, an `ArrowHead` whose size is its length along the shaft, or
  nothing at all. The painter is told a kind, an anchor in slide units and a
  pixel size, and still has no idea what shape it is drawing; it derives the
  head's direction from the two shaft points it has already converted, which is
  correct because the viewport only pans and zooms. **The disc radius is a
  parameter, not a core constant:** `kHitToleranceScreenPixels` lives in ui and
  core cannot reach it, so `ViewportWidget` passes the same value it passes to
  the hit test and the marker you see is the target you click by construction.
  The arrowhead's two constants go the other way and live in core, because the
  arithmetic that reads them is there — the head is the smaller of
  `kArrowHeadScreenPixels` and `kArrowHeadMaxShaftFraction` of the shaft, since
  fixed alone outgrows a short arrow and proportional alone disappears. The
  rejected alternative was making `outline()` take a scale, which the ellipse
  spec forbids: a stored annotation's measured value would then depend on the
  zoom it happened to be measured at. See
  `docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md`.
```

- [ ] **Step 3: Record the read-only gate in `CLAUDE.md`**

The gate Task 4 added is a behaviour change to the rectangle and ellipse tools, not only a new feature's plumbing, so it gets its own line. Add this bullet immediately after the marker/arrowhead bullet from Step 2:

```markdown
- **Every path that creates an annotation asks `annotationsEditable()` first.**
  `commitPendingShape` has since persistence landed; `commitDrawDrag` did not
  until 2026-10-17, so the rectangle and ellipse tools were creating a shape on
  a read-only slide in silence. It is hard to reach — a read-only slide
  disables the drawing tool *actions* — but the active tool is deliberately not
  reset when a slide opens, so picking a tool on an editable slide and then
  opening a read-only one leaves it live. The refusal is noisy
  (`annotationEditBlocked`) where a misclick is silent: a gesture too small to
  be a shape is the user's own doing, and a gesture refused by the slide is
  not.
```

- [ ] **Step 4: Record the build under test in the spec**

In `docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md` §7.3, replace the line `**Not yet run.** Results are recorded here afterwards, as groups U through AA were.` with the build identity the checks will be run against, in the form group AA's results take in the box-resize spec:

```markdown
**Not yet run.** The build to run them against is the installed binary at
`build/install/release/bin/slideio-viewer.exe`, sha256 `<hash from Step 1>`,
commit `<HEAD at this commit>`. Results are recorded here afterwards, as groups
U through AA were — what was observed, not what was intended.

AB19 needs a slide whose annotations are not editable. The drawing tool actions
are disabled on such a slide, so the tool has to be picked first: select the
Point tool on an editable slide, then open the read-only one — the active tool
is deliberately not reset across an open. The cheapest way to produce a
read-only slide is the one group AA used: point the stored `slideId` in the
annotation file at a value matching no slide, and resolve the mismatch dialog
as Open Read-Only.
```

- [ ] **Step 5: Scan for placeholders**

Run: `grep -n "TBD\|TODO\|FIXME\|XXX" CLAUDE.md docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md`

Expected: no output, or only matches that predate this work and are unrelated to it.

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md
git commit -m "Record the point and arrow types and the build group AB runs against

The variant now has twelve compile-guarded dispatch sites. This round is the
first demonstration that withBoxReplaced's std::visit does what it was written
for: both new types were forced to answer nullopt rather than falling silently
through to 'not resizable'."
```

- [ ] **Step 7: Hand group AB to the project owner**

Group AB's 22 checks need a human at the viewer; no suite can reach any of them. Report the installed binary's hash and commit, say that group AB is not yet run, and name the three checks most likely to fail, so the owner knows where to look first:

- **AB9 / AB10** — the arrowhead's minimum rule at the extremes. The arithmetic is covered in core-tests, but whether the result *looks* right at a few pixels of shaft is a judgement only a human makes.
- **AB4** — the marker's grab target matching its drawn disc. The two come from one number by construction, but the construction is what is being checked.
- **AB19** — the read-only refusal, which needed a gate that did not exist and is therefore new code on a path no test reaches.

Also state plainly that every commit in this plan is AI-written and awaits human review, together with the seven box-resize commits `30b2a60..7fd262e` that are still outstanding.

---

## Notes for whoever executes this

- **Let the compiler enumerate the dispatch sites.** The plan says twelve, and the spec says not to trust the count. If the build names a site this plan does not list, that site is real and the plan is incomplete — fix it, and say so.
- **`readGeometry` is the one site the compiler will not find.** If Task 1's Step 10 does not fail in the way it predicts, something is wrong with the test rather than with the code: the whole point of that step is to watch the "writes files it cannot read" defect happen once, before it is fixed.
- **Nothing in Tasks 4 and 5 is testable.** Do not invent a test that constructs a widget; it will not link, and if it did it would crash without a `QApplication`. Those tasks are verified by reading and then by group AB.
