# Vertex Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a pathologist drag, insert and remove a single vertex of a polygon or freehand line without redrawing the outline.

**Architecture:** Five pure functions in `core` over `PolygonGeometry` directly — never over the `AnnotationGeometry` variant, which would fire ten compile-guarded sites for shapes with no answer. One pure routing function in `ui`. `ViewportWidget` gains a mode holding one annotation id; while it is set, handles are painted and presses route through that function. Undo reuses `AnnotationEditCommand` unchanged.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+, Conan 2, Catch2 v3, four ctest suites.

**Spec:** `docs/superpowers/specs/2026-10-17-annotation-vertex-editing-design.md` (committed `3a03fa6`)

## Global Constraints

- Strict downward layering: `core` (Qt-free, links no Qt target) → `app` → `infra` → `ui`. Nothing in `core` may include a Qt header.
- **No test binary creates a `QApplication`, so no test may construct a widget.**
- **Only `core-tests` is safe to run as a bare `.exe`**, and it lives at `build/build/tests/Release/slideio-viewer-core-tests.exe`. `app-tests`, `infra-tests` and `ui-tests` must go through `ctest` — running those `.exe` files pops a modal Windows error box that blocks until a human dismisses it.
- **This plan creates new files**, so `src/core/CMakeLists.txt` and `tests/CMakeLists.txt` are edited and `cmake -S . -B build/build` must be re-run. Task 1 does this.
- Naming: `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, 4-space indent, 120-char lines, `#pragma once`.
- A running viewer turns `cmake --install` into a silent no-op that still exits 0. Verify the installed artifact by hash, never by exit code.
- All AI-generated code must be human-reviewed before commit.

## Review Focus

Input classes the spec implies that are most likely to bite, each pinned by a test in the task that owns the code:

1. **A polygon still carrying a duplicate vertex** — every file saved before 2026-10-17 has one, and real ones exist on this machine. `outline()` drops duplicates, stored vertices keep them, so an outline index addresses the wrong point. Everything here must use `poly.vertices`. → Task 1, and manual check Z17.
2. **A press inside tolerance of both a vertex and an edge.** The vertex must win, or pressing a handle would insert a new vertex on top of it instead of dragging it. → Task 1.
3. **An empty or single-vertex polygon** from a hand-edited file. `nearestEdge` must not form a segment that does not exist, and neither function may index out of range. → Task 1.
4. **Removing down to exactly the floor, then once more.** Three for a closed shape, two for an open one — the same constants the serializer enforces, so the tool cannot produce a file the format rejects. → Task 2.
5. **Inserting on the closing segment of a closed shape**, where `afterIndex` is the last index and the new vertex belongs at the end. → Tasks 1 and 2.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/core/include/slideio/viewer/core/AnnotationGeometry.h` | `closestPointOnSegment` declaration | 1 |
| `src/core/src/AnnotationGeometry.cpp` | its definition; `distanceToSegment` refactored onto it | 1 |
| `src/core/include/slideio/viewer/core/VertexEditing.h` | **new** — `EdgeHit` and the five operations | 1, 2 |
| `src/core/src/VertexEditing.cpp` | **new** — their definitions | 1, 2 |
| `tests/core/VertexEditingTest.cpp` | **new** — all core cases | 1, 2 |
| `src/core/CMakeLists.txt` | add `src/VertexEditing.cpp` | 1 |
| `tests/CMakeLists.txt` | add `core/VertexEditingTest.cpp` | 1 |
| `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h` | `VertexEditAction`, `resolveVertexEditPress`, `kVertexHandleScreenPixels` | 3 |
| `src/ui/src/AnnotationInteraction.cpp` | its definition | 3 |
| `tests/ui/AnnotationInteractionTest.cpp` | routing cases | 3 |
| `src/ui/src/ViewportWidget.cpp` | mode state, entry, exit, handle painting | 4 |
| `src/ui/src/ViewportWidget.cpp` | press routing, drag, insert, remove, undo | 5 |
| `src/ui/include/slideio/viewer/ui/ViewportWidget.h` | new private helpers | 4, 5 |
| `CLAUDE.md` | the mode and the stored-index rule | 5 |

**Why `closestPointOnSegment` becomes public.** `nearestEdge` needs the projected point, not just the distance. `distanceToSegment` already computes that projection and throws it away, in an anonymous namespace where `VertexEditing.cpp` cannot reach it. Rather than duplicate eight lines of projection maths, the projection is exposed and `distanceToSegment` is rewritten in terms of it — one definition, two consumers.

---

## Task 1: `closestPointOnSegment` and the two query functions

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`
- Modify: `src/core/src/AnnotationGeometry.cpp`
- Create: `src/core/include/slideio/viewer/core/VertexEditing.h`
- Create: `src/core/src/VertexEditing.cpp`
- Create: `tests/core/VertexEditingTest.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::PolygonGeometry { std::vector<PointF> vertices; bool closed; }`, `core::PointF { double x, y; }`.
- Produces:
  - `[[nodiscard]] PointF closestPointOnSegment(PointF a, PointF b, PointF point);`
  - `struct EdgeHit { std::size_t afterIndex; PointF position; };`
  - `[[nodiscard]] std::optional<std::size_t> nearestVertex(const PolygonGeometry&, PointF, double toleranceSlideUnits);`
  - `[[nodiscard]] std::optional<EdgeHit> nearestEdge(const PolygonGeometry&, PointF, double toleranceSlideUnits);`

- [ ] **Step 1: Create the new header**

Create `src/core/include/slideio/viewer/core/VertexEditing.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <optional>

namespace slideio::viewer::core
{

/// Where an insertion on an edge belongs: the index of the segment's FIRST
/// vertex, and the point on that segment nearest the press. The new vertex
/// goes at afterIndex + 1.
struct EdgeHit
{
    std::size_t afterIndex;
    PointF position;
};

/// Index of the vertex within `toleranceSlideUnits` of `point`; absent when
/// none is in range.
///
/// When several are in range the NEAREST wins, and a tie goes to the lower
/// index. That is what makes a dense trace editable at any zoom without
/// hiding handles the user cannot then grab -- design spec 5.1.
///
/// Indexes `poly.vertices`, which is NOT core::outline(): outline() drops
/// consecutive duplicate vertices, so its indices do not address stored ones.
/// Every polygon saved before 2026-10-17 carries such a duplicate.
[[nodiscard]] std::optional<std::size_t> nearestVertex(const PolygonGeometry& poly, PointF point,
                                                       double toleranceSlideUnits);

/// The segment within `toleranceSlideUnits` of `point`, nearest first; absent
/// when the press is off the outline.
///
/// A closed shape's closing segment -- last vertex back to first -- is
/// included; an open one's is not, because it does not exist.
[[nodiscard]] std::optional<EdgeHit> nearestEdge(const PolygonGeometry& poly, PointF point,
                                                 double toleranceSlideUnits);

} // namespace slideio::viewer::core
```

