# Box Resize Handles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a pathologist resize a selected rectangle or ellipse by dragging one of eight handles, completing FR-ANN-17.

**Architecture:** Three pure functions in `core` operate on a plain `RectF` and so serve both box-shaped types without knowing which is which; a fourth writes the box back through the single `std::visit` site in this work. Handles are live on selection — no mode — and one pure `ui` function routes the press. The drag commits per frame, so all six of its exits funnel through one `recordBoxResize()`.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+, Conan 2, Catch2 v3, four ctest suites.

**Spec:** `docs/superpowers/specs/2026-10-17-annotation-box-resize-design.md` (committed `30b2a60`)

## Global Constraints

- Strict downward layering: `core` (Qt-free, links no Qt target) → `app` → `infra` → `ui`. Nothing in `core` may include a Qt header.
- **No test binary creates a `QApplication`, so no test may construct a widget.**
- **Only `core-tests` is safe as a bare `.exe`**, at `build/build/tests/Release/slideio-viewer-core-tests.exe`. The other three must go through `ctest` — running them directly pops a modal Windows box that blocks until a human dismisses it.
- **This plan creates new files**, so `src/core/CMakeLists.txt` and `tests/CMakeLists.txt` are edited and `cmake -S . -B build/build` must run. **Create each `.cpp` before adding it to a source list** — CMake's generate step requires every listed source to exist, and the vertex-editing run lost a step to exactly this.
- Naming: `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, 4-space indent, 120-char lines, `#pragma once`.
- A running viewer makes `cmake --install` a silent no-op that still exits 0. Verify by hash, never by exit code.
- All AI-generated code must be human-reviewed before commit.

## Review Focus

Input classes the spec implies that are most likely to bite, each pinned by a test in the task that owns it:

1. **An inverted stored box** — `topLeft` below and right of `bottomRight`, which a file can hold and which §6.4 deliberately produces mid-drag. Handle positions must come from the *normalised* box or every handle is in the wrong place. → Task 1.
2. **A press within tolerance of two handles**, which happens on any shape a few handle-widths across. Nearest must win, deterministically. → Task 1.
3. **A handle dragged past its opposite edge.** The intermediate `RectF` carries a negative extent by design; the geometry built from it must be a correctly inverted pair that `boundingBox` normalises, not a zero or absolute-valued box. → Tasks 2.
4. **A degenerate box** from a hand-edited file — zero extent on one axis, so several handles coincide. No NaN, no out-of-range, and `nearestBoxHandle` still answers. → Task 1.
5. **A polygon reaching the box path.** `withBoxReplaced` must return `nullopt`, and the press router must never offer handles for one. → Task 2 and Task 4.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/core/include/slideio/viewer/core/BoxResize.h` | **new** — `BoxHandle`, the four declarations | 1, 2 |
| `src/core/src/BoxResize.cpp` | **new** — their definitions | 1, 2 |
| `tests/core/BoxResizeTest.cpp` | **new** — all core cases | 1, 2 |
| `src/core/CMakeLists.txt` | add `src/BoxResize.cpp` | 1 |
| `tests/CMakeLists.txt` | add `core/BoxResizeTest.cpp` | 1 |
| `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` | `BoxResizeAction`, `resolveBoxResizePress`; `DragOwner::ResizeBox` | 3, 5 |
| `src/ui/src/AnnotationInteraction.cpp` | `resolveBoxResizePress` | 3 |
| `tests/ui/AnnotationInteractionTest.cpp` | routing cases | 3 |
| `src/ui/src/ViewportWidget.cpp` | handle painting (4); press routing, drag, six exits (5) | 4, 5 |
| `src/ui/include/slideio/viewer/ui/ViewportWidget.h` | `recordBoxResize`, `selectedBoxGeometry` | 4, 5 |
| `CLAUDE.md` | the no-mode decision and the six exits | 5 |

**On the three-value `BoxResizeAction`.** Spec §4 names `None`, `Resize` and `PassThrough`, and `None`/`PassThrough` are handled identically by the caller today — both fall through to `resolveDragOwner`. They are kept distinct because they answer different questions: `None` means *this press was never ours* (a middle-button pan, or a right-click that a future context menu will want), while `PassThrough` means *ours to consider and declined*. If a reviewer judges that distinction not worth an enumerator, collapsing to a `bool` is a safe later change; it is called out here so the redundancy is a decision rather than an oversight.

---

## Task 1: Handle positions and hit-testing

**Files:**
- Create: `src/core/src/BoxResize.cpp`
- Create: `src/core/include/slideio/viewer/core/BoxResize.h`
- Create: `tests/core/BoxResizeTest.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::RectF { double x, y, width, height; }`, `core::PointF { double x, y; }`.
- Produces:
  - `enum class BoxHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };`
  - `[[nodiscard]] std::array<PointF, 8> boxHandlePositions(const RectF& box);`
  - `[[nodiscard]] std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point, double toleranceSlideUnits);`

- [ ] **Step 1: Create the header**

Create `src/core/include/slideio/viewer/core/BoxResize.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <array>
#include <optional>

