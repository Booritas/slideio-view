# Annotation Measurement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the area and perimeter of the selected annotation in physical units, and the length of an open freehand trace, derived from geometry and never stored.

**Architecture:** Three pure functions in `core` do all the arithmetic, so the whole feature is covered by `core-tests` — the only suite that needs no `QApplication` and the only one safe to run directly. The annotation properties panel gains two read-only rows and a translation-invariant cache. Nothing is written to the annotation file, so the serializer and `schemaVersion` are untouched.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+, Conan 2, Catch2 v3, four ctest suites.

**Spec:** `docs/superpowers/specs/2026-10-14-annotation-measurement-design.md` (committed `7d25943`)

## Global Constraints

- **`core` is Qt-free and links no Qt target.** A Qt include or Qt type anywhere under `src/core/` is a Critical defect. This is what makes the measurement arithmetic testable.
- **No test binary creates a `QApplication`, so no test may construct a widget.** The panel work in Task 4 has no automated test by design; it is verified by the manual group W list in the spec.
- **Every `.cpp` and every test file is listed explicitly in CMake** — `src/core/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`. The only glob is `file(GLOB_RECURSE _ui_headers ...)` in `src/ui/CMakeLists.txt`, which exists so AUTOMOC finds `Q_OBJECT` headers; it does not pick up `.cpp` files. A file left unregistered fails to link with an error naming the symbol, not the cause.
- **Never run `app-tests`, `infra-tests` or `ui-tests` as a bare `.exe`** — it pops a modal Windows dialog that blocks until a human dismisses it. Always go through `ctest`. Only `core-tests` is safe to run directly.
- **A running viewer turns `cmake --install` into a silent no-op that still exits 0.** Kill the viewer first and verify by comparing timestamps of `build/install/release/bin/slideio-viewer.exe` and `build/build/Release/slideio-viewer.exe`.
- **Units:** µm below 1 mm, mm at or above; µm² below 1 mm², mm² at or above. Three significant figures. Locale-free UTF-8, **no thousands separators**.
- **Conventions:** 4-space indent, 120-char lines, Allman braces for classes/functions, K&R for control flow, `#pragma once`, `m_camelCase` members, `kPascalCase` constants, include order own-header → project → Qt → std.
- **Workflow:** direct commits to `main` (unprotected); no PRs, no feature branches. All AI-generated code must be human-reviewed before commit.

## Review Focus

Five conditions the spec implies but does not pin. Each gets a test in the task that owns the code.

1. **A degenerate shape** — every vertex identical, or a zero-extent rectangle. Expect area 0 and length 0, never NaN or a crash. A pathologist can produce one with a stray click-drag of two pixels. *(Task 2)*
2. **An inverted rectangle** — built from a drag that went up and left, so `bottomRight` is above `topLeft`. Expect a positive area, because `outline()` normalises the box and the measurement must match what is painted. *(Task 2)*
3. **A non-positive `PixelSize`** — `resolutionX` is 0 on an uncalibrated slide, and a caller could pass it straight through. Expect 0, not NaN or infinity. *(Task 2)*
4. **A value exactly at a unit threshold, from below** — 999.6 µm rounds to 1000 at three significant figures, which must display as `1.00 mm` rather than `1000 µm`. Rounding and unit choice have to agree. *(Task 3)*
5. **A whole-slide-sized area** — a polygon covering 15 mm × 15 mm is 2.25e-4 m². Expect `225 mm²`, never scientific notation. *(Task 3)*

---

## File Structure

**Create:**
- `src/core/include/slideio/viewer/core/AnnotationMeasurement.h` — `PixelSize`, `ShapeMetrics`, `MeasurementUnits`, and all five function declarations
- `src/core/src/AnnotationMeasurement.cpp`
- `tests/core/AnnotationMeasurementTest.cpp`

**Modify:**
- `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/ui/CMakeLists.txt` (no new ui `.cpp`, so only if needed — it is not; listed here for the reviewer's benefit as "unchanged")
- `src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h`
- `src/ui/src/AnnotationPropertiesPanel.cpp`
- `src/ui/src/MainWindow.cpp`

One header and one source in `core` hold all five functions. They are one responsibility — turning a shape into numbers a person can read — and splitting them across files would separate `measure` from the formatter that renders its output.

---

### Task 1: `isSelfIntersecting`

The detector comes first because `measure` depends on it.

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationMeasurement.h`
- Create: `src/core/src/AnnotationMeasurement.cpp`
- Create: `tests/core/AnnotationMeasurementTest.cpp`
- Modify: `src/core/CMakeLists.txt` (add `src/AnnotationMeasurement.cpp` after `src/AnnotationGeometry.cpp`)
- Modify: `tests/CMakeLists.txt` (add `core/AnnotationMeasurementTest.cpp` to `slideio-viewer-core-tests`)

**Interfaces:**
- Consumes: `core::PointF` (`Types.h:341`, fields `x`, `y`), `core::Outline` (`AnnotationGeometry.h`, fields `std::vector<PointF> points` and `bool closed`).
- Produces: `bool core::isSelfIntersecting(const Outline& outline);`

**A deviation from the spec, deliberate.** The spec writes this as `isSelfIntersecting(const PolygonGeometry&)`. It takes an `Outline` instead, because `core::outline(geom)` is already the single place that turns any geometry into points-plus-closed, and a rectangle's outline is its *normalised* corners — the same box `hitTest` uses. Working from `Outline` means the measurement, the painted region and the hit-tested region are all derived from one source, which is exactly the agreement the spec demands. It also means no new exhaustive `std::visit` site is added that a future shape type could be forgotten in.

- [ ] **Step 1: Write the failing tests**

Create `tests/core/AnnotationMeasurementTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <vector>

using namespace slideio::viewer;

namespace
{

core::Outline closedOutline(std::vector<core::PointF> points)
{
    return core::Outline{std::move(points), true};
}

core::Outline openOutline(std::vector<core::PointF> points)
{
    return core::Outline{std::move(points), false};
}

} // namespace

TEST_CASE("a simple convex polygon does not self-intersect", "[core][AnnotationMeasurement]")
{
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}})));
}