- [ ] **Step 2: Write the failing tests**

Create `tests/core/VertexEditingTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/VertexEditing.h"

#include <cmath>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

// A unit square, closed. Vertices at (0,0) (10,0) (10,10) (0,10).
PolygonGeometry square()
{
    return PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{10.0, 10.0},
                            PointF{0.0, 10.0}},
                           true};
}

} // namespace

TEST_CASE("closestPointOnSegment projects onto the segment", "[core][VertexEditing]")
{
    const PointF p = closestPointOnSegment(PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{3.0, 5.0});
    REQUIRE(p.x == 3.0);
    REQUIRE(p.y == 0.0);
}

// Past either end it clamps to the endpoint rather than running off the line.
TEST_CASE("closestPointOnSegment clamps beyond the ends", "[core][VertexEditing]")
{
    const PointF before = closestPointOnSegment(PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                                PointF{-5.0, 1.0});
    REQUIRE(before.x == 0.0);
    const PointF after = closestPointOnSegment(PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                               PointF{15.0, 1.0});
    REQUIRE(after.x == 10.0);
}

TEST_CASE("closestPointOnSegment on a zero-length segment is its endpoint",
          "[core][VertexEditing]")
{
    const PointF p = closestPointOnSegment(PointF{4.0, 4.0}, PointF{4.0, 4.0}, PointF{9.0, 9.0});
    REQUIRE(p.x == 4.0);
    REQUIRE(p.y == 4.0);
}

TEST_CASE("nearestVertex finds a vertex in tolerance", "[core][VertexEditing]")
{
    const auto index = nearestVertex(square(), PointF{10.0, 10.4}, 1.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 2);
}

TEST_CASE("nearestVertex is empty when nothing is in range", "[core][VertexEditing]")
{
    REQUIRE_FALSE(nearestVertex(square(), PointF{5.0, 5.0}, 1.0).has_value());
}

// Review Focus 1 and design spec 5.1. Two vertices inside the tolerance: the
// nearer one wins, whichever order they appear in.
TEST_CASE("nearestVertex returns the nearest of several in range", "[core][VertexEditing]")
{
    const PolygonGeometry dense{
        {PointF{0.0, 0.0}, PointF{1.0, 0.0}, PointF{2.0, 0.0}, PointF{3.0, 0.0}}, true};
    const auto index = nearestVertex(dense, PointF{2.1, 0.0}, 5.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 2);
}

// Review Focus 1. A duplicate vertex is a STORED vertex and must be
// addressable; outline() would have dropped it and shifted every later index.
TEST_CASE("nearestVertex addresses stored indices, duplicates included",
          "[core][VertexEditing]")
{
    const PolygonGeometry withDuplicate{
        {PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{10.0, 10.0}, PointF{10.0, 10.0}}, true};
    REQUIRE(withDuplicate.vertices.size() == 4);
    const auto index = nearestVertex(withDuplicate, PointF{0.0, 0.1}, 1.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 0);
}

TEST_CASE("a negative tolerance behaves as zero", "[core][VertexEditing]")
{
    REQUIRE(nearestVertex(square(), PointF{0.0, 0.0}, -5.0).has_value());
    REQUIRE_FALSE(nearestVertex(square(), PointF{0.5, 0.0}, -5.0).has_value());
}

TEST_CASE("nearestEdge finds the segment and the projected point",
          "[core][VertexEditing]")
{
    const auto hit = nearestEdge(square(), PointF{5.0, 0.3}, 1.0);
    REQUIRE(hit.has_value());
    REQUIRE(hit->afterIndex == 0);
    REQUIRE(hit->position.x == 5.0);
    REQUIRE(hit->position.y == 0.0);
}

// Review Focus 5. The closing segment of a closed shape runs from the last
// vertex back to the first and is a real edge the user can insert on.
TEST_CASE("nearestEdge includes the closing segment of a closed shape",
          "[core][VertexEditing]")
{
    const auto hit = nearestEdge(square(), PointF{0.3, 5.0}, 1.0);
    REQUIRE(hit.has_value());
    REQUIRE(hit->afterIndex == 3);
}

TEST_CASE("nearestEdge excludes the closing segment of an open path",
          "[core][VertexEditing]")
{
    PolygonGeometry open = square();
    open.closed = false;
    REQUIRE_FALSE(nearestEdge(open, PointF{0.3, 5.0}, 1.0).has_value());
}

TEST_CASE("nearestEdge is empty off the outline", "[core][VertexEditing]")
{
    REQUIRE_FALSE(nearestEdge(square(), PointF{5.0, 5.0}, 1.0).has_value());
}

// Review Focus 3. A hand-edited file can hold these; neither function may
// index out of range or invent a segment.
TEST_CASE("degenerate polygons answer without indexing out of range",
          "[core][VertexEditing]")
{
    const PolygonGeometry empty{{}, true};
    REQUIRE_FALSE(nearestVertex(empty, PointF{0.0, 0.0}, 5.0).has_value());
    REQUIRE_FALSE(nearestEdge(empty, PointF{0.0, 0.0}, 5.0).has_value());

    const PolygonGeometry single{{PointF{1.0, 1.0}}, true};
    REQUIRE(nearestVertex(single, PointF{1.0, 1.0}, 5.0).has_value());
    REQUIRE_FALSE(nearestEdge(single, PointF{1.0, 1.0}, 5.0).has_value());
}

// Review Focus 2. A press on a handle is in range of the vertex AND of both
// segments meeting there. The caller asks nearestVertex first (design spec
// 3.4); this pins that it has an answer to give.
TEST_CASE("a press on a corner is in range of both a vertex and an edge",
          "[core][VertexEditing]")
{
    REQUIRE(nearestVertex(square(), PointF{10.0, 0.0}, 1.0).has_value());
    REQUIRE(nearestEdge(square(), PointF{10.0, 0.0}, 1.0).has_value());
}
```

- [ ] **Step 3: Add both files to CMake and reconfigure**

In `src/core/CMakeLists.txt`, add to `add_library(slideio-viewer-core STATIC ...)`, after `src/AnnotationMerge.cpp`:

```cmake
    src/VertexEditing.cpp
```

In `tests/CMakeLists.txt`, add to `add_executable(slideio-viewer-core-tests ...)`, after `core/AnnotationMergeTest.cpp`:

```cmake
    core/VertexEditingTest.cpp
```

Run: `cmake -S . -B build/build`
Expected: "Configuring done" / "Generating done", no errors. **This step is mandatory** — the source lists are explicit, so without it the new files are not compiled and the build will appear to pass while testing nothing.

- [ ] **Step 4: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `VertexEditing.cpp` as missing or `closestPointOnSegment`, `nearestVertex` and `nearestEdge` as undeclared. This is the C++ red state.

- [ ] **Step 5: Expose `closestPointOnSegment`**

In `src/core/include/slideio/viewer/core/AnnotationGeometry.h`, before `hasWellDefinedArea`:

```cpp
/// The point on segment `ab` nearest `point`, clamped to the segment rather
/// than its infinite line. A zero-length segment yields its own endpoint.
///
/// Public because core::nearestEdge needs the projection itself, not just the
/// distance to it, and duplicating the maths in two translation units is how
/// the two come to disagree.
[[nodiscard]] PointF closestPointOnSegment(PointF a, PointF b, PointF point);
```

In `src/core/src/AnnotationGeometry.cpp`, **remove** the anonymous-namespace `distanceToSegment` body's projection and rewrite it in terms of the new function, which is defined outside the anonymous namespace beside the other public ones:

```cpp
// in the anonymous namespace, replacing the existing body:
double distanceToSegment(PointF a, PointF b, PointF point)
{
    const PointF nearest = closestPointOnSegment(a, b, point);
    return std::hypot(point.x - nearest.x, point.y - nearest.y);
}
```

```cpp
// outside the anonymous namespace, beside boundingBox/hitTest/typeOf:
PointF closestPointOnSegment(PointF a, PointF b, PointF point)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0) {
        return a;
    }
    double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq;
    t = std::max(0.0, std::min(1.0, t));
    return PointF{a.x + t * dx, a.y + t * dy};
}
```

`distanceToSegment` is declared in the anonymous namespace above its first use, so add a forward declaration of `closestPointOnSegment` only if the compiler complains about ordering — it is declared in the header the file includes first, so it should not.

- [ ] **Step 6: Implement the query functions**

Create `src/core/src/VertexEditing.cpp`:

```cpp
#include "slideio/viewer/core/VertexEditing.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

std::optional<std::size_t> nearestVertex(const PolygonGeometry& poly, PointF point,
                                         double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    std::optional<std::size_t> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < poly.vertices.size(); ++i) {
        const PointF vertex = poly.vertices[i];
        const double distance = std::hypot(point.x - vertex.x, point.y - vertex.y);
        if (distance > tolerance) {
            continue;
        }
        // Strictly nearer, so a tie keeps the lower index and the answer does
        // not depend on iteration order changing later.
        if (!best.has_value() || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

std::optional<EdgeHit> nearestEdge(const PolygonGeometry& poly, PointF point,
                                   double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    const std::size_t count = poly.vertices.size();
    // Below two vertices there is no segment. An open path's last vertex
    // starts no segment, so it has one fewer than a closed one.
    if (count < 2) {
        return std::nullopt;
    }
    const std::size_t segmentCount = poly.closed ? count : count - 1;

    std::optional<EdgeHit> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const PointF a = poly.vertices[i];
        const PointF b = poly.vertices[(i + 1) % count];
        const PointF projected = closestPointOnSegment(a, b, point);
        const double distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance > tolerance) {
            continue;
        }
        if (!best.has_value() || distance < bestDistance) {
            best = EdgeHit{i, projected};
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[VertexEditing]"`

Expected: PASS, all cases, no warnings in the build output.

- [ ] **Step 8: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass. `distanceToSegment` was rewritten, so a failure in `[AnnotationGeometry]` hit-test cases means the refactor changed behaviour and must be fixed, not accommodated.

- [ ] **Step 9: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationGeometry.h \
        src/core/src/AnnotationGeometry.cpp \
        src/core/include/slideio/viewer/core/VertexEditing.h \
        src/core/src/VertexEditing.cpp \
        tests/core/VertexEditingTest.cpp \
        src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Find the vertex or edge under a press"
```

---

## Task 2: The three edit operations

**Files:**
- Modify: `src/core/include/slideio/viewer/core/VertexEditing.h`
- Modify: `src/core/src/VertexEditing.cpp`
- Test: `tests/core/VertexEditingTest.cpp`

**Interfaces:**
- Consumes: `EdgeHit`, `nearestVertex`, `nearestEdge` from Task 1; `core::kMinPolygonVertices` (3) and `core::kMinPolylineVertices` (2) from `AnnotationGeometry.h`.
- Produces:
  - `[[nodiscard]] PolygonGeometry withVertexMoved(const PolygonGeometry&, std::size_t index, PointF position);`
  - `[[nodiscard]] PolygonGeometry withVertexInserted(const PolygonGeometry&, std::size_t afterIndex, PointF position);`
  - `[[nodiscard]] std::optional<PolygonGeometry> withVertexRemoved(const PolygonGeometry&, std::size_t index);`
  - `[[nodiscard]] bool sameVertices(const PolygonGeometry& a, const PolygonGeometry& b);`

**Why `sameVertices` and not the existing `ui::moveEdit`.** Task 5 must decide whether a completed gesture changed anything, because one that did not must record no undo step. `moveEdit` already answers that for a *move* — but it compares **bounding boxes**, which its own doc calls "exact for the translation a move performs". A vertex drag is not a translation: dragging an interior vertex of a polygon can leave the bounding box identical while genuinely changing the shape. Reusing `moveEdit` here would silently drop real edits from the undo stack. The comparison vertex editing needs is over the vertices themselves.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/VertexEditingTest.cpp`:

```cpp
TEST_CASE("withVertexMoved moves the addressed vertex and no other",
          "[core][VertexEditing]")
{
    const PolygonGeometry moved = withVertexMoved(square(), 1, PointF{99.0, 98.0});
    REQUIRE(moved.vertices.size() == 4);
    REQUIRE(moved.vertices[0].x == 0.0);
    REQUIRE(moved.vertices[1].x == 99.0);
    REQUIRE(moved.vertices[1].y == 98.0);
    REQUIRE(moved.vertices[2].x == 10.0);
    REQUIRE(moved.closed);
}

TEST_CASE("withVertexMoved leaves an out-of-range index alone",
          "[core][VertexEditing]")
{
    const PolygonGeometry moved = withVertexMoved(square(), 99, PointF{1.0, 1.0});
    REQUIRE(moved.vertices.size() == 4);
    REQUIRE(moved.vertices[0].x == 0.0);
}

TEST_CASE("withVertexInserted puts the new vertex after the given index",
          "[core][VertexEditing]")
{
    const PolygonGeometry grown = withVertexInserted(square(), 0, PointF{5.0, 0.0});
    REQUIRE(grown.vertices.size() == 5);
    REQUIRE(grown.vertices[1].x == 5.0);
    REQUIRE(grown.vertices[1].y == 0.0);
    REQUIRE(grown.vertices[2].x == 10.0);
    REQUIRE(grown.closed);
}

// Review Focus 5. Inserting on the closing segment means appending: the new
// vertex sits between the old last and the first, which is exactly that edge.
TEST_CASE("withVertexInserted on the closing segment appends",
          "[core][VertexEditing]")
{
    const PolygonGeometry grown = withVertexInserted(square(), 3, PointF{0.0, 5.0});
    REQUIRE(grown.vertices.size() == 5);
    REQUIRE(grown.vertices[4].x == 0.0);
    REQUIRE(grown.vertices[4].y == 5.0);
}

TEST_CASE("withVertexRemoved removes the addressed vertex", "[core][VertexEditing]")
{
    const auto shrunk = withVertexRemoved(square(), 1);
    REQUIRE(shrunk.has_value());
    REQUIRE(shrunk->vertices.size() == 3);
    REQUIRE(shrunk->vertices[1].x == 10.0);
    REQUIRE(shrunk->vertices[1].y == 10.0);
    REQUIRE(shrunk->closed);
}

// Review Focus 4. The floor is the serializer's own minimum, so the tool
// cannot build a shape the file format would refuse on write or read.
TEST_CASE("withVertexRemoved refuses at the closed-shape floor",
          "[core][VertexEditing]")
{
    const PolygonGeometry triangle{
        {PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 10.0}}, true};
    REQUIRE(triangle.vertices.size() == kMinPolygonVertices);
    REQUIRE_FALSE(withVertexRemoved(triangle, 0).has_value());
}

TEST_CASE("withVertexRemoved refuses at the open-path floor", "[core][VertexEditing]")
{
    const PolygonGeometry line{{PointF{0.0, 0.0}, PointF{10.0, 0.0}}, false};
    REQUIRE(line.vertices.size() == kMinPolylineVertices);
    REQUIRE_FALSE(withVertexRemoved(line, 0).has_value());
}

// One above the floor still removes, which is what makes the refusal a floor
// rather than a blanket refusal.
TEST_CASE("withVertexRemoved allows the removal that reaches the floor",
          "[core][VertexEditing]")
{
    const auto shrunk = withVertexRemoved(square(), 0);
    REQUIRE(shrunk.has_value());
    REQUIRE(shrunk->vertices.size() == kMinPolygonVertices);
}

TEST_CASE("withVertexRemoved refuses an out-of-range index", "[core][VertexEditing]")
{
    REQUIRE_FALSE(withVertexRemoved(square(), 99).has_value());
}

// An open path has a lower floor than a closed one, so the same vertex count
// is removable in one and not the other.
TEST_CASE("the floor depends on whether the shape is closed",
          "[core][VertexEditing]")
{
    PolygonGeometry three{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 10.0}}, true};
    REQUIRE_FALSE(withVertexRemoved(three, 0).has_value());
    three.closed = false;
    REQUIRE(withVertexRemoved(three, 0).has_value());
}

TEST_CASE("sameVertices sees an identical shape as unchanged",
          "[core][VertexEditing]")
{
    REQUIRE(sameVertices(square(), square()));
}

// The case ui::moveEdit's bounding-box comparison would get WRONG: an
// interior vertex moves, the outer extent does not budge, and the edit is
// real. Recording nothing here would lose the user's work from the undo stack.
TEST_CASE("sameVertices sees an interior move that preserves the bounding box",
          "[core][VertexEditing]")
{
    const PolygonGeometry before{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 4.0},
                                  PointF{10.0, 10.0}, PointF{0.0, 10.0}},
                                 true};
    const PolygonGeometry after = withVertexMoved(before, 2, PointF{6.0, 5.0});

    const RectF boxBefore = boundingBox(before);
    const RectF boxAfter = boundingBox(after);
    REQUIRE(boxBefore.x == boxAfter.x);
    REQUIRE(boxBefore.y == boxAfter.y);
    REQUIRE(boxBefore.width == boxAfter.width);
    REQUIRE(boxBefore.height == boxAfter.height);

    REQUIRE_FALSE(sameVertices(before, after));
}

TEST_CASE("sameVertices distinguishes count, order and closedness",
          "[core][VertexEditing]")
{
    REQUIRE_FALSE(sameVertices(square(), withVertexRemoved(square(), 0).value()));
    REQUIRE_FALSE(sameVertices(square(), withVertexInserted(square(), 0, PointF{5.0, 0.0})));

    PolygonGeometry open = square();
    open.closed = false;
    REQUIRE_FALSE(sameVertices(square(), open));
}
```

Add `#include "slideio/viewer/core/AnnotationGeometry.h"` to the test file's include block if `boundingBox` is not already reachable — `VertexEditing.h` includes it, so it should be.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `withVertexMoved`, `withVertexInserted` and `withVertexRemoved` as undeclared.

- [ ] **Step 3: Declare them**

Append to `src/core/include/slideio/viewer/core/VertexEditing.h`, before the closing namespace brace:

```cpp
/// `poly` with vertex `index` at `position`. An index past the end returns the
/// polygon unchanged: the only caller passes an index nearestVertex produced,
/// so out of range means a bug upstream, and corrupting the shape would be a
/// worse answer than doing nothing.
[[nodiscard]] PolygonGeometry withVertexMoved(const PolygonGeometry& poly, std::size_t index,
                                              PointF position);

/// `poly` with a new vertex at `position`, placed at afterIndex + 1 so it lies
/// between the segment's endpoints. On the closing segment of a closed shape
/// afterIndex is the last index and the vertex is appended, which is the same
/// place.
[[nodiscard]] PolygonGeometry withVertexInserted(const PolygonGeometry& poly,
                                                 std::size_t afterIndex, PointF position);

/// `poly` without vertex `index`; absent when that would take the shape below
/// kMinPolygonVertices for a closed shape or kMinPolylineVertices for an open
/// one, or when the index is past the end.
///
/// Those are the same constants the serializer enforces on read and write, so
/// this cannot produce a shape the application would refuse to save or reopen
/// -- the rule ui::pendingShapeCommits already follows for the drawing tools.
[[nodiscard]] std::optional<PolygonGeometry> withVertexRemoved(const PolygonGeometry& poly,
                                                               std::size_t index);

/// True when `a` and `b` have the same vertices in the same order and the same
/// closed flag.
///
/// Exists because a completed gesture that changed nothing must record no undo
/// step, and ui::moveEdit cannot answer that here: it compares bounding boxes,
/// which is exact for a translation but blind to an interior vertex moving
/// inside an unchanged extent. PointF is an aggregate with no operator==, so
/// the coordinates are compared directly.
[[nodiscard]] bool sameVertices(const PolygonGeometry& a, const PolygonGeometry& b);
```