namespace slideio::viewer::core
{

/// The eight grab points of a box-shaped annotation: four corners and four
/// edge midpoints. A corner drags two edges, an edge midpoint drags one.
enum class BoxHandle
{
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
};

/// The eight handle centres of `box`, in the enumerator order above, so a
/// BoxHandle can index this array directly.
///
/// Positions are derived from the box as given. Callers hold the NORMALISED
/// box -- core::boundingBox of the annotation -- because a stored box may be
/// inverted, and handles computed from an inverted box would each sit on the
/// wrong side.
[[nodiscard]] std::array<PointF, 8> boxHandlePositions(const RectF& box);

/// The handle within `toleranceSlideUnits` of `point`; absent when none is in
/// range.
///
/// Nearest wins, and a tie goes to the earlier enumerator. On a shape only a
/// few handle-widths across, several handles are inside the tolerance at once
/// and the answer must not depend on iteration order changing later.
[[nodiscard]] std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point,
                                                        double toleranceSlideUnits);

} // namespace slideio::viewer::core
```

- [ ] **Step 2: Create the .cpp as an empty namespace**

Create `src/core/src/BoxResize.cpp`:

```cpp
#include "slideio/viewer/core/BoxResize.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

} // namespace slideio::viewer::core
```

**This step exists because of the next one.** CMake's generate step requires every listed source to exist; adding `src/BoxResize.cpp` to the source list before creating it fails with "CMake Generate step failed". Creating it empty keeps the red state honest — the header declares, the tests call, and the failure is an unresolved symbol rather than a broken build system.

- [ ] **Step 3: Write the failing tests**

Create `tests/core/BoxResizeTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/BoxResize.h"

#include <cmath>

using namespace slideio::viewer::core;

namespace
{

// x=10, y=20, width=100, height=40. Left 10, right 110, top 20, bottom 60,
// midX 60, midY 40.
RectF box()
{
    return RectF{10.0, 20.0, 100.0, 40.0};
}

std::size_t indexOf(BoxHandle handle)
{
    return static_cast<std::size_t>(handle);
}

} // namespace

TEST_CASE("boxHandlePositions puts all eight handles on the box", "[core][BoxResize]")
{
    const std::array<PointF, 8> handles = boxHandlePositions(box());

    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::Top)].x == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Top)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::TopRight)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::TopRight)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::Right)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::Right)].y == 40.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Bottom)].x == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Bottom)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomLeft)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomLeft)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Left)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::Left)].y == 40.0);
}

// Review Focus 1. A stored box can be inverted -- a file can hold one, and
// 6.4 produces one on every mid-drag flip. The caller is required to pass the
// NORMALISED box, so this pins that core::boundingBox and boxHandlePositions
// compose correctly for an inverted stored pair: the handles must land on the
// shape, not mirrored off it.
//
// Note this asserts against the SAME expected coordinates as the normalised
// case above. Comparing the two results to each other would pass whatever
// either produced.
TEST_CASE("an inverted stored box normalises to the same handles",
          "[core][BoxResize]")
{
    // The same rectangle as box(), stored bottom-right to top-left.
    const AnnotationGeometry inverted =
        RectangleGeometry{PointF{110.0, 60.0}, PointF{10.0, 20.0}};
    const std::array<PointF, 8> handles = boxHandlePositions(boundingBox(inverted));

    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Top)].x == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Left)].y == 40.0);
}

TEST_CASE("nearestBoxHandle finds a handle in tolerance", "[core][BoxResize]")
{
    const auto handle = nearestBoxHandle(box(), PointF{110.4, 60.0}, 1.0);
    REQUIRE(handle.has_value());
    REQUIRE(*handle == BoxHandle::BottomRight);
}

TEST_CASE("nearestBoxHandle is empty away from every handle", "[core][BoxResize]")
{
    REQUIRE_FALSE(nearestBoxHandle(box(), PointF{60.0, 40.0}, 1.0).has_value());
}

// Review Focus 2. On a small shape several handles are in range at once.
TEST_CASE("nearestBoxHandle returns the nearest of several in range",
          "[core][BoxResize]")
{
    const RectF small{0.0, 0.0, 4.0, 4.0};
    // Nearer TopRight (4,0) than TopLeft (0,0) or Top (2,0).
    const auto handle = nearestBoxHandle(small, PointF{3.6, 0.0}, 100.0);
    REQUIRE(handle.has_value());
    REQUIRE(*handle == BoxHandle::TopRight);
}

TEST_CASE("a negative tolerance behaves as zero", "[core][BoxResize]")
{
    REQUIRE(nearestBoxHandle(box(), PointF{10.0, 20.0}, -5.0).has_value());
    REQUIRE_FALSE(nearestBoxHandle(box(), PointF{10.5, 20.0}, -5.0).has_value());
}

// Review Focus 4. A hand-edited file can hold one. Several handles coincide
// and nothing may produce NaN or index out of range.
TEST_CASE("a degenerate box still answers", "[core][BoxResize]")
{
    const RectF flat{10.0, 20.0, 100.0, 0.0};
    const std::array<PointF, 8> handles = boxHandlePositions(flat);
    for (const PointF& handle : handles) {
        REQUIRE(std::isfinite(handle.x));
        REQUIRE(std::isfinite(handle.y));
        REQUIRE(handle.y == 20.0);
    }
    REQUIRE(nearestBoxHandle(flat, PointF{10.0, 20.0}, 1.0).has_value());

    const RectF point{5.0, 5.0, 0.0, 0.0};
    const auto onPoint = nearestBoxHandle(point, PointF{5.0, 5.0}, 1.0);
    REQUIRE(onPoint.has_value());
    // All eight coincide; the tie goes to the earliest enumerator.
    REQUIRE(*onPoint == BoxHandle::TopLeft);
}
```

- [ ] **Step 4: Add both files to CMake and reconfigure**

In `src/core/CMakeLists.txt`, after `src/VertexEditing.cpp`:

```cmake
    src/BoxResize.cpp
```

In `tests/CMakeLists.txt`, after `core/VertexEditingTest.cpp`:

```cmake
    core/BoxResizeTest.cpp
```

Run: `cmake -S . -B build/build`
Expected: "Configuring done" / "Generating done", no errors. Both files already exist from steps 2 and 3, so generate succeeds.

- [ ] **Step 5: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **link failure**, unresolved external symbols for `boxHandlePositions` and `nearestBoxHandle`. The header declares them and the empty `.cpp` does not define them — the C++ red state for this shape of change.

- [ ] **Step 6: Implement both functions**

Replace the body of `src/core/src/BoxResize.cpp`:

```cpp
#include "slideio/viewer/core/BoxResize.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