TEST_CASE("a simple concave polygon does not self-intersect", "[core][AnnotationMeasurement]")
{
    // An arrowhead: concave at the notch, but no edge crosses another.
    REQUIRE_FALSE(core::isSelfIntersecting(
        closedOutline({{0.0, 0.0}, {10.0, 5.0}, {0.0, 10.0}, {3.0, 5.0}})));
}

TEST_CASE("a figure-eight self-intersects", "[core][AnnotationMeasurement]")
{
    // The second and fourth edges cross in the middle.
    REQUIRE(core::isSelfIntersecting(
        closedOutline({{0.0, 0.0}, {10.0, 10.0}, {10.0, 0.0}, {0.0, 10.0}})));
}

TEST_CASE("a crossing created only by the closing edge is detected",
          "[core][AnnotationMeasurement]")
{
    // These four points form a simple open path. Joining the last back to the
    // first is what crosses an earlier edge -- so a detector that forgets the
    // closing edge passes the open case and fails this one.
    const std::vector<core::PointF> points{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {5.0, -5.0}};
    REQUIRE(core::isSelfIntersecting(closedOutline(points)));
    REQUIRE_FALSE(core::isSelfIntersecting(openOutline(points)));
}

TEST_CASE("segments sharing an endpoint are not an intersection",
          "[core][AnnotationMeasurement]")
{
    // Every polygon's consecutive edges touch. If that counted, nothing would
    // ever be simple.
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}, {5.0, 0.0}, {5.0, 5.0}})));
}

TEST_CASE("collinear overlapping segments count as self-intersecting",
          "[core][AnnotationMeasurement]")
{
    // A trace that doubles back exactly along itself. Its interior is not
    // well-defined, so declining to measure it is the honest answer.
    REQUIRE(core::isSelfIntersecting(
        openOutline({{0.0, 0.0}, {10.0, 0.0}, {4.0, 0.0}, {12.0, 0.0}})));
}

TEST_CASE("a shape with too few points cannot self-intersect",
          "[core][AnnotationMeasurement]")
{
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({})));
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}})));
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}, {5.0, 5.0}})));
}
```

- [ ] **Step 2: Register both files, then run to verify failure**

In `src/core/CMakeLists.txt`, after `src/AnnotationGeometry.cpp`:

```cmake
    src/AnnotationMeasurement.cpp
```

In `tests/CMakeLists.txt`, in the `slideio-viewer-core-tests` list after `core/AnnotationGeometryTest.cpp`:

```cmake
    core/AnnotationMeasurementTest.cpp
```

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```

Expected: `Cannot open include file: 'slideio/viewer/core/AnnotationMeasurement.h'`.

- [ ] **Step 3: Write the header**

Create `src/core/include/slideio/viewer/core/AnnotationMeasurement.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <optional>
#include <string>

namespace slideio::viewer::core
{

/// True when any two non-adjacent segments of `outline` cross, or when two
/// segments overlap along a line.
///
/// Takes an Outline rather than a geometry because outline() is already the one
/// place that turns any shape into points-plus-closed, and a rectangle's
/// outline is its *normalised* corners -- the same box hitTest uses. Deriving
/// the measurement, the painted region and the hit-tested region from one
/// source is what keeps them from disagreeing.
///
/// Segments meeting at a shared endpoint are NOT an intersection: consecutive
/// edges of every polygon touch, so counting that would make nothing simple.
/// Collinear overlap IS: a trace doubling back along itself has no
/// well-defined interior.
///
/// When `outline.closed` is true the closing edge is included in the test.
///
/// O(n^2) in the point count. Callers measuring long freehand traces should
/// note that an open path's area is absent regardless, so there is no reason to
/// ask this question about one.
[[nodiscard]] bool isSelfIntersecting(const Outline& outline);

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Write the implementation**

Create `src/core/src/AnnotationMeasurement.cpp`:

```cpp
#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace slideio::viewer::core
{

namespace
{

/// Cross product of (b - a) and (c - a). Sign gives which side of ab c lies on;
/// zero means the three are collinear.
double cross(PointF a, PointF b, PointF c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/// True when c lies within the axis-aligned box spanned by a and b. Only
/// meaningful once the three are known to be collinear.
bool withinSpan(PointF a, PointF b, PointF c)
{
    return std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x)
           && std::min(a.y, b.y) <= c.y && c.y <= std::max(a.y, b.y);
}

/// `shape` with every vertex scaled into physical units.
///
/// A negative scale would mirror the shape and a NaN would poison every sum.
/// Neither is a calibration, so both collapse to zero -- resolutionX is 0 on an
/// uncalibrated slide and a caller may pass it straight through.
Outline scaledOutline(const Outline& shape, PixelSize pixelSize)
{
    const double sx = pixelSize.x > 0.0 ? pixelSize.x : 0.0;
    const double sy = pixelSize.y > 0.0 ? pixelSize.y : 0.0;

    Outline scaled;
    scaled.closed = shape.closed;
    scaled.points.reserve(shape.points.size());
    for (const PointF& p : shape.points) {
        scaled.points.push_back(PointF{p.x * sx, p.y * sy});
    }
    return scaled;
}

/// True when segments ab and cd share any point. Endpoints count, so callers
/// must exclude adjacent pairs themselves.
bool segmentsIntersect(PointF a, PointF b, PointF c, PointF d)
{
    const double d1 = cross(c, d, a);
    const double d2 = cross(c, d, b);
    const double d3 = cross(a, b, c);
    const double d4 = cross(a, b, d);

    // Strict straddle on both sides: a proper crossing.
    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0))
        && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
        return true;
    }

    // Collinear touching or overlap. This is the case that makes a trace
    // doubling back along itself count as self-intersecting.
    if (d1 == 0.0 && withinSpan(c, d, a)) {
        return true;
    }
    if (d2 == 0.0 && withinSpan(c, d, b)) {
        return true;
    }
    if (d3 == 0.0 && withinSpan(a, b, c)) {
        return true;
    }
    if (d4 == 0.0 && withinSpan(a, b, d)) {
        return true;
    }
    return false;
}

} // namespace