- [ ] **Step 4: Implement them**

Append to `src/core/src/VertexEditing.cpp`, before the closing namespace brace:

```cpp
PolygonGeometry withVertexMoved(const PolygonGeometry& poly, std::size_t index, PointF position)
{
    PolygonGeometry result = poly;
    if (index < result.vertices.size()) {
        result.vertices[index] = position;
    }
    return result;
}

PolygonGeometry withVertexInserted(const PolygonGeometry& poly, std::size_t afterIndex,
                                   PointF position)
{
    PolygonGeometry result = poly;
    const std::size_t at =
        afterIndex < result.vertices.size() ? afterIndex + 1 : result.vertices.size();
    result.vertices.insert(result.vertices.begin() + static_cast<std::ptrdiff_t>(at), position);
    return result;
}

std::optional<PolygonGeometry> withVertexRemoved(const PolygonGeometry& poly, std::size_t index)
{
    if (index >= poly.vertices.size()) {
        return std::nullopt;
    }
    const std::size_t minimum = poly.closed ? kMinPolygonVertices : kMinPolylineVertices;
    if (poly.vertices.size() <= minimum) {
        return std::nullopt;
    }
    PolygonGeometry result = poly;
    result.vertices.erase(result.vertices.begin() + static_cast<std::ptrdiff_t>(index));
    return result;
}

bool sameVertices(const PolygonGeometry& a, const PolygonGeometry& b)
{
    if (a.closed != b.closed || a.vertices.size() != b.vertices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (a.vertices[i].x != b.vertices[i].x || a.vertices[i].y != b.vertices[i].y) {
            return false;
        }
    }
    return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then
`build/build/tests/Release/slideio-viewer-core-tests.exe "[VertexEditing]"`

Expected: PASS, all cases.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/VertexEditing.h \
        src/core/src/VertexEditing.cpp tests/core/VertexEditingTest.cpp
git commit -m "Move, insert and remove a vertex"
```

---

## Task 3: Routing a press while editing

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`
- Modify: `src/ui/src/AnnotationInteraction.cpp`
- Test: `tests/ui/AnnotationInteractionTest.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks — it takes two booleans the caller computes from Task 1's functions.
- Produces:
  - `enum class VertexEditAction { None, DragVertex, InsertAndDrag, RemoveVertex, Exit };`
  - `[[nodiscard]] VertexEditAction resolveVertexEditPress(Qt::MouseButton, Qt::KeyboardModifiers, bool hitVertex, bool hitEdge);`
  - `constexpr double kVertexHandleScreenPixels = 8.0;`

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
TEST_CASE("a left press on a handle drags that vertex", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, true, false)
            == VertexEditAction::DragVertex);
}

// Design spec 3.4: a press on a handle is in range of the vertex AND of the
// two segments meeting there. The vertex wins, or pressing a handle would
// insert a new vertex on top of the one the user meant to grab.
TEST_CASE("a vertex hit beats an edge hit", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, true, true)
            == VertexEditAction::DragVertex);
}

TEST_CASE("ctrl and a handle removes that vertex", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ControlModifier, true, false)
            == VertexEditAction::RemoveVertex);
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ControlModifier, true, true)
            == VertexEditAction::RemoveVertex);
}

TEST_CASE("a left press on the outline inserts and drags",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, false, true)
            == VertexEditAction::InsertAndDrag);
    // Ctrl means "remove" only on a handle; on an edge there is nothing to
    // remove, so it does not suppress the insert.
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ControlModifier, false, true)
            == VertexEditAction::InsertAndDrag);
}

TEST_CASE("a left press on neither leaves the mode", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, false, false)
            == VertexEditAction::Exit);
}

// A middle or right press must not exit the mode: the middle button pans, and
// right is reserved for a future context menu.
TEST_CASE("a non-left press does nothing while editing",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::MiddleButton, Qt::NoModifier, true, false)
            == VertexEditAction::None);
    REQUIRE(resolveVertexEditPress(Qt::RightButton, Qt::NoModifier, false, false)
            == VertexEditAction::None);
}

// Shift and Alt have meanings while drawing; neither means anything here and
// neither may divert the gesture.
TEST_CASE("unrelated modifiers do not change the action",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ShiftModifier, true, false)
            == VertexEditAction::DragVertex);
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::AltModifier, false, true)
            == VertexEditAction::InsertAndDrag);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`

Expected: **compile failure**, naming `resolveVertexEditPress` and `VertexEditAction` as undeclared.

- [ ] **Step 3: Declare the enum, the constant and the function**

In `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`, after the `DragOwner` enum:

```cpp
/// What a press does while the viewport is in vertex-editing mode. Separate
/// from DragOwner: that routes drags in the ordinary viewport, and widening it
/// would make one set of rules serve two unrelated situations.
enum class VertexEditAction
{
    None,
    DragVertex,
    InsertAndDrag,
    RemoveVertex,
    Exit,
};
```

Beside `kHitToleranceScreenPixels`:

```cpp
/// Side length of a vertex handle, in screen pixels, so handles stay the same
/// size to grab at 1x and at 40x. Smaller than the grab diameter on purpose:
/// the target is easier to hit than to see precisely, which suits a gesture
/// aimed with a mouse.
constexpr double kVertexHandleScreenPixels = 8.0;
```

After `shouldPlaceClickVertex`:

```cpp
/// What a press means while vertex editing, given what it landed on.
///
/// `hitVertex` and `hitEdge` come from core::nearestVertex and core::nearestEdge
/// against the annotation being edited. Both can be true at once -- a press on
/// a handle is in range of the two segments meeting there -- and the vertex
/// wins, because the user aimed at something they can see.
///
/// Pure, like every other routing predicate here, so the rules are covered
/// without a QApplication.
[[nodiscard]] VertexEditAction resolveVertexEditPress(Qt::MouseButton button,
                                                      Qt::KeyboardModifiers modifiers,
                                                      bool hitVertex, bool hitEdge);