std::array<PointF, 8> boxHandlePositions(const RectF& box)
{
    const double left = box.x;
    const double right = box.x + box.width;
    const double top = box.y;
    const double bottom = box.y + box.height;
    const double midX = box.x + box.width * 0.5;
    const double midY = box.y + box.height * 0.5;

    // Enumerator order: clockwise from the top-left corner.
    return {PointF{left, top},     PointF{midX, top},    PointF{right, top},
            PointF{right, midY},   PointF{right, bottom}, PointF{midX, bottom},
            PointF{left, bottom},  PointF{left, midY}};
}

std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point,
                                          double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    const std::array<PointF, 8> handles = boxHandlePositions(box);

    std::optional<BoxHandle> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const double distance = std::hypot(point.x - handles[i].x, point.y - handles[i].y);
        if (distance > tolerance) {
            continue;
        }
        // Strictly nearer, so a tie keeps the earlier enumerator and a
        // degenerate box -- where all eight coincide -- answers TopLeft rather
        // than whichever the loop happened to reach last.
        if (!best.has_value() || distance < bestDistance) {
            best = static_cast<BoxHandle>(i);
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[BoxResize]"`

Expected: PASS, all cases, no warnings in the build output.

- [ ] **Step 8: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

- [ ] **Step 9: Commit**

```bash
git add src/core/include/slideio/viewer/core/BoxResize.h src/core/src/BoxResize.cpp \
        tests/core/BoxResizeTest.cpp src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Find the resize handle under a press"
```

---

## Task 2: Moving a handle, and writing the box back

**Files:**
- Modify: `src/core/include/slideio/viewer/core/BoxResize.h`
- Modify: `src/core/src/BoxResize.cpp`
- Test: `tests/core/BoxResizeTest.cpp`

**Interfaces:**
- Consumes: `BoxHandle`, `boxHandlePositions` from Task 1.
- Produces:
  - `[[nodiscard]] RectF withBoxHandleAt(const RectF& box, BoxHandle handle, PointF position);`
  - `[[nodiscard]] std::optional<AnnotationGeometry> withBoxReplaced(const AnnotationGeometry& geom, const RectF& box);`

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/BoxResizeTest.cpp`. Add `#include <variant>` to the include block.

```cpp
TEST_CASE("a corner handle moves two edges and leaves the opposite two",
          "[core][BoxResize]")
{
    const RectF moved = withBoxHandleAt(box(), BoxHandle::TopLeft, PointF{0.0, 0.0});
    REQUIRE(moved.x == 0.0);
    REQUIRE(moved.y == 0.0);
    // Right stayed at 110, bottom at 60.
    REQUIRE(moved.x + moved.width == 110.0);
    REQUIRE(moved.y + moved.height == 60.0);
}

TEST_CASE("every corner handle moves its own two edges", "[core][BoxResize]")
{
    const RectF topRight = withBoxHandleAt(box(), BoxHandle::TopRight, PointF{200.0, 0.0});
    REQUIRE(topRight.x == 10.0);
    REQUIRE(topRight.x + topRight.width == 200.0);
    REQUIRE(topRight.y == 0.0);
    REQUIRE(topRight.y + topRight.height == 60.0);

    const RectF bottomRight =
        withBoxHandleAt(box(), BoxHandle::BottomRight, PointF{200.0, 200.0});
    REQUIRE(bottomRight.x == 10.0);
    REQUIRE(bottomRight.y == 20.0);
    REQUIRE(bottomRight.x + bottomRight.width == 200.0);
    REQUIRE(bottomRight.y + bottomRight.height == 200.0);

    const RectF bottomLeft = withBoxHandleAt(box(), BoxHandle::BottomLeft, PointF{0.0, 200.0});
    REQUIRE(bottomLeft.x == 0.0);
    REQUIRE(bottomLeft.y == 20.0);
    REQUIRE(bottomLeft.x + bottomLeft.width == 110.0);
    REQUIRE(bottomLeft.y + bottomLeft.height == 200.0);
}

TEST_CASE("an edge handle moves one edge and leaves the other three",
          "[core][BoxResize]")
{
    const RectF top = withBoxHandleAt(box(), BoxHandle::Top, PointF{999.0, 0.0});
    // The x of the press is ignored: a top handle owns the top edge only.
    REQUIRE(top.x == 10.0);
    REQUIRE(top.x + top.width == 110.0);
    REQUIRE(top.y == 0.0);
    REQUIRE(top.y + top.height == 60.0);

    const RectF right = withBoxHandleAt(box(), BoxHandle::Right, PointF{200.0, 999.0});
    REQUIRE(right.y == 20.0);
    REQUIRE(right.y + right.height == 60.0);
    REQUIRE(right.x == 10.0);
    REQUIRE(right.x + right.width == 200.0);

    const RectF bottom = withBoxHandleAt(box(), BoxHandle::Bottom, PointF{999.0, 200.0});
    REQUIRE(bottom.y == 20.0);
    REQUIRE(bottom.y + bottom.height == 200.0);

    const RectF left = withBoxHandleAt(box(), BoxHandle::Left, PointF{0.0, 999.0});
    REQUIRE(left.x == 0.0);
    REQUIRE(left.x + left.width == 110.0);
}

// Review Focus 3, and spec 6.4. Dragging past the opposite edge is allowed and
// the intermediate RectF carries a NEGATIVE extent. It must not be clamped or
// absolute-valued here: the geometry built from it is an inverted pair, which
// core::boundingBox normalises.
TEST_CASE("a handle dragged past its opposite edge gives a negative extent",
          "[core][BoxResize]")
{
    const RectF flipped = withBoxHandleAt(box(), BoxHandle::Left, PointF{200.0, 0.0});
    REQUIRE(flipped.x == 200.0);
    REQUIRE(flipped.width == -90.0);
    // The right edge did not move.
    REQUIRE(flipped.x + flipped.width == 110.0);
}

TEST_CASE("withBoxReplaced keeps a rectangle a rectangle", "[core][BoxResize]")
{
    const AnnotationGeometry original =
        RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}};
    const auto replaced = withBoxReplaced(original, RectF{10.0, 20.0, 100.0, 40.0});

    REQUIRE(replaced.has_value());
    const auto* rectangle = std::get_if<RectangleGeometry>(&*replaced);
    REQUIRE(rectangle != nullptr);
    REQUIRE(rectangle->topLeft.x == 10.0);
    REQUIRE(rectangle->topLeft.y == 20.0);
    REQUIRE(rectangle->bottomRight.x == 110.0);
    REQUIRE(rectangle->bottomRight.y == 60.0);
}

TEST_CASE("withBoxReplaced keeps an ellipse an ellipse", "[core][BoxResize]")
{
    const AnnotationGeometry original = EllipseGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}};
    const auto replaced = withBoxReplaced(original, RectF{10.0, 20.0, 100.0, 40.0});

    REQUIRE(replaced.has_value());
    REQUIRE(std::get_if<EllipseGeometry>(&*replaced) != nullptr);
    REQUIRE(typeOf(*replaced) == AnnotationType::Ellipse);
}

// Review Focus 5. A traced shape is not box-defined, and scaling 400 vertices
// by a box drag is a different feature (spec 8).
TEST_CASE("withBoxReplaced refuses a polygon", "[core][BoxResize]")
{
    const AnnotationGeometry polygon = PolygonGeometry{
        {PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 10.0}}, true};
    REQUIRE_FALSE(withBoxReplaced(polygon, RectF{0.0, 0.0, 1.0, 1.0}).has_value());
}

// Review Focus 3, the other half: the negative extent must survive into the
// stored geometry as an inverted pair, and boundingBox must normalise it back.
TEST_CASE("a flipped box becomes an inverted pair that boundingBox normalises",
          "[core][BoxResize]")
{
    const AnnotationGeometry original =
        RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}};
    const RectF flipped = withBoxHandleAt(box(), BoxHandle::Left, PointF{200.0, 0.0});
    const auto replaced = withBoxReplaced(original, flipped);

    REQUIRE(replaced.has_value());
    const auto* rectangle = std::get_if<RectangleGeometry>(&*replaced);
    REQUIRE(rectangle != nullptr);
    REQUIRE(rectangle->topLeft.x == 200.0);
    REQUIRE(rectangle->bottomRight.x == 110.0);

    const RectF normalised = boundingBox(*replaced);
    REQUIRE(normalised.x == 110.0);
    REQUIRE(normalised.width == 90.0);
}

// Spec 7.1: "A resized rectangle and a resized ellipse each round-trip through
// the serializer unchanged." The resize can only build shapes the format
// accepts, and this is what holds it to that. Both types, because they are
// separate branches of withBoxReplaced and a wrong one would silently change
// an ellipse into a rectangle on disk.
TEST_CASE("a resized rectangle and ellipse round-trip through the serializer",
          "[core][BoxResize]")
{
    const RectF resized = withBoxHandleAt(box(), BoxHandle::BottomRight, PointF{200.0, 200.0});

    const std::vector<AnnotationGeometry> starts{
        RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
        EllipseGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
    };

    for (const AnnotationGeometry& start : starts) {
        const auto replaced = withBoxReplaced(start, resized);
        REQUIRE(replaced.has_value());

        AnnotationDocument document;
        document.slideId = "a3f8c2e1b4d50697";
        document.sceneIndex = 0;
        document.annotations.emplace_back("b1", *replaced);

        const ParseResult parsed = parseAnnotationDocument(serializeAnnotationDocument(document));
        REQUIRE(parsed.error == ParseError::None);
        REQUIRE(parsed.document.annotations.size() == 1);

        const AnnotationGeometry& readBack = parsed.document.annotations.front().geometry();
        REQUIRE(typeOf(readBack) == typeOf(*replaced));

        const RectF before = boundingBox(*replaced);
        const RectF after = boundingBox(readBack);
        REQUIRE(before.x == after.x);
        REQUIRE(before.y == after.y);
        REQUIRE(before.width == after.width);
        REQUIRE(before.height == after.height);
    }
}
```

Add `#include "slideio/viewer/core/AnnotationSerialization.h"` and `#include <vector>` to the test file's include block.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `withBoxHandleAt` and `withBoxReplaced` as undeclared.

- [ ] **Step 3: Declare both**

Append to `src/core/include/slideio/viewer/core/BoxResize.h`, before the closing namespace brace:

```cpp
/// `box` with `handle` moved to `position`. A corner moves two edges, an edge
/// midpoint moves one, and the opposite edges never move.
///
/// The result may carry a NEGATIVE extent, and deliberately is not clamped:
/// dragging a handle past its opposite edge flips the box and the resize
/// continues, which is the same freedom a creation drag has. The geometry
/// built from it is an inverted pair, and core::boundingBox normalises that.
/// Clamping or taking an absolute value here would make the shape stick
/// instead of flipping. Design spec 6.4.
[[nodiscard]] RectF withBoxHandleAt(const RectF& box, BoxHandle handle, PointF position);

/// `geom` rebuilt around `box`, preserving its type; absent for a shape that
/// is not box-defined.
///
/// Dispatches with std::visit onto an overload set, NOT a std::get_if chain.
/// A get_if chain would let a future box-shaped type fall silently through to
/// "not resizable" -- the shape of the readGeometry trap CLAUDE.md warns
/// about. With std::visit the compiler names the site until every shape
/// answers. This is the only place this work touches the variant.
[[nodiscard]] std::optional<AnnotationGeometry> withBoxReplaced(const AnnotationGeometry& geom,
                                                                const RectF& box);
```

- [ ] **Step 4: Implement both**

Append to `src/core/src/BoxResize.cpp`, before the closing namespace brace:

```cpp
namespace
{

std::optional<AnnotationGeometry> replaceBox(const RectangleGeometry&, const RectF& box)
{
    return RectangleGeometry{PointF{box.x, box.y},
                             PointF{box.x + box.width, box.y + box.height}};
}

std::optional<AnnotationGeometry> replaceBox(const EllipseGeometry&, const RectF& box)
{
    return EllipseGeometry{PointF{box.x, box.y},
                           PointF{box.x + box.width, box.y + box.height}};
}

// A traced shape is not defined by a box. Scaling its vertices is a different
// feature with different arithmetic -- design spec 8.
std::optional<AnnotationGeometry> replaceBox(const PolygonGeometry&, const RectF&)
{
    return std::nullopt;
}

} // namespace

RectF withBoxHandleAt(const RectF& box, BoxHandle handle, PointF position)
{
    double left = box.x;
    double right = box.x + box.width;
    double top = box.y;
    double bottom = box.y + box.height;

    switch (handle) {
    case BoxHandle::TopLeft:
        left = position.x;
        top = position.y;
        break;
    case BoxHandle::Top:
        top = position.y;
        break;
    case BoxHandle::TopRight:
        right = position.x;
        top = position.y;
        break;
    case BoxHandle::Right:
        right = position.x;
        break;
    case BoxHandle::BottomRight:
        right = position.x;
        bottom = position.y;
        break;
    case BoxHandle::Bottom:
        bottom = position.y;
        break;
    case BoxHandle::BottomLeft:
        left = position.x;
        bottom = position.y;
        break;
    case BoxHandle::Left:
        left = position.x;
        break;
    }

    // Extents may come out negative. See the header: that is the flip, not a
    // bug, and normalising here would stop the shape flipping at all.
    return RectF{left, top, right - left, bottom - top};
}

std::optional<AnnotationGeometry> withBoxReplaced(const AnnotationGeometry& geom, const RectF& box)
{
    return std::visit([&](const auto& shape) { return replaceBox(shape, box); }, geom);
}
```

Add `#include <variant>` to the file's include block.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[BoxResize]"`

Expected: PASS, all cases.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/BoxResize.h src/core/src/BoxResize.cpp \
        tests/core/BoxResizeTest.cpp
git commit -m "Move a box by one of its handles"
```

---

## Task 3: Routing a press

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`
- Modify: `src/ui/src/AnnotationInteraction.cpp`
- Test: `tests/ui/AnnotationInteractionTest.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks — it takes two booleans the caller computes.
- Produces:
  - `enum class BoxResizeAction { None, Resize, PassThrough };`
  - `[[nodiscard]] BoxResizeAction resolveBoxResizePress(Qt::MouseButton button, bool spaceHeld, bool hitHandle);`

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
TEST_CASE("a left press on a resize handle resizes", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::LeftButton, false, true) == BoxResizeAction::Resize);
}

TEST_CASE("a left press off the handles passes through",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::LeftButton, false, false)
            == BoxResizeAction::PassThrough);
}

// Space pans whatever else is happening. That promise predates every mode in
// this widget and was broken once already during vertex editing.
TEST_CASE("held space passes through even over a handle",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::LeftButton, true, true) == BoxResizeAction::PassThrough);
}

// Middle pans and right is reserved for a future context menu; neither is a
// resize, and neither may be swallowed.
TEST_CASE("a non-left press is not ours", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::MiddleButton, false, true) == BoxResizeAction::None);
    REQUIRE(resolveBoxResizePress(Qt::RightButton, false, true) == BoxResizeAction::None);
    REQUIRE(resolveBoxResizePress(Qt::MiddleButton, true, false) == BoxResizeAction::None);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `resolveBoxResizePress` and `BoxResizeAction` as undeclared.

- [ ] **Step 3: Declare the enum and the function**

In `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`, after the `VertexEditAction` enum:

```cpp
/// What a press means when a box-shaped annotation is selected.
///
/// `None` and `PassThrough` are both handled by falling through to
/// resolveDragOwner today, and are kept distinct because they answer different
/// questions: None means the press was never ours -- a middle-button pan, or a
/// right-click a future context menu will want -- while PassThrough means ours
/// to consider and declined.
enum class BoxResizeAction
{
    None,
    Resize,
    PassThrough,
};
```

After `resolveVertexEditPress`:

```cpp
/// Whether a press on a selected box-shaped annotation starts a resize.
///
/// `hitHandle` comes from core::nearestBoxHandle against the selected shape's
/// normalised bounding box. Tested BEFORE resolveDragOwner, because a corner
/// handle sits on the outline and would otherwise be taken as a move.
///
/// Pure, like every other routing predicate here, so the rules are covered
/// without a QApplication.
[[nodiscard]] BoxResizeAction resolveBoxResizePress(Qt::MouseButton button, bool spaceHeld,
                                                    bool hitHandle);
```

- [ ] **Step 4: Implement it**

In `src/ui/src/AnnotationInteraction.cpp`, after `resolveVertexEditPress`:

```cpp
BoxResizeAction resolveBoxResizePress(Qt::MouseButton button, bool spaceHeld, bool hitHandle)
{
    if (button != Qt::LeftButton) {
        return BoxResizeAction::None;
    }
    // Space pans whatever else is happening, including over a handle.
    if (spaceHeld) {
        return BoxResizeAction::PassThrough;
    }
    return hitHandle ? BoxResizeAction::Resize : BoxResizeAction::PassThrough;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`

Expected: PASS. **Do not run `slideio-viewer-ui-tests.exe` directly.**

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        src/ui/src/AnnotationInteraction.cpp tests/ui/AnnotationInteractionTest.cpp
git commit -m "Decide whether a press starts a box resize"
```

---

## Task 4: Painting the handles

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp`

**Interfaces:**
- Consumes: `core::boxHandlePositions`, `core::BoxHandle` (Task 1); `ui::kVertexHandleScreenPixels`.
- Produces: `ViewportWidget::Impl::selectedBoxGeometry()` returning `std::optional<core::RectF>`.

**What this task delivers:** a selected rectangle or ellipse shows eight handles. No resizing yet — Task 5. A reviewer can accept "the handles are there and in the right places" independently of "the drag works".

- [ ] **Step 1: Add the accessor**

In `src/ui/src/ViewportWidget.cpp`, in `Impl`, beside `vertexEditPolygon()`:

```cpp
    /// The normalised bounding box of the selected annotation when it is
    /// box-shaped, or absent when nothing is selected, the selection is a
    /// traced shape, or no model is attached.
    ///
    /// Normalised deliberately: a stored box may be inverted -- from a file or
    /// from a mid-drag flip -- and handles computed from an inverted box would
    /// each sit on the wrong side. core::boundingBox is the one place that
    /// normalisation lives.
    std::optional<core::RectF> selectedBoxGeometry() const
    {
        if (annotations == nullptr) {
            return std::nullopt;
        }
        const std::string& id = annotations->selectedId();
        if (id.empty()) {
            return std::nullopt;
        }
        const core::Annotation* annotation = annotations->find(id);
        if (annotation == nullptr) {
            return std::nullopt;
        }
        // withBoxReplaced is the authority on which shapes are box-defined;
        // asking it with the shape's own box keeps that judgement in one place
        // rather than repeating a type test here.
        if (!core::withBoxReplaced(annotation->geometry(),
                                   core::boundingBox(annotation->geometry()))
                 .has_value()) {
            return std::nullopt;
        }
        return core::boundingBox(annotation->geometry());
    }
```

Add `#include "slideio/viewer/core/BoxResize.h"` to the file's include block.

- [ ] **Step 2: Paint the handles**

In `ViewportWidget::paintGL`, immediately after the vertex-handle block, add:

```cpp
    // Resize handles for a selected box-shaped annotation. Live on selection,
    // with no mode: eight handles cannot bury a shape the way 400 vertex
    // handles could, which is the whole reason traced shapes need a mode and
    // these do not. Design spec 2.
    if (const std::optional<core::RectF> selectedBox = m_impl->selectedBoxGeometry()) {
        const double half = kVertexHandleScreenPixels * 0.5;
        const double margin = kHitToleranceScreenPixels + half;
        const QRectF viewportRect(-margin, -margin, static_cast<double>(width()) + 2.0 * margin,
                                  static_cast<double>(height()) + 2.0 * margin);
        painter.setPen(QPen(QColor(0x00, 0x00, 0x00), 1.0));
        painter.setBrush(QBrush(QColor(0xFF, 0xFF, 0xFF)));
        for (const core::PointF& handle : core::boxHandlePositions(*selectedBox)) {
            const QPointF centre = toScreen(handle);
            if (!viewportRect.contains(centre)) {
                continue;
            }
            // Two-tone, as the vertex handles and the selection outline are:
            // brightfield H&E glass and tissue are near-white and paintGL
            // clears the slide rect to white, so a white handle alone would
            // vanish on the commonest slide type.
            painter.drawRect(QRectF(centre.x() - half, centre.y() - half,
                                    kVertexHandleScreenPixels, kVertexHandleScreenPixels));
        }
    }
```

The cull margin matches the vertex handles': grown by the grab radius plus the handle's half-width, so every handle a press could reach is painted. Culling to the bare viewport left a handle grabbable but invisible, which review graded a real defect.

- [ ] **Step 3: Build and run the whole suite**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass, no warnings. Nothing here is reachable from a test; the suite is confirming nothing else broke.

- [ ] **Step 4: Install and verify by artifact**

Close any running viewer, then: `cmake --install build/build --config Release`

Verify: `sha256sum build/build/Release/slideio-viewer.exe build/install/release/bin/slideio-viewer.exe`
Expected: identical hashes. A running viewer makes the install a silent no-op that still exits 0, so the exit code proves nothing.

- [ ] **Step 5: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp
git commit -m "Show resize handles on a selected box"
```

---

## Task 5: The resize drag and its six exits

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` (`DragOwner::ResizeBox`)
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp`
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-10-17-annotation-box-resize-design.md`

**Interfaces:**
- Consumes: everything from Tasks 1–4, plus the existing `ViewportWidget::pushGeometryCommand`, `ui::moveEdit`, `ui::withMinimumExtent`, `ui::screenToleranceToSlide`, `AnnotationModel::restore`.
- Produces: nothing later tasks consume.

**Read spec §6 before starting.** The drag writes geometry to the model every mouse-move, so by the time any exit is reached the edit is already committed and autosave will write it. There are **six** exits and all of them must record. Vertex editing found them one review at a time; this task writes them all at once.

- [ ] **Step 1: Add the drag state and the enumerator**

In `AnnotationInteraction.h`, add to `DragOwner` after `MoveVertex`:

```cpp
    /// Dragging one handle of a selected box-shaped annotation.
    ResizeBox,
```

In `ViewportWidget.cpp`, in `Impl` beside the vertex drag members:

```cpp
    // Resizing a box-shaped annotation.
    std::optional<core::BoxHandle> resizingHandle;
    std::optional<core::Annotation> boxResizeOriginal;
    // The box as it was when the handle was picked up. Every frame resizes
    // THIS rather than the previous frame's output, so the drag cannot drift,
    // and the grab offset is not lost.
    core::RectF boxResizeAnchorBox;
    // The box this drag last wrote. If the shape is not where we left it,
    // something else rewrote the geometry mid-drag -- an undo is the reachable
    // case -- and the drag stops rather than resizing from a stale box.
    core::RectF boxResizeLastApplied;
```

- [ ] **Step 2: Add the recorder — the one place every exit funnels through**

In `ViewportWidget.h`, beside `recordVertexDrag`:

```cpp
    /// Pushes the undo step for a live box resize, if it changed anything, and
    /// clears the drag state. Safe to call with no resize live.
    ///
    /// Every route out of a resize goes through this: the ordinary release,
    /// focus loss, Escape, an undo taken mid-drag, a tool change and a slide
    /// close. Each leaves the resized geometry already committed to the model
    /// by the per-frame update, so a route that forgets to record produces an
    /// edit that is saved with no undo step — and the user's next Ctrl+Z then
    /// undoes something unrelated. Design spec 6.2.
    void recordBoxResize();
```

In `ViewportWidget.cpp`, beside `recordVertexDrag`:

```cpp
void ViewportWidget::recordBoxResize()
{
    if (m_impl->boxResizeOriginal.has_value()) {
        const std::string id = m_impl->boxResizeOriginal->id();
        const core::Annotation* now = m_impl->annotations->find(id);
        // moveEdit, not core::sameVertices: it compares bounding boxes, which
        // is EXACT for this gesture because a resize is precisely a change of
        // box. That is the opposite of a vertex drag, where an interior vertex
        // can move inside an unchanged box and moveEdit was the wrong test.
        if (now != nullptr && moveEdit(*m_impl->boxResizeOriginal, *now).has_value()) {
            pushGeometryCommand(id, *m_impl->boxResizeOriginal, tr("Resize"));
        }
    }
    m_impl->resizingHandle.reset();
    m_impl->boxResizeOriginal.reset();
}
```

- [ ] **Step 3: Route the press**

In `mousePressEvent`, immediately after the vertex-edit router block and **before** `resolveDragOwner`:

```cpp
    if (const std::optional<core::RectF> selectedBox =
            m_impl->controller == nullptr ? std::nullopt : m_impl->selectedBoxGeometry()) {
        const double tolerance = screenToleranceToSlide(kHitToleranceScreenPixels,
                                                        m_impl->controller->viewport().scale());
        const std::optional<core::BoxHandle> handle =
            core::nearestBoxHandle(*selectedBox, m_impl->movePressSlide, tolerance);

        if (resolveBoxResizePress(event->button(), m_impl->spaceHeld, handle.has_value())
            == BoxResizeAction::Resize) {
            // Refused on a read-only slide by the same gate that refuses a
            // move, and the user is told why. Selecting still works and the
            // handles stay painted -- hiding them would make a read-only slide
            // look like a different application. Design spec 4.1.
            if (!m_impl->annotationsEditable()) {
                emit annotationEditBlocked(readOnlyEditMessage());
            } else {
                m_impl->dragOwner = DragOwner::ResizeBox;
                m_impl->dragButton = event->button();
                m_impl->pressPos = event->pos();
                m_impl->resizingHandle = handle;
                m_impl->boxResizeAnchorBox = *selectedBox;
                m_impl->boxResizeLastApplied = *selectedBox;
                m_impl->boxResizeOriginal =
                    *m_impl->annotations->find(m_impl->annotations->selectedId());
                updateCursor();
                event->accept();
                return;
            }
            event->accept();
            return;
        }
    }
```

- [ ] **Step 4: The drag**

In `mouseMoveEvent`, beside the `MoveVertex` branch:

```cpp
    if (m_impl->dragOwner == DragOwner::ResizeBox && m_impl->resizingHandle.has_value()
        && m_impl->boxResizeOriginal.has_value() && m_impl->controller) {
        const std::string id = m_impl->boxResizeOriginal->id();
        const core::Annotation* current = m_impl->annotations->find(id);
        if (current != nullptr) {
            const core::RectF nowBox = core::boundingBox(current->geometry());
            // Something else rewrote the geometry since the last frame -- an
            // undo during a held drag is the reachable case. Carrying on would
            // resize from a box the user is no longer looking at.
            if (nowBox.x != m_impl->boxResizeLastApplied.x
                || nowBox.y != m_impl->boxResizeLastApplied.y
                || nowBox.width != m_impl->boxResizeLastApplied.width
                || nowBox.height != m_impl->boxResizeLastApplied.height) {
                m_impl->dragOwner = DragOwner::None;
                m_impl->dragButton = Qt::NoButton;
                m_impl->resizingHandle.reset();
                m_impl->boxResizeOriginal.reset();
                updateCursor();
                event->accept();
                update();
                return;
            }

            double slideX = 0.0;
            double slideY = 0.0;
            m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);
            // Resize the ANCHOR box, not the current one: every frame
            // transforms a pristine original rather than the previous frame's
            // output, the rule 5.2 of the geometry-widening spec sets.
            const core::RectF resized = core::withBoxHandleAt(
                m_impl->boxResizeAnchorBox, *m_impl->resizingHandle,
                core::PointF{slideX, slideY});
            if (const auto geometry = core::withBoxReplaced(current->geometry(), resized)) {
                m_impl->annotations->setGeometry(id, *geometry);
                m_impl->boxResizeLastApplied = core::boundingBox(*geometry);
                event->accept();
                update();
            }
        }
    }
```

- [ ] **Step 5: Exit 1 — the ordinary release**

In `mouseReleaseEvent`, beside the `MoveVertex` branch:

```cpp
    if (m_impl->dragOwner == DragOwner::ResizeBox) {
        // A collapsed axis is widened to one screen pixel, exactly as a newly
        // drawn shape's is, so a shape dragged to nothing comes back visible
        // rather than becoming an invisible line. Design spec 6.4.
        if (m_impl->boxResizeOriginal.has_value() && m_impl->controller) {
            const std::string id = m_impl->boxResizeOriginal->id();
            if (const core::Annotation* current = m_impl->annotations->find(id)) {
                const double scale = m_impl->controller->viewport().scale();
                const core::RectF widened = withMinimumExtent(
                    core::boundingBox(current->geometry()), screenToleranceToSlide(1.0, scale));
                if (const auto geometry = core::withBoxReplaced(current->geometry(), widened)) {
                    m_impl->annotations->setGeometry(id, *geometry);
                }
            }
        }
        recordBoxResize();
        m_impl->dragOwner = DragOwner::None;
        m_impl->dragButton = Qt::NoButton;
        updateCursor();
        event->accept();
        update();
        return;
    }
```

- [ ] **Step 6: Exits 2 to 6**

**Exit 2, focus loss.** In `focusOutEvent`, beside the `MoveVertex` recording branch:

```cpp
    // A resize abandoned by focus loss has already written the resized box
    // into the model frame by frame, so this is the last chance to record the
    // step: the teardown below clears dragOwner and the eventual release takes
    // mouseReleaseEvent's early return.
    if (m_impl->dragOwner == DragOwner::ResizeBox) {
        recordBoxResize();
    }
    m_impl->resizingHandle.reset();
    m_impl->boxResizeOriginal.reset();
```

**Exit 3, Escape.** In `keyPressEvent`'s Escape cascade, beside the vertex-drag branch:

```cpp
        } else if (m_impl->dragOwner == DragOwner::ResizeBox
                   && m_impl->boxResizeOriginal.has_value()) {
            // restore(), not setGeometry(): the whole annotation goes back,
            // metadata included. setGeometry restamps modifiedAt, which would
            // leave the file saying the mark was last touched by a gesture
            // whose entire purpose was to change nothing.
            m_impl->annotations->restore(*m_impl->boxResizeOriginal);
            m_impl->dragOwner = DragOwner::None;
            m_impl->dragButton = Qt::NoButton;
            m_impl->resizingHandle.reset();
            m_impl->boxResizeOriginal.reset();
            cancelled = true;
```

**Exit 4, undo mid-drag** is Step 4's changed-underneath check. No further work.

**Exit 5, tool change.** `endToolDrag()` returns early unless `dragOwner == Tool`, so it will not clean this up. In `setActiveTool`, beside the `endVertexEditing()` call:

```cpp
    if (m_impl->activeTool != tool && m_impl->dragOwner == DragOwner::ResizeBox) {
        recordBoxResize();
        m_impl->dragOwner = DragOwner::None;
        m_impl->dragButton = Qt::NoButton;
    }
```

Place this **before** `m_impl->activeTool = tool;` so the early-return guard at the top of the function still protects it.

**Exit 6, slide close.** In `resetAnnotationState()`, beside the vertex members:

```cpp
    // Holds a whole annotation from the slide being left, which an Escape
    // would otherwise restore onto the next one.
    m_impl->resizingHandle.reset();
    m_impl->boxResizeOriginal.reset();
```

- [ ] **Step 7: The cursor**

In `updateCursor()`'s `switch (m_impl->dragOwner)`, beside the `MoveVertex` case:

```cpp
    case DragOwner::ResizeBox:
        // The same cursor as a move and a vertex drag. Per-handle directional
        // cursors are out of scope -- design spec 8.
        setCursor(Qt::SizeAllCursor);
        return;
```

- [ ] **Step 8: Build, run the whole suite, install**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass, no warnings.

Close any running viewer, then `cmake --install build/build --config Release`, and verify the installed and built binaries hash identically.

- [ ] **Step 9: Record the documentation**

In `CLAUDE.md`, in the **Not built yet** paragraph, remove "resize handles for the box-shaped types, " so it begins "the point, arrow and text shapes". In **Key Design Decisions**, after the vertex-editing entry, add:

```markdown
- **Box resize handles are live on selection, with no mode.** A selected
  `RectangleGeometry` or `EllipseGeometry` paints eight handles — four corners,
  four edge midpoints — and a press on one starts a resize, tested *before*
  `resolveDragOwner` because a corner handle sits on the outline and would
  otherwise be taken as a move. No mode, deliberately: the vertex-editing mode
  exists only because 400 handles would bury a trace, and eight cannot. The
  core operations take a plain `RectF`, so the same three functions resize a
  rectangle and an ellipse; only `core::withBoxReplaced` touches the variant,
  and it uses `std::visit` rather than `get_if` so a future box-shaped type is
  a compile error rather than a silent "not resizable". Dragging a handle past
  its opposite edge flips the box, as a creation drag may, and
  `withMinimumExtent` widens a collapsed axis at commit. **Six exits record the
  undo step** — release, focus loss, Escape, undo mid-drag, tool change and
  slide close — all through `recordBoxResize()`; vertex editing found that list
  one review at a time and this one was written from it. See
  `docs/superpowers/specs/2026-10-17-annotation-box-resize-design.md`.
```

In the spec, under §7.3, add above the table:

```markdown
**Not yet run.** Group AA is the list to work through against the installed
build; results are recorded here afterwards, as groups U through Z were.
```

- [ ] **Step 10: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        src/ui/include/slideio/viewer/ui/ViewportWidget.h \
        src/ui/src/ViewportWidget.cpp CLAUDE.md \
        docs/superpowers/specs/2026-10-17-annotation-box-resize-design.md
git commit -m "Resize a box by dragging a handle"
```

---

## After the plan

Group AA in the spec is nineteen manual checks that no suite can reach. Run them against the installed build and record the results in the spec, as groups U through Z were.

**AA11 to AA14 are the four that matter most** — they walk four of the six exits by hand. On this feature the same class of defect (an edit committed with no undo step) was found by three separate reviews of the vertex-editing work, twice in code already fixed for it. The exits here were written from that list rather than discovered, which is the reason to check them rather than the reason to skip them.