bool isSelfIntersecting(const Outline& outline)
{
    const std::vector<PointF>& p = outline.points;
    const std::size_t n = p.size();

    // A closed shape needs 3 points before it has two non-adjacent edges; an
    // open one needs 4. Below that there is nothing to cross.
    if (n < 3) {
        return false;
    }

    const std::size_t segmentCount = outline.closed ? n : n - 1;

    for (std::size_t i = 0; i < segmentCount; ++i) {
        const PointF a = p[i];
        const PointF b = p[(i + 1) % n];
        for (std::size_t j = i + 1; j < segmentCount; ++j) {
            // Adjacent segments share an endpoint by construction. For a closed
            // shape the last segment is also adjacent to the first.
            const bool adjacent = (j == i + 1) || (outline.closed && i == 0 && j == segmentCount - 1);
            if (adjacent) {
                continue;
            }
            const PointF c = p[j];
            const PointF d = p[(j + 1) % n];
            if (segmentsIntersect(a, b, c, d)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS. If "collinear overlapping segments" fails, check that `withinSpan` is reached — a detector written only with the strict-straddle test will pass every other case and fail that one.

- [ ] **Step 6: Verify the tests can fail**

Temporarily change `if (n < 3)` to `if (n < 3000)` so the function always returns false. Rebuild, run, and confirm the figure-eight, closing-edge and collinear tests fail. Restore the line and confirm green again. Report what you saw — a test that has never been seen to fail is not known to work.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationMeasurement.h src/core/src/AnnotationMeasurement.cpp tests/core/AnnotationMeasurementTest.cpp src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add core::isSelfIntersecting for annotation outlines"
```

---

### Task 2: `measure`

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationMeasurement.h`
- Modify: `src/core/src/AnnotationMeasurement.cpp`
- Modify: `tests/core/AnnotationMeasurementTest.cpp` (append)

**Interfaces:**
- Consumes: `core::isSelfIntersecting(const Outline&)` (Task 1); `core::outline(const AnnotationGeometry&)` which returns `Outline{points, closed}`; `core::RectangleGeometry` with fields `topLeft` and `bottomRight` of type `PointF`; `core::PolygonGeometry` with `std::vector<PointF> vertices` and `bool closed`.
- Produces:
  - `struct core::PixelSize { double x = 1.0; double y = 1.0; };`
  - `struct core::ShapeMetrics { std::optional<double> area; double outlineLength = 0.0; bool closed = false; bool selfIntersecting = false; };`
  - `core::ShapeMetrics core::measure(const AnnotationGeometry& geometry, PixelSize pixelSize);`
  - `core::ShapeMetrics core::measureKnown(const AnnotationGeometry& geometry, PixelSize pixelSize, bool selfIntersecting);` — the same work with the expensive question already answered. Task 4's panel needs it, but it belongs here with the rest of the measurement arithmetic rather than being introduced inside a ui task.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/AnnotationMeasurementTest.cpp`:

```cpp
namespace
{

/// RectangleGeometry stores two CORNERS, not an origin plus a size.
core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

core::AnnotationGeometry polygon(std::vector<core::PointF> vertices, bool closed)
{
    core::PolygonGeometry geometry;
    geometry.vertices = std::move(vertices);
    geometry.closed = closed;
    return geometry;
}

} // namespace

TEST_CASE("a rectangle measures its area and perimeter", "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m = core::measure(rect(0.0, 0.0, 10.0, 20.0), core::PixelSize{});
    REQUIRE(m.closed);
    REQUIRE_FALSE(m.selfIntersecting);
    REQUIRE(m.area.has_value());
    REQUIRE(*m.area == 200.0);
    REQUIRE(m.outlineLength == 60.0);
}

TEST_CASE("an inverted rectangle still measures positive", "[core][AnnotationMeasurement]")
{
    // Review Focus 2. A drag that went up and left puts bottomRight above
    // topLeft. outline() normalises the box, so the measurement matches the
    // shape that is actually painted rather than reporting a negative area.
    const core::ShapeMetrics m = core::measure(rect(10.0, 20.0, 0.0, 0.0), core::PixelSize{});
    REQUIRE(*m.area == 200.0);
    REQUIRE(m.outlineLength == 60.0);
}

TEST_CASE("anisotropic pixels scale area by the product", "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m = core::measure(rect(0.0, 0.0, 10.0, 20.0), core::PixelSize{2.0, 3.0});
    REQUIRE(*m.area == 1200.0);        // (10*2) * (20*3)
    REQUIRE(m.outlineLength == 160.0); // 2 * (20 + 60)
}

TEST_CASE("a diagonal under anisotropic pixels is not scaled by one factor",
          "[core][AnnotationMeasurement]")
{
    // THE test for the API's shape. A 45-degree segment from (0,0) to (10,10)
    // under PixelSize{1,2} becomes (0,0)->(10,20), whose length is
    // sqrt(100 + 400) = 22.360..., which is neither 14.142 (unscaled) nor
    // 28.284 (scaled by 2). Any implementation that measures in pixels and
    // multiplies afterwards gets this wrong.
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 10.0}}, false), core::PixelSize{1.0, 2.0});
    REQUIRE_FALSE(m.closed);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE(std::abs(m.outlineLength - std::sqrt(500.0)) < 1e-9);
}

TEST_CASE("a closed triangle measures by the shoelace formula",
          "[core][AnnotationMeasurement]")
{
    // Base 10, height 10, so area 50. Perimeter 10 + sqrt(200) + 10.
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}, true), core::PixelSize{});
    REQUIRE(*m.area == 50.0);
    REQUIRE(std::abs(m.outlineLength - (10.0 + std::sqrt(200.0) + 10.0)) < 1e-9);
}

TEST_CASE("an open polyline has no area and excludes the closing edge",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}, false), core::PixelSize{});
    REQUIRE_FALSE(m.closed);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE(m.outlineLength == 20.0); // not 20 + sqrt(200)
}

TEST_CASE("a self-intersecting polygon reports no area but a real perimeter",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 10.0}, {10.0, 0.0}, {0.0, 10.0}}, true),
                      core::PixelSize{});
    REQUIRE(m.selfIntersecting);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE(m.outlineLength > 0.0);
}

TEST_CASE("an open trace is never tested for self-intersection",
          "[core][AnnotationMeasurement]")
{
    // The same crossing points, left open. Area is absent either way, so the
    // O(n^2) test is skipped -- and open freehand traces are exactly the shapes
    // carrying thousands of vertices. selfIntersecting therefore means "this is
    // why the area is missing", not "this shape is geometrically simple".
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 10.0}, {10.0, 0.0}, {0.0, 10.0}}, false),
                      core::PixelSize{});
    REQUIRE_FALSE(m.selfIntersecting);
    REQUIRE_FALSE(m.area.has_value());
}

TEST_CASE("a degenerate shape measures zero, not NaN", "[core][AnnotationMeasurement]")
{
    // Review Focus 1. A stray two-pixel drag, or a trace whose points coincide.
    const core::ShapeMetrics zeroRect = core::measure(rect(5.0, 5.0, 5.0, 5.0), core::PixelSize{});
    REQUIRE(*zeroRect.area == 0.0);
    REQUIRE(zeroRect.outlineLength == 0.0);

    const core::ShapeMetrics coincident =
        core::measure(polygon({{3.0, 3.0}, {3.0, 3.0}, {3.0, 3.0}}, true), core::PixelSize{});
    REQUIRE(coincident.outlineLength == 0.0);
    REQUIRE(std::isfinite(coincident.outlineLength));
    if (coincident.area.has_value()) {
        REQUIRE(*coincident.area == 0.0);
    }
}

TEST_CASE("a non-positive pixel size yields zero, not NaN or infinity",
          "[core][AnnotationMeasurement]")
{
    // Review Focus 3. resolutionX is 0 on an uncalibrated slide and a caller
    // could pass it straight through. Zero is a defensible answer; NaN is not.
    const core::ShapeMetrics m = core::measure(rect(0.0, 0.0, 10.0, 20.0), core::PixelSize{0.0, 0.0});
    REQUIRE(m.area.has_value());
    REQUIRE(*m.area == 0.0);
    REQUIRE(m.outlineLength == 0.0);
    REQUIRE(std::isfinite(*m.area));
}

TEST_CASE("measureKnown trusts the supplied self-intersection answer",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry simple = polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}, true);

    // Told it is simple: the area is computed.
    const core::ShapeMetrics trusted = core::measureKnown(simple, core::PixelSize{}, false);
    REQUIRE(trusted.area.has_value());
    REQUIRE(*trusted.area == 50.0);

    // Told it self-intersects: the area is withheld even though it does not.
    // That is the contract, and why only a caller that can prove the outline is
    // unchanged may use it. The length is unaffected either way.
    const core::ShapeMetrics withheld = core::measureKnown(simple, core::PixelSize{}, true);
    REQUIRE_FALSE(withheld.area.has_value());
    REQUIRE(withheld.selfIntersecting);
    REQUIRE(withheld.outlineLength == trusted.outlineLength);
}

TEST_CASE("measureKnown ignores the flag for an open path", "[core][AnnotationMeasurement]")
{
    // An open path has no area because it is open, not because it crosses
    // itself, so the flag must not leak into selfIntersecting and imply a
    // reason that is not the real one.
    const core::ShapeMetrics m =
        core::measureKnown(polygon({{0.0, 0.0}, {10.0, 0.0}}, false), core::PixelSize{}, true);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE_FALSE(m.selfIntersecting);
}
```

Add `#include <cmath>` to the test file's includes.

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```

Expected: compile error, `'measure': is not a member of 'slideio::viewer::core'`.

- [ ] **Step 3: Declare the types and the function**

Add to `AnnotationMeasurement.h`, above `isSelfIntersecting`:

```cpp
/// Physical size of one pixel, in metres along each axis. The default measures
/// in pixel units, which is what most tests use because the arithmetic stays
/// checkable by hand.
struct PixelSize
{
    double x = 1.0;
    double y = 1.0;
};

struct ShapeMetrics
{
    /// Absent for an open path, and for a closed path that intersects itself.
    /// Never a number the application cannot stand behind.
    std::optional<double> area;
    /// Perimeter for a closed shape, total length for an open one. `closed`
    /// says which word a caller should use.
    double outlineLength = 0.0;
    bool closed = false;
    /// Why `area` is missing, when it is. NOT a general statement that the
    /// shape is simple: an open path is never tested, because its area is
    /// absent regardless and open traces carry the most vertices.
    bool selfIntersecting = false;
};

/// Area and outline length of `geometry`, with each vertex scaled by
/// `pixelSize` BEFORE measuring.
///
/// The scaling order matters. An area scales by x*y, but a perimeter does not
/// scale by any single factor -- each segment scales differently with its
/// direction -- so measuring in pixels and multiplying afterwards is wrong on
/// any slide whose pixels are not square. Scaling the vertices first is the
/// only order that is correct for both.
///
/// A non-positive pixelSize component collapses that axis to zero rather than
/// producing NaN: resolutionX is 0 on an uncalibrated slide.
[[nodiscard]] ShapeMetrics measure(const AnnotationGeometry& geometry, PixelSize pixelSize);

/// As measure(), but with the self-intersection answer supplied rather than
/// computed.
///
/// Exists so a caller that already knows the answer -- because the shape has
/// only been translated since it last asked, and self-intersection is
/// translation-invariant -- can skip the O(n^2) test. The properties panel
/// needs this: it repopulates on every mouse-move of a drag.
///
/// The supplied answer is TRUSTED. Passing a wrong one produces an area that
/// disagrees with the painted region, so only a caller that can prove the
/// outline is unchanged may use this.
[[nodiscard]] ShapeMetrics measureKnown(const AnnotationGeometry& geometry, PixelSize pixelSize,
                                        bool selfIntersecting);
```

- [ ] **Step 4: Implement it**

Add to `AnnotationMeasurement.cpp`, before `isSelfIntersecting`:

```cpp
ShapeMetrics measure(const AnnotationGeometry& geometry, PixelSize pixelSize)
{
    // The expensive question, asked once, then delegated. An open path never
    // needs it: its area is absent either way, and open freehand traces carry
    // the most vertices by far.
    const Outline shape = outline(geometry);
    const bool crosses = shape.closed && isSelfIntersecting(scaledOutline(shape, pixelSize));
    return measureKnown(geometry, pixelSize, crosses);
}

ShapeMetrics measureKnown(const AnnotationGeometry& geometry, PixelSize pixelSize,
                          bool selfIntersecting)
{
    // outline() is the single place that turns any shape into points plus a
    // closed flag, and a rectangle's outline is its normalised corners -- the
    // same box hitTest uses and paintAnnotations strokes. Measuring from it is
    // what keeps the number, the painted region and the hit-tested region in
    // agreement, and means a new geometry type needs no change here.
    const Outline shape = outline(geometry);

    const Outline scaled = scaledOutline(shape, pixelSize);

    ShapeMetrics metrics;
    metrics.closed = scaled.closed;

    const std::size_t n = scaled.points.size();
    if (n < 2) {
        return metrics;
    }

    const std::size_t segmentCount = scaled.closed ? n : n - 1;
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const PointF a = scaled.points[i];
        const PointF b = scaled.points[(i + 1) % n];
        metrics.outlineLength += std::hypot(b.x - a.x, b.y - a.y);
    }

    if (!scaled.closed) {
        // No area for an open path, so the supplied flag is irrelevant and
        // deliberately not echoed: selfIntersecting means "this is why the area
        // is missing", and for an open path the reason is simply that it is open.
        return metrics;
    }

    metrics.selfIntersecting = selfIntersecting;
    if (metrics.selfIntersecting) {
        return metrics;
    }

    // Shoelace, absolute so winding direction does not matter.
    double twiceSigned = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const PointF a = scaled.points[i];
        const PointF b = scaled.points[(i + 1) % n];
        twiceSigned += a.x * b.y - b.x * a.y;
    }
    metrics.area = std::abs(twiceSigned) / 2.0;
    return metrics;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS. If the diagonal test fails, the implementation is scaling after measuring rather than before.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationMeasurement.h src/core/src/AnnotationMeasurement.cpp tests/core/AnnotationMeasurementTest.cpp
git commit -m "Add core::measure and core::measureKnown"
```

---

### Task 3: Unit selection and formatting

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationMeasurement.h`
- Modify: `src/core/src/AnnotationMeasurement.cpp`
- Modify: `tests/core/AnnotationMeasurementTest.cpp` (append)

**Interfaces:**
- Produces:
  - `enum class core::MeasurementUnits { Physical, Pixels };`
  - `std::string core::formatLength(double value, MeasurementUnits units);`
  - `std::string core::formatArea(double value, MeasurementUnits units);`

`Physical` takes metres and square metres. `Pixels` takes pixels and square pixels.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/AnnotationMeasurementTest.cpp`:

```cpp
TEST_CASE("lengths below a millimetre read in microns", "[core][AnnotationMeasurement]")
{
    // 245 um = 245e-6 m.
    REQUIRE(core::formatLength(245e-6, core::MeasurementUnits::Physical) == "245 \xC2\xB5m");
    REQUIRE(core::formatLength(1e-6, core::MeasurementUnits::Physical) == "1.00 \xC2\xB5m");
    REQUIRE(core::formatLength(0.5e-6, core::MeasurementUnits::Physical) == "0.500 \xC2\xB5m");
}

TEST_CASE("lengths at or above a millimetre read in millimetres",
          "[core][AnnotationMeasurement]")
{
    REQUIRE(core::formatLength(1e-3, core::MeasurementUnits::Physical) == "1.00 mm");
    REQUIRE(core::formatLength(1.234e-3, core::MeasurementUnits::Physical) == "1.23 mm");
    REQUIRE(core::formatLength(15e-3, core::MeasurementUnits::Physical) == "15.0 mm");
}

TEST_CASE("a length rounding up to the threshold switches unit",
          "[core][AnnotationMeasurement]")
{
    // Review Focus 4. 999.6 um is 1000 um at three significant figures.
    // Printing "1000 um" would be four significant figures AND the wrong unit.
    REQUIRE(core::formatLength(999.6e-6, core::MeasurementUnits::Physical) == "1.00 mm");
    REQUIRE(core::formatLength(999.4e-6, core::MeasurementUnits::Physical) == "999 \xC2\xB5m");
}

TEST_CASE("areas pick their unit at one square millimetre",
          "[core][AnnotationMeasurement]")
{
    // 1 mm^2 = 1e-6 m^2. 872 um^2 = 872e-12 m^2.
    REQUIRE(core::formatArea(872e-12, core::MeasurementUnits::Physical) == "872 \xC2\xB5m\xC2\xB2");
    REQUIRE(core::formatArea(1e-6, core::MeasurementUnits::Physical) == "1.00 mm\xC2\xB2");
    REQUIRE(core::formatArea(4.56e-6, core::MeasurementUnits::Physical) == "4.56 mm\xC2\xB2");
}

TEST_CASE("a whole-slide area stays in millimetres, never scientific",
          "[core][AnnotationMeasurement]")
{
    // Review Focus 5. 15 mm x 15 mm = 225 mm^2 = 2.25e-4 m^2.
    const std::string shown = core::formatArea(2.25e-4, core::MeasurementUnits::Physical);
    REQUIRE(shown == "225 mm\xC2\xB2");
    REQUIRE(shown.find('e') == std::string::npos);
    REQUIRE(shown.find('E') == std::string::npos);
}

TEST_CASE("pixel measurements are labelled and carry no separators",
          "[core][AnnotationMeasurement]")
{
    REQUIRE(core::formatLength(1480.0, core::MeasurementUnits::Pixels) == "1480 px");
    REQUIRE(core::formatArea(12480.0, core::MeasurementUnits::Pixels) == "12480 px\xC2\xB2");
    // Locale-free: grouping is a presentation decision and core does not make it.
    REQUIRE(core::formatArea(12480.0, core::MeasurementUnits::Pixels).find(',') == std::string::npos);
}

TEST_CASE("zero formats cleanly in both unit systems", "[core][AnnotationMeasurement]")
{
    REQUIRE(core::formatLength(0.0, core::MeasurementUnits::Physical) == "0.00 \xC2\xB5m");
    REQUIRE(core::formatArea(0.0, core::MeasurementUnits::Pixels) == "0 px\xC2\xB2");
}
```

Add `#include <string>` to the test file's includes.

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```

Expected: `'formatLength': is not a member of 'slideio::viewer::core'`.

- [ ] **Step 3: Declare them**

Add to `AnnotationMeasurement.h`:

```cpp
/// Which unit system a formatter's input is in. The enum exists so a caller
/// cannot pass pixels where metres are expected and get a plausible wrong
/// answer.
enum class MeasurementUnits
{
    Physical, ///< metres, and square metres
    Pixels,   ///< pixels, and square pixels
};

/// `value` as a human-readable length. Physical input reads in microns below
/// one millimetre and millimetres at or above it; pixel input reads in px.
///
/// Three significant figures throughout, so the mantissa always reads 1-999 and
/// two people measuring the same shape write down the same number. The unit is
/// chosen AFTER rounding, so 999.6 um becomes "1.00 mm" rather than "1000 um".
///
/// Locale-free UTF-8: no thousands separators, and the micro sign spelled as
/// the two bytes the rest of this codebase already uses. Grouping and
/// translation are presentation decisions; this layer produces numbers, not
/// prose.
[[nodiscard]] std::string formatLength(double value, MeasurementUnits units);

/// `value` as a human-readable area, on the same rules: square microns below
/// one square millimetre, square millimetres at or above.
[[nodiscard]] std::string formatArea(double value, MeasurementUnits units);
```

- [ ] **Step 4: Implement them**

Add to `AnnotationMeasurement.cpp`, inside the anonymous namespace:

```cpp
/// `value` rendered with three significant figures and no exponent.
std::string threeSignificantFigures(double value)
{
    const double magnitude = std::abs(value);
    int decimals = 2;
    if (magnitude >= 100.0) {
        decimals = 0;
    } else if (magnitude >= 10.0) {
        decimals = 1;
    } else if (magnitude >= 1.0) {
        decimals = 2;
    } else {
        decimals = 3;
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return std::string(buffer);
}
```

and after `measure`:

```cpp
std::string formatLength(double value, MeasurementUnits units)
{
    if (units == MeasurementUnits::Pixels) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        return std::string(buffer) + " px";
    }

    // Microns first, then promote. The unit is chosen after rounding so a value
    // that rounds up to 1000 um is shown as 1.00 mm rather than as four
    // significant figures in the smaller unit.
    const double microns = value * 1e6;
    const std::string asMicrons = threeSignificantFigures(microns);
    // Re-read the ROUNDED text, not the raw value: 999.6 um rounds to "1000",
    // which belongs in millimetres. Testing `microns < 1000.0` instead would
    // print "1000 um" -- four significant figures in the smaller unit.
    if (std::stod(asMicrons) < 1000.0) {
        return asMicrons + " \xC2\xB5m";
    }
    return threeSignificantFigures(value * 1e3) + " mm";
}

std::string formatArea(double value, MeasurementUnits units)
{
    if (units == MeasurementUnits::Pixels) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        return std::string(buffer) + " px\xC2\xB2";
    }

    // 1 m^2 = 1e12 um^2, and 1 mm^2 = 1e6 um^2.
    const double squareMicrons = value * 1e12;
    const std::string asMicrons = threeSignificantFigures(squareMicrons);
    if (std::stod(asMicrons) < 1e6) {
        return asMicrons + " \xC2\xB5m\xC2\xB2";
    }
    return threeSignificantFigures(value * 1e6) + " mm\xC2\xB2";
}
```

Add `<cstdio>` and `<string>` to the file's includes.

The same rounding-then-choosing order applies in `formatArea`, which is why it
also re-reads `asMicrons` rather than testing `squareMicrons` directly.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS. The threshold tests are the ones most likely to fail: if `999.6e-6` shows as `1000 µm`, the unit is being chosen before rounding instead of after.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationMeasurement.h src/core/src/AnnotationMeasurement.cpp tests/core/AnnotationMeasurementTest.cpp
git commit -m "Add core::formatLength and core::formatArea"
```

---

### Task 4: The properties panel rows

No automated test is possible here — no test binary creates a `QApplication`. Verification is a clean build, all four suites still green, and the group W manual checks in Task 5.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h`
- Modify: `src/ui/src/AnnotationPropertiesPanel.cpp`
- Modify: `src/ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: `core::measure`, `core::ShapeMetrics`, `core::PixelSize`, `core::MeasurementUnits`, `core::formatLength`, `core::formatArea` (Tasks 1-3); `core::SlideInfo::resolutionX` / `resolutionY` (metres per pixel).
- Produces: `AnnotationPropertiesPanel::setPixelSize(core::PixelSize)`.

- [ ] **Step 1: Add the public setter to the header**

In `AnnotationPropertiesPanel.h`, beside `setEditingAllowed`:

```cpp
    /// Physical size of one pixel on the open slide, from SlideInfo's
    /// resolutionX/resolutionY. A non-positive component means the slide
    /// carries no calibration, and measurements are then shown in pixels
    /// rather than suppressed -- a labelled "12480 px²" is a real number that
    /// can be exported, where a scale bar with no scale would be meaningless.
    void setPixelSize(core::PixelSize pixelSize);
```

Add `#include "slideio/viewer/core/AnnotationMeasurement.h"` to the header's project includes.

- [ ] **Step 2: Add the rows, the state and the cache**

In `AnnotationPropertiesPanel.cpp`'s `Impl`, beside the other read-only labels:

```cpp
    QLabel* areaValue = nullptr;
    QLabel* lengthValue = nullptr;
    /// The QFormLayout row index of the Area row, so it can be hidden for an
    /// open trace.
    int areaRow = -1;
    /// The label widget of the length row, retitled between "Perimeter" and
    /// "Length" as the shape changes.
    QLabel* lengthCaption = nullptr;

    core::PixelSize pixelSize;

    /// Self-intersection by annotation id.
    ///
    /// The panel repopulates on annotationChanged, which ViewportWidget emits on
    /// EVERY mouse-move of a drag. The test is O(n^2) -- about a millisecond at
    /// a thousand vertices, but around 125 ms at five thousand -- so running it
    /// per frame would make dragging a long trace unusable. Caching is safe
    /// because self-intersection, area and perimeter are all
    /// translation-invariant: moving a shape cannot change any of them.
    ///
    /// *** VERTEX EDITING MUST INVALIDATE THIS CACHE WHEN IT LANDS. ***
    /// A stale "not self-intersecting" after a reshape would show an area that
    /// disagrees with the shaded region, which is the single failure this whole
    /// feature is designed to prevent.
    std::map<std::string, bool> selfIntersectionById;
```

Add `<map>` to the includes.

In the constructor, after the Modified row:

```cpp
    m_impl->areaValue = new QLabel(contents);
    form->addRow(tr("Area"), m_impl->areaValue);
    m_impl->areaRow = form->rowCount() - 1;

    m_impl->lengthCaption = new QLabel(tr("Perimeter"), contents);
    m_impl->lengthValue = new QLabel(contents);
    form->addRow(m_impl->lengthCaption, m_impl->lengthValue);
```

- [ ] **Step 3: Implement the setter and the measurement update**

```cpp
void AnnotationPropertiesPanel::setPixelSize(core::PixelSize pixelSize)
{
    m_impl->pixelSize = pixelSize;
    // A different slide means different calibration and a different set of
    // annotations; the cache is keyed by id and ids are not unique across
    // slides.
    m_impl->selfIntersectionById.clear();
    repopulate(true);
}

void AnnotationPropertiesPanel::updateMeasurements(const core::Annotation& annotation)
{
    const bool calibrated = m_impl->pixelSize.x > 0.0 && m_impl->pixelSize.y > 0.0;
    const core::PixelSize scale = calibrated ? m_impl->pixelSize : core::PixelSize{1.0, 1.0};
    const auto units = calibrated ? core::MeasurementUnits::Physical : core::MeasurementUnits::Pixels;

    const core::ShapeMetrics metrics = core::measure(annotation.geometry(), scale);

    // measure() already answered the expensive question for a closed shape, so
    // record it rather than asking again on the next repopulate.
    if (metrics.closed) {
        m_impl->selfIntersectionById[annotation.id()] = metrics.selfIntersecting;
    }

    auto* form = qobject_cast<QFormLayout*>(widget()->layout());
    if (metrics.closed) {
        if (form != nullptr && m_impl->areaRow >= 0) {
            form->setRowVisible(m_impl->areaRow, true);
        }
        m_impl->lengthCaption->setText(tr("Perimeter"));
        if (metrics.area.has_value()) {
            m_impl->areaValue->setText(
                QString::fromStdString(core::formatArea(*metrics.area, units)));
        } else {
            m_impl->areaValue->setText(tr("unavailable (self-intersecting)"));
        }
    } else {
        if (form != nullptr && m_impl->areaRow >= 0) {
            form->setRowVisible(m_impl->areaRow, false);
        }
        m_impl->lengthCaption->setText(tr("Length"));
    }

    m_impl->lengthValue->setText(
        QString::fromStdString(core::formatLength(metrics.outlineLength, units)));
}
```

Declare `void updateMeasurements(const core::Annotation& annotation);` in the header's private section beside `updateColorSwatch`.

`measureKnown` is the `core` function added in Task 2; the cache is what lets the panel use it instead of `measure`.

- [ ] **Step 4: Clear the cache with the model, and call the update**

In `setAnnotationModel`'s connection block, the existing `cleared` and `modelReset` handlers must also empty the cache. Replace the lambda they share:

```cpp
        const auto resetForNewContents = [this]() {
            // Ids are not unique across slides, and a cleared model is about to
            // be filled with a different set of annotations.
            m_impl->selfIntersectionById.clear();
            repopulate(true);
        };
        connect(m_impl->model, &app::AnnotationModel::cleared, this, resetForNewContents);
        connect(m_impl->model, &app::AnnotationModel::modelReset, this, resetForNewContents);
```

In `repopulate`, in the branch where an annotation is shown, after the existing field population:

```cpp
    updateMeasurements(*annotation);
```

and in the empty-state branch, beside the other cleared labels:

```cpp
    m_impl->areaValue->clear();
    m_impl->lengthValue->clear();
```

- [ ] **Step 5: Wire the calibration from MainWindow**

In `src/ui/src/MainWindow.cpp`, in the `slideOpened` handler, in the block that already reads `const auto& info = viewportWidget->slideInfo();` beside `zoomIndicatorWidget->setResolution(info.resolutionX);`:

```cpp
                if (annotationPropertiesPanel != nullptr) {
                    annotationPropertiesPanel->setPixelSize(
                        core::PixelSize{info.resolutionX, info.resolutionY});
                }
```

- [ ] **Step 6: Build and run every suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass. **If the build fails or any suite fails, your status is BLOCKED and you do not commit** — this project lands directly on `main` with no branch to absorb a red commit.

**Do not attempt any GUI check** — there is no GUI session in an agent context. Those are group W, run by a human in Task 5.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h src/ui/src/AnnotationPropertiesPanel.cpp src/ui/src/MainWindow.cpp src/core/include/slideio/viewer/core/AnnotationMeasurement.h src/core/src/AnnotationMeasurement.cpp tests/core/AnnotationMeasurementTest.cpp
git commit -m "Show annotation area, perimeter and length in the properties panel"
```

---

### Task 5: Run group W and record the results

**Files:**
- Modify: `docs/superpowers/specs/2026-10-14-annotation-measurement-design.md` (results)
- Modify: `CLAUDE.md`

- [ ] **Step 1: Build, install, and verify the artifact**

Kill any running viewer first, then:

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
ls -l build/install/release/bin/slideio-viewer.exe build/build/Release/slideio-viewer.exe
```

The two timestamps must match. A running viewer makes the install a silent no-op that still exits 0, and everything measured afterwards would be the old binary.

- [ ] **Step 2: Work through W1-W9**

Use the table in the spec. Record pass, fail or not-run with the reason, and **write down what was observed, not what was expected**. W8 — dragging a long freehand trace with the properties dock open — is the one that catches a mistake in the cache.

- [ ] **Step 3: Record the results in the spec**

Add a results section under group W: the date, the commit it ran against, and a row per check. Write up any new defect the way earlier campaigns did — what was observed, the cause, and the fix.

- [ ] **Step 4: Update CLAUDE.md**

Add measurement to the shipped feature list, and `AnnotationMeasurement` to the core layer line of the architecture diagram. Do **not** claim any runtime behaviour beyond what Step 2 actually observed; that file says so itself, and earlier drafts of it asserted things nobody had run.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-10-14-annotation-measurement-design.md CLAUDE.md
git commit -m "Record the group W manual run"
```

---

## Notes for the implementer

**The one thing most likely to go wrong** is Task 3's unit threshold. The unit must be chosen *after* rounding to three significant figures, or 999.6 µm prints as `1000 µm` — four significant figures in the wrong unit. The two threshold tests exist for exactly this and are worth running first.

**Task 2's diagonal test earns its keep.** It is the only test that distinguishes "scale the vertices, then measure" from "measure, then scale", and the second is wrong on every slide whose pixels are not square. If it fails, the fix is the order of operations, not the arithmetic.

**The cache comment in Task 4 is not decoration.** Vertex editing is on this project's roadmap, and when it lands a stale cache entry produces an area that disagrees with the shaded region — the exact failure the whole design exists to prevent.