```

- [ ] **Step 4: Implement it**

In `src/ui/src/AnnotationInteraction.cpp`, after `shouldPlaceClickVertex`:

```cpp
VertexEditAction resolveVertexEditPress(Qt::MouseButton button, Qt::KeyboardModifiers modifiers,
                                        bool hitVertex, bool hitEdge)
{
    // Middle still pans and right is reserved for a context menu; neither may
    // end the mode by accident.
    if (button != Qt::LeftButton) {
        return VertexEditAction::None;
    }
    if (hitVertex) {
        return modifiers.testFlag(Qt::ControlModifier) ? VertexEditAction::RemoveVertex
                                                       : VertexEditAction::DragVertex;
    }
    if (hitEdge) {
        return VertexEditAction::InsertAndDrag;
    }
    return VertexEditAction::Exit;
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
git commit -m "Decide what a press means while editing vertices"
```

---

## Task 4: The mode and its handles

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp`

**Interfaces:**
- Consumes: `core::PolygonGeometry`, `ui::kVertexHandleScreenPixels` from Task 3.
- Produces: `ViewportWidget::Impl::vertexEditId` (a `std::string`), `Impl::vertexEditPolygon()` returning `const core::PolygonGeometry*`, and `ViewportWidget::endVertexEditing()`.

**What this task delivers:** double-clicking a traced shape shows handles on every vertex, and the mode ends on the exits the spec names. No editing yet — that is Task 5. A reviewer can accept or reject "the mode exists and is visible" independently of "the gestures work".

- [ ] **Step 1: Add the mode state and its accessor**

In `src/ui/src/ViewportWidget.cpp`, in `Impl`, beside `drawAnchorSlide`:

```cpp
    // Vertex editing: the annotation whose handles are shown and whose
    // vertices presses address. Empty when not editing. Design spec 2.
    std::string vertexEditId;

    /// The polygon being vertex-edited, or null when the mode is off, the
    /// annotation is gone, or it is not a traced shape.
    ///
    /// Returns the STORED geometry. Callers must not substitute core::outline():
    /// it drops consecutive duplicate vertices, so its indices do not address
    /// stored ones, and every polygon saved before 2026-10-17 has such a
    /// duplicate. Design spec 3.2.
    const core::PolygonGeometry* vertexEditPolygon() const
    {
        if (vertexEditId.empty() || annotations == nullptr) {
            return nullptr;
        }
        const core::Annotation* annotation = annotations->find(vertexEditId);
        if (annotation == nullptr) {
            return nullptr;
        }
        return std::get_if<core::PolygonGeometry>(&annotation->geometry());
    }
```

Add `#include "slideio/viewer/core/VertexEditing.h"` and `#include <variant>` to the file's include block if absent.

- [ ] **Step 2: Add the exit helper**

In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, beside `pushCreateCommand`:

```cpp
    /// Leaves vertex-editing mode and repaints. Safe to call when not editing.
    void endVertexEditing();
```

In `src/ui/src/ViewportWidget.cpp`, beside `pushCreateCommand`:

```cpp
void ViewportWidget::endVertexEditing()
{
    if (m_impl->vertexEditId.empty()) {
        return;
    }
    m_impl->vertexEditId.clear();
    update();
}
```

- [ ] **Step 3: Enter the mode on double-click**

In `ViewportWidget::mouseDoubleClickEvent`, the existing body returns early unless `m_impl->pending.active` and the button is left. Replace the guard so a double-click with no pending shape can enter the mode instead of being ignored:

```cpp
void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }

    // A pending trace takes the gesture: double-click closes it. The two never
    // contend, because entering vertex editing applies only to a COMMITTED
    // annotation and pending.active is true only while a trace is unfinished.
    // Design spec 2.2.
    if (m_impl->pending.active) {
        // Qt sends MouseButtonDblClick in place of the second press, not in
        // addition to it -- QWidget's default implementation forwards it to
        // mousePressEvent for exactly that reason. This override does not
        // forward, so the pair has appended exactly one vertex, placed by its
        // first click, and that vertex is the one the user double-clicked to
        // position. Removing it would drop a real point and, on a triangle,
        // take the shape below kMinPolygonVertices so it was discarded in
        // silence.
        commitPendingShape();
        updateCursor();
        event->accept();
        return;
    }

    // Entering is declined silently for a shape with no vertices to edit, and
    // on a slide whose annotations are not editable -- the same gate that
    // blocks a move drag. Offering an edit that would then be refused is worse
    // than not offering it. Design spec 2.
    if (!m_impl->annotationsEditable() || m_impl->controller == nullptr) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }

    double slideX = 0.0;
    double slideY = 0.0;
    m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);
    const double tolerance =
        screenToleranceToSlide(kHitToleranceScreenPixels, m_impl->controller->viewport().scale());
    const std::string hit = m_impl->annotations->hitTest(core::PointF{slideX, slideY}, tolerance);
    if (hit.empty()) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }
    const core::Annotation* annotation = m_impl->annotations->find(hit);
    if (annotation == nullptr
        || std::get_if<core::PolygonGeometry>(&annotation->geometry()) == nullptr) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }

    m_impl->annotations->setSelected(hit);
    m_impl->vertexEditId = hit;
    updateCursor();
    event->accept();
    update();
}
```

`AnnotationModel::hitTest(core::PointF point, double toleranceSlideUnits)` returns the id of the annotation under a point, or an empty string — declared at `src/app/include/slideio/viewer/app/AnnotationModel.h:95`. It is the same call the press handler uses, so "what is under the cursor" keeps one answer.

- [ ] **Step 4: Paint the handles**

In `ViewportWidget::paintGL`, immediately after the selection-outline block, add:

```cpp
    // Vertex handles, painted from the STORED vertices -- not core::outline(),
    // which drops duplicates and would therefore paint a handle whose index
    // does not address the vertex a press will move. Design spec 3.2.
    if (const core::PolygonGeometry* editing = m_impl->vertexEditPolygon()) {
        const double half = kVertexHandleScreenPixels * 0.5;
        const QRectF viewportRect(0.0, 0.0, static_cast<double>(width()),
                                  static_cast<double>(height()));
        for (const core::PointF& vertex : editing->vertices) {
            const QPointF centre = toScreen(vertex);
            // Culled to what is on screen: a trace can run far outside the
            // viewport and those handles cannot be grabbed anyway.
            if (!viewportRect.contains(centre)) {
                continue;
            }
            const QRectF handle(centre.x() - half, centre.y() - half,
                                kVertexHandleScreenPixels, kVertexHandleScreenPixels);
            // Two-tone for the same reason the selection outline is: brightfield
            // H&E glass and tissue are near-white, and paintGL clears the slide
            // rect to white, so a white handle alone would vanish on the
            // commonest slide type.
            painter.setPen(QPen(QColor(0x00, 0x00, 0x00), 1.0));
            painter.setBrush(QBrush(QColor(0xFF, 0xFF, 0xFF)));
            painter.drawRect(handle);
        }
    }
```

- [ ] **Step 5: Leave the mode on every exit the spec names**

Two call sites, and only two:

**`ViewportWidget::resetAnnotationState()`** — the single place per-slide annotation state is cleared, reached from both `closeSlide()` paths. Add beside `m_impl->movingId.clear()`:

```cpp
    // The mode names an annotation belonging to the slide being left. Kept, it
    // would paint handles over a different slide's tissue and address indices
    // into a shape that is gone -- the same reason moveOriginal is reset here.
    m_impl->vertexEditId.clear();
```

Clear the member directly rather than calling `endVertexEditing()`: that helper calls `update()`, and this function runs mid-teardown where the other resets deliberately do not repaint.

**`ViewportWidget::setActiveTool(AnnotationTool)`** — add after the existing `endToolDrag()` call:

```cpp
    endVertexEditing();
```

The other two exits the spec names need no work here: **Escape** is Task 5 Step 5, and **selecting a different annotation** already happens through a press that lands on neither a handle nor the outline, which `resolveVertexEditPress` answers with `Exit` and Task 5 Step 2 acts on. No `selectionChanged` hook is needed, and adding one would give that exit two owners.

- [ ] **Step 6: Build and run the whole suite**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass, no warnings in the build output. Nothing here is reachable from a test; the suite is confirming nothing else broke.

- [ ] **Step 7: Install and verify by artifact**

Close any running viewer, then run: `cmake --install build/build --config Release`

Verify: `sha256sum build/build/Release/slideio-viewer.exe build/install/release/bin/slideio-viewer.exe`
Expected: identical hashes. A running viewer makes the install a silent no-op that still exits 0, so the exit code proves nothing.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp
git commit -m "Show vertex handles on a double-clicked trace"
```

---

## Task 5: The gestures

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp`
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-10-17-annotation-vertex-editing-design.md`

**Interfaces:**
- Consumes: `core::nearestVertex`, `core::nearestEdge`, `core::withVertexMoved`, `core::withVertexInserted`, `core::withVertexRemoved`, `core::EdgeHit` (Task 1 and 2); `ui::resolveVertexEditPress`, `ui::VertexEditAction` (Task 3); `Impl::vertexEditPolygon()`, `ViewportWidget::endVertexEditing()` (Task 4).
- Produces: nothing later tasks consume.

- [ ] **Step 1: Add the drag state**

In `Impl`, beside `vertexEditId`:

```cpp
    // The vertex being dragged, and the whole annotation as it was when the
    // drag began. The original is what Escape restores and what the undo step
    // records as "before" -- the same press-captures, release-records shape
    // the move drag uses.
    std::optional<std::size_t> draggingVertex;
    std::optional<core::Annotation> vertexDragOriginal;
```

Add `DragOwner::MoveVertex` to the enum in `AnnotationInteraction.h`, so the existing drag plumbing can tell a vertex drag from a shape move.

- [ ] **Step 2: Route the press**

In `mousePressEvent`, **before** the existing drawing-tool and move-annotation branches, add:

```cpp
    if (const core::PolygonGeometry* editing = m_impl->vertexEditPolygon()) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);
        const core::PointF cursor{slideX, slideY};
        const double tolerance = screenToleranceToSlide(kHitToleranceScreenPixels,
                                                        m_impl->controller->viewport().scale());

        // Vertex first, edge only if no vertex answered: a press on a handle is
        // in range of the segments meeting there too. Design spec 3.4.
        const auto vertex = core::nearestVertex(*editing, cursor, tolerance);
        const auto edge = vertex.has_value()
                              ? std::nullopt
                              : core::nearestEdge(*editing, cursor, tolerance);

        switch (resolveVertexEditPress(event->button(), event->modifiers(), vertex.has_value(),
                                       edge.has_value())) {
        case VertexEditAction::DragVertex:
            m_impl->dragOwner = DragOwner::MoveVertex;
            m_impl->dragButton = event->button();
            m_impl->draggingVertex = vertex;
            m_impl->vertexDragOriginal = *m_impl->annotations->find(m_impl->vertexEditId);
            event->accept();
            return;
        case VertexEditAction::InsertAndDrag: {
            m_impl->vertexDragOriginal = *m_impl->annotations->find(m_impl->vertexEditId);
            m_impl->annotations->setGeometry(
                m_impl->vertexEditId,
                core::withVertexInserted(*editing, edge->afterIndex, edge->position));
            m_impl->dragOwner = DragOwner::MoveVertex;
            m_impl->dragButton = event->button();
            // The inserted vertex is the one now being dragged.
            m_impl->draggingVertex = edge->afterIndex + 1;
            event->accept();
            update();
            return;
        }
        case VertexEditAction::RemoveVertex: {
            const auto shrunk = core::withVertexRemoved(*editing, *vertex);
            if (!shrunk.has_value()) {
                const std::size_t minimum =
                    editing->closed ? core::kMinPolygonVertices : core::kMinPolylineVertices;
                emit annotationEditBlocked(
                    tr("This shape cannot have fewer than %1 points.").arg(minimum));
                event->accept();
                return;
            }
            const core::Annotation before = *m_impl->annotations->find(m_impl->vertexEditId);
            m_impl->annotations->setGeometry(m_impl->vertexEditId, *shrunk);
            pushGeometryCommand(m_impl->vertexEditId, before, tr("Remove Point"));
            event->accept();
            update();
            return;
        }
        case VertexEditAction::Exit:
            endVertexEditing();
            break;  // fall through to the ordinary press handling below
        case VertexEditAction::None:
            break;
        }
    }
```

`annotationEditBlocked` already exists as a signal and `MainWindow` already routes it to `StatusBarManager::showTransientMessage` — no new signal or wiring is needed.

- [ ] **Step 3: Add the undo helper**

In `ViewportWidget.h` beside `pushCreateCommand`:

```cpp
    /// Records one completed geometry edit: the annotation as it was, against
    /// what it is now. One per gesture, never one per mouse-move -- a single
    /// nudge would otherwise bury fifty steps of history and Ctrl+Z would
    /// appear not to work.
    void pushGeometryCommand(const std::string& id, const core::Annotation& before,
                             const QString& description);
```

In `ViewportWidget.cpp`, modelled on the existing `pushMoveCommand`:

```cpp
void ViewportWidget::pushGeometryCommand(const std::string& id, const core::Annotation& before,
                                         const QString& description)
{
    if (m_impl->undoStack == nullptr || id.empty()) {
        return;
    }
    const core::Annotation* after = m_impl->annotations->find(id);
    if (after == nullptr) {
        return;
    }
    m_impl->undoStack->push(app::AnnotationEditCommand::create(
        *m_impl->annotations, id, before, *after, description.toStdString()));
}
```

This mirrors `pushMoveCommand` (`ViewportWidget.cpp`, around line 3050) in shape — null stack, empty id and missing annotation all return early — with one deliberate difference: it does **not** call `ui::moveEdit`. That helper decides "did anything change" by comparing bounding boxes, which is exact for a translation and wrong for a vertex edit, where an interior point can move inside an unchanged extent. The caller makes that decision with `core::sameVertices` instead (Step 4), so by the time this is called the edit is already known to be real.

- [ ] **Step 4: Drag and release**

In `mouseMoveEvent`, beside the `DragOwner::MoveAnnotation` branch:

```cpp
    if (m_impl->dragOwner == DragOwner::MoveVertex && m_impl->draggingVertex.has_value()
        && m_impl->controller) {
        if (const core::PolygonGeometry* editing = m_impl->vertexEditPolygon()) {
            double slideX = 0.0;
            double slideY = 0.0;
            m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX,
                                              slideY);
            m_impl->annotations->setGeometry(
                m_impl->vertexEditId,
                core::withVertexMoved(*editing, *m_impl->draggingVertex,
                                      core::PointF{slideX, slideY}));
            event->accept();
            update();
        }
    }
```

In `mouseReleaseEvent`, beside the `DragOwner::MoveAnnotation` branch:

```cpp
    if (m_impl->dragOwner == DragOwner::MoveVertex) {
        // A gesture that changed nothing records nothing: touching the model
        // would mark the slide dirty and restamp modifiedAt for an edit that
        // did not happen, and the autosave would then write exactly that.
        if (m_impl->vertexDragOriginal.has_value()) {
            const auto* was =
                std::get_if<core::PolygonGeometry>(&m_impl->vertexDragOriginal->geometry());
            const core::PolygonGeometry* now = m_impl->vertexEditPolygon();
            // sameVertices, not ui::moveEdit: that compares bounding boxes,
            // which an interior vertex can move without changing.
            if (was != nullptr && now != nullptr && !core::sameVertices(*was, *now)) {
                pushGeometryCommand(m_impl->vertexEditId, *m_impl->vertexDragOriginal,
                                    tr("Edit Points"));
            }
        }
        m_impl->dragOwner = DragOwner::None;
        m_impl->dragButton = Qt::NoButton;
        m_impl->draggingVertex.reset();
        m_impl->vertexDragOriginal.reset();
        updateCursor();
        event->accept();
        update();
        return;
    }
```

`core::AnnotationGeometry` has no `operator==` and none is added: `core::sameVertices` from Task 2 answers exactly the question this needs, and giving the whole variant an equality operator would be a core change this task does not own.

- [ ] **Step 5: Escape**

In `keyPressEvent`'s Escape cascade, insert a branch beside the `MoveAnnotation` one, **before** the general exit:

```cpp
        } else if (m_impl->dragOwner == DragOwner::MoveVertex
                   && m_impl->vertexDragOriginal.has_value()) {
            // restore(), not setGeometry(): the whole annotation goes back,
            // metadata included. setGeometry restamps modifiedAt, which would
            // leave the file saying the mark was last touched by a gesture
            // whose entire purpose was to change nothing -- and the autosave
            // that follows would write exactly that.
            m_impl->annotations->restore(*m_impl->vertexDragOriginal);
            m_impl->dragOwner = DragOwner::None;
            m_impl->dragButton = Qt::NoButton;
            m_impl->draggingVertex.reset();
            m_impl->vertexDragOriginal.reset();
            cancelled = true;
        } else if (!m_impl->vertexEditId.empty()) {
            endVertexEditing();
            cancelled = true;
```

- [ ] **Step 6: Build, run the whole suite, install**

Run: `cmake --build build/build --config Release` then
`ctest --test-dir build/build -C Release --output-on-failure`

Expected: 4/4 suites pass.

Close any running viewer, then `cmake --install build/build --config Release`, and verify the installed and built binaries hash identically.

- [ ] **Step 7: Record the documentation**

In `CLAUDE.md`, in the **Not built yet** paragraph, remove "vertex editing and" so it reads "the point, arrow and text shapes". In **Key Design Decisions**, after the ellipse entry, add:

```markdown
- **Vertex editing is a mode, and it addresses STORED vertices.** Double-clicking
  a committed polygon or freehand line enters it; Escape, a press off the shape,
  a tool change or a new selection leaves it. While it is on, `ViewportWidget`
  paints a handle per vertex and routes presses through
  `ui::resolveVertexEditPress`. Every operation — `core::nearestVertex`,
  `nearestEdge`, `withVertexMoved`, `withVertexInserted`, `withVertexRemoved` —
  indexes `poly.vertices` and **never** `core::outline()`, which drops
  consecutive duplicates and so does not address stored vertices; every polygon
  saved before 2026-10-17 carries such a duplicate. The functions take
  `PolygonGeometry` directly rather than the variant, which is why adding this
  fired none of the ten dispatch sites. Removal refuses below the serializer's
  own minimum, so the tool cannot build a shape the format would reject. See
  `docs/superpowers/specs/2026-10-17-annotation-vertex-editing-design.md`.
```

In the spec, under §7.3, add above the table:

```markdown
**Not yet run.** Group Z is the list to work through against the installed
build; results are recorded here afterwards, as groups U through Y were.
```

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp \
        src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        CLAUDE.md docs/superpowers/specs/2026-10-17-annotation-vertex-editing-design.md
git commit -m "Drag, insert and remove a vertex"
```

---

## After the plan

Group Z in the spec is seventeen manual checks that no suite can reach. Run them against the installed build and record the results in the spec, as groups U through Y were.

**Z17 needs a polygon saved before 2026-10-17** — one that still carries a duplicate final vertex. `D:/tmp/third/annotations/ec83fc2ebbfbc515.s0.annotations.json` held one at index 159 as of this morning; if it has since been overwritten, any pre-fix file will do, or one can be made by hand-editing a vertex list to repeat its last entry. That check is the only thing standing between a correct implementation and one that silently moves the wrong point on every annotation drawn before today.
