# Annotation Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put a drawable, selectable, movable rectangle annotation on a real slide, built on the domain model the rest of Stage 3 will extend.

**Architecture:** Geometry lives in `core` as a `std::variant` with pure free functions over plain data (Qt-free, fully unit-tested). `app::AnnotationModel` holds them in memory with change signals. `ui` adds a pure drag-routing function, tool modes, and overlay painting with `QPainter` at the end of `paintGL`. Geometry is stored in level-0 slide pixels; everything presentational — stroke width, hit tolerance, handle size — is computed in screen pixels.

**Tech Stack:** C++17, Qt 6 Widgets/OpenGLWidgets, Catch2 v3, CMake 3.20+ with Conan 2, MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-10-08-annotation-foundation-design.md`

## Global Constraints

- **`slideio-viewer-core` is Qt-free.** No Qt headers, no `Q_OBJECT`, no `QString`. Id generation uses `QUuid` and therefore lives in `app`, not `core`.
- **No test may construct a widget or require a `QApplication`.** None of the three existing test binaries creates one. Anything needing a widget is hand-verified instead.
- **Geometry is stored in slide coordinates (level-0 pixels, FR-ANN-28). Presentation is computed in screen coordinates.** Stroke width is screen-space (FR-ANN-11: 1–5 screen pixels) and must not scale with zoom.
- C++17. 4-space indent, 120-character lines. Allman braces for classes and functions, K&R for control flow. `#pragma once`.
- Include order: own header, project headers, Qt headers, std headers.
- Naming: `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, `lowercase` namespaces, `PascalCase.h/.cpp` files.
- **All AI-generated code must be human-reviewed before commit** (organization requirement).
- Work on a branch: `git checkout -b annotation-foundation` before Task 1. Do not commit to `main` directly.

### Build and test commands

```bash
# Incremental build of one target (fast loop)
cmake --build build/build --config Release --target slideio-viewer-core-tests

# One suite
ctest --test-dir build/build -C Release -R core-tests --output-on-failure

# All suites
ctest --test-dir build/build -C Release --output-on-failure

# A single Catch2 case by name — core-tests ONLY
./build/build/tests/Release/slideio-viewer-core-tests.exe "the test case name"

# After adding a NEW source file or test target, re-run CMake configure first:
cmake --build build/build --config Release
```

**Never invoke a Qt-linked test executable directly.** `app-tests`, `infra-tests` and
`ui-tests` find `Qt6Core.dll` (and, for the latter two, the SlideIO DLLs) through the
`ENVIRONMENT_MODIFICATION` property set in `tests/CMakeLists.txt`, which **ctest applies
and a bare exe invocation does not**. Running one of those `.exe` files directly pops a
modal Windows error box — "The code execution cannot proceed because Qt6Core.dll was not
found" — which blocks until a human dismisses it and will hang an unattended run. Use
`ctest ... -R <suite> --output-on-failure`, which gives per-assertion output on failure
anyway. Only `core-tests` links neither Qt nor SlideIO and is safe to run directly.

A full `./build.sh` is only needed when dependencies change, and requires conan on PATH:
`export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"` first.

**GUI verification:** the build-tree executable will not start. Install first
(`cmake --install build/build --config Release`) and launch the installed
`slideio-viewer.exe`. Close any running instance before installing or the copy fails.

## Review Focus

Five failure modes the spec implies that no obvious task would otherwise test. Each has its test assigned to the task that owns the code.

1. **Non-positive viewport scale in the tolerance conversion** divides by zero, yielding an infinite tolerance that makes every hit test succeed — every click would select an annotation anywhere on the slide. *Test in Task 5.*
2. **A rectangle dragged right-to-left or bottom-to-top** must become a valid positive-extent shape, not one that fails its own hit test and cannot be selected again. *Tests in Task 2 and Task 7.*
3. **Annotations surviving a slide change** would draw one slide's marks over another's tissue — a clinical misassociation, and the worst outcome available in this slice. *Test in Task 4, verified in Task 8.*
4. **Changing tool mid-drag** must not strand half-built state; the in-flight drag keeps its original owner until release. This rule lives in the handlers' `dragOwner` state, not in `resolveDragOwner`, so no unit test can reach it. *Enforced in Task 6 Step 4, hand-verified in Task 6 Step 8 and Task 8 Step 8.*
5. **Releasing the mouse outside the widget** must complete the drag rather than leave an annotation permanently attached to the cursor. *Verified in Task 8.*

---

### Task 1: Spike — prove `QPainter` can coexist with the tile renderer

Throwaway by design, and first by design. If this fails, every later task's painting assumption is wrong and the fallback (a transparent child `QWidget` overlay) changes the plan.

**Files:**
- Modify (temporarily): `src/ui/src/ViewportWidget.cpp` — `paintGL()`, after the `captureSnapshotTexture` call at line 3172

**Interfaces:**
- Consumes: nothing
- Produces: a yes/no answer recorded in the commit message of Task 7; no code survives this task

- [ ] **Step 1: Add a hard-coded painted rectangle at the very end of `paintGL`**

In `src/ui/src/ViewportWidget.cpp`, add `#include <QPainter>` to the Qt include block, then append to the end of `ViewportWidget::paintGL()` — after the closing brace of the `if (tilesSkipped > 0 || morePending)` / `else` block, as the last statements in the function:

```cpp
    // SPIKE -- remove before Task 2. Verifies QPainter can draw over the FBO
    // tile renderer without disturbing it.
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(0xE6, 0x7E, 0x22), 2));
        painter.setBrush(QColor(0xE6, 0x7E, 0x22, 60));
        painter.drawRect(100, 100, 300, 200);
    }
```

- [ ] **Step 2: Build and install**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

Expected: builds clean.

- [ ] **Step 3: Verify in the running application**

Launch the installed `slideio-viewer.exe` and open any brightfield slide. Check all five:

1. The orange rectangle appears over the tissue.
2. Tiles still render correctly — no black viewport, no missing tiles, no corruption.
3. Zoom in and out with the scroll wheel. Tiles keep loading and the view stays correct.
4. **During a zoom, the rectangle does not smear or scale with the blurry backdrop.** It stays at a fixed 300×200 screen position. This is the specific property the post-`captureSnapshotTexture` placement buys.
5. Pan with left-drag. No rendering artifacts.

- [ ] **Step 4: Decide**

- **All five hold** → record "QPainter overlay verified" and continue to Step 5.
- **Any fails** → **stop and report to your human partner.** Do not continue to Task 2. The spec's §5 fallback (transparent child `QWidget` overlay parented to `viewportWidget`, as `ZoomIndicatorWidget` and `LoadingOverlay` already are) changes Tasks 7 and 8 and needs a decision, not a workaround.

- [ ] **Step 5: Revert the spike**

```bash
git checkout -- src/ui/src/ViewportWidget.cpp
```

Nothing from this task is committed.

---

### Task 2: `core` — geometry types, `boundingBox`, `hitTest`

**Files:**
- Modify: `src/core/include/slideio/viewer/core/Types.h` — append `PointF`, `RectF`, `Color` after `struct Rect` (line 331), before the closing `} // namespace`
- Create: `src/core/include/slideio/viewer/core/AnnotationGeometry.h`
- Create: `src/core/src/AnnotationGeometry.cpp`
- Modify: `src/core/CMakeLists.txt` — add `src/AnnotationGeometry.cpp`
- Test: `tests/core/AnnotationGeometryTest.cpp`
- Modify: `tests/CMakeLists.txt` — add `core/AnnotationGeometryTest.cpp` to `slideio-viewer-core-tests`

**Interfaces:**
- Consumes: `core::Rect<T>` from `Types.h`
- Produces: `core::PointF{double x, y}`, `core::RectF` (alias of `Rect<double>`), `core::Color{uint8_t r,g,b,a}`, `core::RectangleGeometry{PointF topLeft, bottomRight}`, `core::AnnotationGeometry` (variant), `RectF boundingBox(const AnnotationGeometry&)`, `bool hitTest(const AnnotationGeometry&, PointF, double)`

- [ ] **Step 1: Write the failing test**

Create `tests/core/AnnotationGeometryTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeometry.h"

using namespace slideio::viewer::core;

namespace
{

AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return RectangleGeometry{PointF{x0, y0}, PointF{x1, y1}};
}

} // namespace

TEST_CASE("boundingBox returns the rectangle itself", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(rect(10.0, 20.0, 110.0, 70.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

// Review Focus 2: a drag that runs right-to-left and bottom-to-top must still
// produce a positive-extent box, or the shape fails its own hit test and can
// never be selected again.
TEST_CASE("boundingBox normalises an inverted rectangle", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(rect(110.0, 70.0, 10.0, 20.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("boundingBox of a degenerate rectangle has zero extent", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(rect(10.0, 20.0, 10.0, 20.0));
    REQUIRE(box.width == 0.0);
    REQUIRE(box.height == 0.0);
}

TEST_CASE("hitTest succeeds inside the shape", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{50.0, 40.0}, 0.0));
}

TEST_CASE("hitTest succeeds on the outline with zero tolerance", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{10.0, 45.0}, 0.0));
}

TEST_CASE("hitTest fails outside the shape", "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{5.0, 45.0}, 0.0));
}

TEST_CASE("hitTest succeeds just outside when tolerance covers the gap",
          "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{5.0, 45.0}, 5.0));
}

TEST_CASE("hitTest fails just beyond the tolerance boundary", "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{4.0, 45.0}, 5.0));
}

TEST_CASE("hitTest treats a negative tolerance as zero", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{50.0, 40.0}, -10.0));
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{5.0, 45.0}, -10.0));
}

TEST_CASE("hitTest works on an inverted rectangle", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(110.0, 70.0, 10.0, 20.0), PointF{50.0, 40.0}, 0.0));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```
Expected: FAIL — `AnnotationGeometry.h` does not exist.

- [ ] **Step 3: Add the new types to `Types.h`**

In `src/core/include/slideio/viewer/core/Types.h`, immediately after the `struct Rect` definition (which ends at line 337) and before `} // namespace slideio::viewer::core`:

```cpp
using RectF = Rect<double>;

struct PointF
{
    double x = 0.0;
    double y = 0.0;
};

/// 8-bit RGBA. Annotation colours come from the colorblind-safe palette in
/// FR-ANN-15; the default below is that palette's orange.
struct Color
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};
```

- [ ] **Step 4: Write `AnnotationGeometry.h`**

```cpp
#pragma once

#include "slideio/viewer/core/Types.h"

#include <variant>

namespace slideio::viewer::core
{

struct RectangleGeometry
{
    PointF topLeft;
    PointF bottomRight;
};

/// The variant starts with one alternative and widens as drawing tools land.
/// std::visit dispatch in the free functions below is identical at one
/// alternative or six, so the structure is established now without writing
/// nine geometry types against a transform not yet proven on a real slide.
using AnnotationGeometry = std::variant<RectangleGeometry>;

/// Always positive-extent: a shape built from an inverted drag is normalised.
[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);

/// True when `point` lies inside the shape or within `toleranceSlideUnits` of
/// its outline. Filled annotations (FR-ANN-11 gives them a fill opacity) must
/// be clickable in their interior.
///
/// The tolerance is in SLIDE units. Callers hold a screen-pixel tolerance and
/// convert with ui::screenToleranceToSlide, which keeps the only
/// zoom-dependent arithmetic at one call site and this function pure.
/// A negative tolerance is treated as zero.
[[nodiscard]] bool hitTest(const AnnotationGeometry& geom, PointF point,
                           double toleranceSlideUnits);

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Write `AnnotationGeometry.cpp`**

```cpp
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <algorithm>

namespace slideio::viewer::core
{

namespace
{

RectF normalized(const RectangleGeometry& rect)
{
    const double x0 = std::min(rect.topLeft.x, rect.bottomRight.x);
    const double y0 = std::min(rect.topLeft.y, rect.bottomRight.y);
    const double x1 = std::max(rect.topLeft.x, rect.bottomRight.x);
    const double y1 = std::max(rect.topLeft.y, rect.bottomRight.y);
    return RectF{x0, y0, x1 - x0, y1 - y0};
}

bool hitTestShape(const RectangleGeometry& rect, PointF point, double tolerance)
{
    const RectF box = normalized(rect);
    return point.x >= box.x - tolerance
        && point.x <= box.x + box.width + tolerance
        && point.y >= box.y - tolerance
        && point.y <= box.y + box.height + tolerance;
}

} // namespace

RectF boundingBox(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return normalized(shape); }, geom);
}

bool hitTest(const AnnotationGeometry& geom, PointF point, double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    return std::visit([&](const auto& shape) { return hitTestShape(shape, point, tolerance); },
                      geom);
}

} // namespace slideio::viewer::core
```

- [ ] **Step 6: Wire into CMake**

In `src/core/CMakeLists.txt`, add to `add_library(slideio-viewer-core STATIC ...)` after `src/SlideId.cpp`:

```cmake
    src/AnnotationGeometry.cpp
```

In `tests/CMakeLists.txt`, add to `add_executable(slideio-viewer-core-tests ...)` after `core/ColorProfileOverrideTest.cpp`:

```cmake
    core/AnnotationGeometryTest.cpp
```

- [ ] **Step 7: Run tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```
Expected: PASS, all cases.

- [ ] **Step 8: Commit**

```bash
git add src/core/include/slideio/viewer/core/Types.h \
        src/core/include/slideio/viewer/core/AnnotationGeometry.h \
        src/core/src/AnnotationGeometry.cpp \
        src/core/CMakeLists.txt \
        tests/core/AnnotationGeometryTest.cpp \
        tests/CMakeLists.txt
git commit -m "Add annotation geometry with normalised bounds and hit testing"
```

---

### Task 3: `core` — `Annotation`

**Files:**
- Create: `src/core/include/slideio/viewer/core/Annotation.h`
- Create: `src/core/src/Annotation.cpp`
- Modify: `src/core/CMakeLists.txt` — add `src/Annotation.cpp`
- Test: `tests/core/AnnotationTest.cpp`
- Modify: `tests/CMakeLists.txt` — add `core/AnnotationTest.cpp`

**Interfaces:**
- Consumes: `core::AnnotationGeometry`, `core::boundingBox`, `core::hitTest`, `core::Color`, `core::PointF`, `core::RectF` from Task 2
- Produces: `core::AnnotationType` (enum, `Rectangle`), `core::AnnotationProperties`, `core::AnnotationMetadata`, `core::Annotation` with `id()`, `type()`, `geometry()`, `properties()`, `metadata()`, `setGeometry(AnnotationGeometry)`, `setProperties(AnnotationProperties)`, `boundingBox()`, `containsPoint(PointF, double)`

- [ ] **Step 1: Write the failing test**

Create `tests/core/AnnotationTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Annotation.h"

using namespace slideio::viewer::core;

namespace
{

Annotation makeRect(std::string id = "a1")
{
    return Annotation(std::move(id), AnnotationType::Rectangle,
                      RectangleGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});
}

} // namespace

TEST_CASE("an annotation keeps the id and type it was built with", "[core][Annotation]")
{
    const Annotation annotation = makeRect("abc123");
    REQUIRE(annotation.id() == "abc123");
    REQUIRE(annotation.type() == AnnotationType::Rectangle);
}

TEST_CASE("an annotation delegates its bounding box to the geometry", "[core][Annotation]")
{
    const RectF box = makeRect().boundingBox();
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("containsPoint follows the geometry's hit test", "[core][Annotation]")
{
    const Annotation annotation = makeRect();
    REQUIRE(annotation.containsPoint(PointF{50.0, 40.0}, 0.0));
    REQUIRE_FALSE(annotation.containsPoint(PointF{5.0, 45.0}, 0.0));
    REQUIRE(annotation.containsPoint(PointF{5.0, 45.0}, 5.0));
}

TEST_CASE("a new annotation carries the default palette colour", "[core][Annotation]")
{
    const AnnotationProperties props = makeRect().properties();
    REQUIRE(props.color.r == 0xE6);
    REQUIRE(props.color.g == 0x7E);
    REQUIRE(props.color.b == 0x22);
    REQUIRE(props.color.a == 0xFF);
}

TEST_CASE("the author field starts empty pending user identity", "[core][Annotation]")
{
    // FR-USER-01 and FR-ANN-12 require an author, but user identity is Stage 4.
    // The field exists now so the struct does not change when it lands.
    REQUIRE(makeRect().metadata().author.empty());
}

TEST_CASE("setGeometry replaces the shape and refreshes the bounding box",
          "[core][Annotation]")
{
    Annotation annotation = makeRect();
    annotation.setGeometry(RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});

    const RectF box = annotation.boundingBox();
    REQUIRE(box.x == 0.0);
    REQUIRE(box.width == 10.0);
}

TEST_CASE("modifiedAt is never earlier than createdAt", "[core][Annotation]")
{
    // Deliberately not asserting a strict increase: the system clock's
    // resolution can be coarser than the time between construction and the
    // call below, which would make a strict assertion flaky.
    Annotation annotation = makeRect();
    annotation.setGeometry(RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});
    REQUIRE(annotation.metadata().modifiedAt >= annotation.metadata().createdAt);
}

TEST_CASE("setProperties replaces the properties", "[core][Annotation]")
{
    Annotation annotation = makeRect();
    AnnotationProperties props;
    props.label = "tumour margin";
    props.lineWidth = 4.0f;
    annotation.setProperties(props);

    REQUIRE(annotation.properties().label == "tumour margin");
    REQUIRE(annotation.properties().lineWidth == 4.0f);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```
Expected: FAIL — `Annotation.h` does not exist.

- [ ] **Step 3: Write `Annotation.h`**

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <chrono>
#include <string>

namespace slideio::viewer::core
{

enum class AnnotationType
{
    Rectangle,
};

struct AnnotationProperties
{
    std::string label;
    std::string classification;
    /// FR-ANN-15's colorblind-safe palette; this is its orange.
    Color color{0xE6, 0x7E, 0x22, 0xFF};
    /// Screen pixels, not slide pixels (FR-ANN-11: 1-5 screen pixels).
    float lineWidth = 2.0f;
    float fillOpacity = 0.3f;
    std::string notes;
};

struct AnnotationMetadata
{
    /// Empty until user identity lands. FR-ANN-12 requires an author and
    /// FR-USER-01 blocks creation without a configured identity, but user
    /// identity is Stage 4 item 8. The field exists now so the struct does not
    /// change when annotation persistence populates it.
    std::string author;
    std::chrono::system_clock::time_point createdAt{};
    std::chrono::system_clock::time_point modifiedAt{};
};

class Annotation
{
public:
    Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry);

    [[nodiscard]] const std::string& id() const;
    [[nodiscard]] AnnotationType type() const;
    [[nodiscard]] const AnnotationGeometry& geometry() const;
    [[nodiscard]] const AnnotationProperties& properties() const;
    [[nodiscard]] const AnnotationMetadata& metadata() const;

    void setGeometry(AnnotationGeometry geometry);
    void setProperties(AnnotationProperties properties);

    [[nodiscard]] RectF boundingBox() const;
    [[nodiscard]] bool containsPoint(PointF slidePos, double toleranceSlideUnits) const;

private:
    std::string m_id;
    AnnotationType m_type;
    AnnotationGeometry m_geometry;
    AnnotationProperties m_properties;
    AnnotationMetadata m_metadata;
};

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Write `Annotation.cpp`**

```cpp
#include "slideio/viewer/core/Annotation.h"

#include <utility>

namespace slideio::viewer::core
{

Annotation::Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry)
    : m_id(std::move(id))
    , m_type(type)
    , m_geometry(std::move(geometry))
{
    m_metadata.createdAt = std::chrono::system_clock::now();
    m_metadata.modifiedAt = m_metadata.createdAt;
}

const std::string& Annotation::id() const
{
    return m_id;
}

AnnotationType Annotation::type() const
{
    return m_type;
}

const AnnotationGeometry& Annotation::geometry() const
{
    return m_geometry;
}

const AnnotationProperties& Annotation::properties() const
{
    return m_properties;
}

const AnnotationMetadata& Annotation::metadata() const
{
    return m_metadata;
}

void Annotation::setGeometry(AnnotationGeometry geometry)
{
    m_geometry = std::move(geometry);
    m_metadata.modifiedAt = std::chrono::system_clock::now();
}

void Annotation::setProperties(AnnotationProperties properties)
{
    m_properties = std::move(properties);
    m_metadata.modifiedAt = std::chrono::system_clock::now();
}

RectF Annotation::boundingBox() const
{
    return core::boundingBox(m_geometry);
}

bool Annotation::containsPoint(PointF slidePos, double toleranceSlideUnits) const
{
    return core::hitTest(m_geometry, slidePos, toleranceSlideUnits);
}

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Wire into CMake**

`src/core/CMakeLists.txt`, after `src/AnnotationGeometry.cpp`:

```cmake
    src/Annotation.cpp
```

`tests/CMakeLists.txt`, after `core/AnnotationGeometryTest.cpp`:

```cmake
    core/AnnotationTest.cpp
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/Annotation.h \
        src/core/src/Annotation.cpp \
        src/core/CMakeLists.txt \
        tests/core/AnnotationTest.cpp \
        tests/CMakeLists.txt
git commit -m "Add the Annotation domain type"
```

---

### Task 4: `app` — `AnnotationModel` and a new test target

`slideio-viewer-app` is currently built but linked by nothing — `AnnotationModel` is its first live consumer, so this task also gives `slideio-viewer-ui` the link it will need in Task 6.

**Files:**
- Create: `src/app/include/slideio/viewer/app/AnnotationModel.h`
- Create: `src/app/src/AnnotationModel.cpp`
- Modify: `src/app/CMakeLists.txt` — add the source
- Modify: `src/ui/CMakeLists.txt:42` — add `slideio-viewer-app` to the PUBLIC link libraries
- Test: `tests/app/AnnotationModelTest.cpp`
- Modify: `tests/CMakeLists.txt` — new `slideio-viewer-app-tests` target

**Interfaces:**
- Consumes: `core::Annotation`, `core::AnnotationType`, `core::AnnotationGeometry`, `core::PointF` from Tasks 2–3
- Produces: `app::AnnotationModel` with `add(AnnotationType, AnnotationGeometry) -> std::string`, `remove(const std::string&) -> bool`, `setGeometry(const std::string&, AnnotationGeometry) -> bool`, `annotations() -> const std::vector<core::Annotation>&`, `find(const std::string&) -> const core::Annotation*`, `hitTest(PointF, double) -> std::string`, `setSelected(const std::string&)`, `clearSelection()`, `selectedId() -> const std::string&`, `clear()`; signals `annotationAdded`, `annotationRemoved`, `annotationChanged`, `selectionChanged`, `cleared`

- [ ] **Step 1: Write the failing test**

Create `tests/app/AnnotationModelTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"

#include <QObject>

#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

std::string addRect(app::AnnotationModel& model, double x0, double y0, double x1, double y1)
{
    return model.add(core::AnnotationType::Rectangle, rect(x0, y0, x1, y1));
}

} // namespace

TEST_CASE("add stores an annotation and returns a non-empty id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    REQUIRE_FALSE(id.empty());
    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("every added annotation gets a distinct id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string a = addRect(model, 0.0, 0.0, 10.0, 10.0);
    const std::string b = addRect(model, 0.0, 0.0, 10.0, 10.0);
    const std::string c = addRect(model, 0.0, 0.0, 10.0, 10.0);

    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a != c);
}

TEST_CASE("add emits annotationAdded with the new id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::annotationAdded,
                     [&seen](const std::string& id) { seen.push_back(id); });

    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("remove deletes the annotation and emits annotationRemoved",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::annotationRemoved,
                     [&seen](const std::string& removed) { seen.push_back(removed); });

    REQUIRE(model.remove(id));
    REQUIRE(model.annotations().empty());
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("remove reports false for an unknown id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    REQUIRE_FALSE(model.remove("not-a-real-id"));
}

TEST_CASE("setGeometry updates the shape and emits annotationChanged",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    int changes = 0;
    QObject::connect(&model, &app::AnnotationModel::annotationChanged,
                     [&changes](const std::string&) { ++changes; });

    REQUIRE(model.setGeometry(id, rect(5.0, 5.0, 25.0, 25.0)));
    REQUIRE(changes == 1);

    const core::Annotation* found = model.find(id);
    REQUIRE(found != nullptr);
    REQUIRE(found->boundingBox().x == 5.0);
    REQUIRE(found->boundingBox().width == 20.0);
}

TEST_CASE("hitTest returns the topmost annotation under the point",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    addRect(model, 0.0, 0.0, 100.0, 100.0);
    const std::string top = addRect(model, 10.0, 10.0, 50.0, 50.0);

    // Both contain (20,20); the most recently added wins.
    REQUIRE(model.hitTest(core::PointF{20.0, 20.0}, 0.0) == top);
}

TEST_CASE("hitTest returns an empty id when nothing is under the point",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    addRect(model, 0.0, 0.0, 10.0, 10.0);
    REQUIRE(model.hitTest(core::PointF{500.0, 500.0}, 0.0).empty());
}

TEST_CASE("setSelected records the selection and emits selectionChanged",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&seen](const std::string& selected) { seen.push_back(selected); });

    model.setSelected(id);
    REQUIRE(model.selectedId() == id);
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("selecting the already-selected annotation emits nothing",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    int emissions = 0;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&emissions](const std::string&) { ++emissions; });

    model.setSelected(id);
    REQUIRE(emissions == 0);
}

TEST_CASE("removing the selected annotation clears the selection",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    REQUIRE(model.remove(id));
    REQUIRE(model.selectedId().empty());
}

// Review Focus 3: annotations surviving a slide change would draw one slide's
// marks over another's tissue. Nothing is persisted in this slice, so a stale
// model is the only way that can happen -- and the only outcome is a clinical
// misassociation.
TEST_CASE("clear drops every annotation and the selection", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    addRect(model, 20.0, 20.0, 30.0, 30.0);
    model.setSelected(id);

    int clearedCount = 0;
    QObject::connect(&model, &app::AnnotationModel::cleared,
                     [&clearedCount]() { ++clearedCount; });

    model.clear();

    REQUIRE(model.annotations().empty());
    REQUIRE(model.selectedId().empty());
    REQUIRE(clearedCount == 1);
}

TEST_CASE("clear on an empty model still reports cleared", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    int clearedCount = 0;
    QObject::connect(&model, &app::AnnotationModel::cleared,
                     [&clearedCount]() { ++clearedCount; });

    model.clear();
    REQUIRE(clearedCount == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

The target does not exist yet, so this fails at configure time:

```bash
cmake --build build/build --config Release --target slideio-viewer-app-tests
```
Expected: FAIL — no such target.

- [ ] **Step 3: Write `AnnotationModel.h`**

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QObject>

#include <string>
#include <vector>

namespace slideio::viewer::app
{

/// In-memory store of the open slide's annotations.
///
/// Lives in the application layer rather than core because it generates ids
/// with QUuid and emits Qt signals; core is Qt-free. Storage is a flat vector
/// searched linearly -- at the hundreds-of-annotations scale a spatial index
/// buys nothing, and adding one later does not change this interface.
class AnnotationModel : public QObject
{
    Q_OBJECT

public:
    explicit AnnotationModel(QObject* parent = nullptr);
    ~AnnotationModel() override;

    AnnotationModel(const AnnotationModel&) = delete;
    AnnotationModel& operator=(const AnnotationModel&) = delete;

    /// Stores a new annotation under a freshly generated id, which is returned.
    std::string add(core::AnnotationType type, core::AnnotationGeometry geometry);

    /// False when `id` is unknown. Clears the selection if it named `id`.
    bool remove(const std::string& id);

    /// False when `id` is unknown.
    bool setGeometry(const std::string& id, core::AnnotationGeometry geometry);

    [[nodiscard]] const std::vector<core::Annotation>& annotations() const;

    /// Null when `id` is unknown. The pointer is invalidated by any mutation.
    [[nodiscard]] const core::Annotation* find(const std::string& id) const;

    /// The topmost annotation containing `point`, searched in reverse creation
    /// order so the most recently drawn wins. Empty when none is hit.
    [[nodiscard]] std::string hitTest(core::PointF point, double toleranceSlideUnits) const;

    /// Pass an empty id to clear. Emits only on an actual change.
    void setSelected(const std::string& id);
    void clearSelection();
    [[nodiscard]] const std::string& selectedId() const;

    /// Drops every annotation and the selection. Called whenever the open slide
    /// changes: annotations must never survive into a different slide, and
    /// nothing here is persisted, so a stale model is pure misassociation.
    void clear();

signals:
    void annotationAdded(const std::string& id);
    void annotationRemoved(const std::string& id);
    void annotationChanged(const std::string& id);
    void selectionChanged(const std::string& id);
    void cleared();

private:
    std::vector<core::Annotation> m_annotations;
    std::string m_selectedId;
};

} // namespace slideio::viewer::app
```

- [ ] **Step 4: Write `AnnotationModel.cpp`**

```cpp
#include "slideio/viewer/app/AnnotationModel.h"

#include <QUuid>

#include <algorithm>
#include <utility>

namespace slideio::viewer::app
{

namespace
{

std::string generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

} // namespace

AnnotationModel::AnnotationModel(QObject* parent)
    : QObject(parent)
{
}

AnnotationModel::~AnnotationModel() = default;

std::string AnnotationModel::add(core::AnnotationType type, core::AnnotationGeometry geometry)
{
    std::string id = generateId();
    m_annotations.emplace_back(id, type, std::move(geometry));
    emit annotationAdded(id);
    return id;
}

bool AnnotationModel::remove(const std::string& id)
{
    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&id](const core::Annotation& a) { return a.id() == id; });
    if (it == m_annotations.end()) {
        return false;
    }

    m_annotations.erase(it);
    if (m_selectedId == id) {
        m_selectedId.clear();
        emit selectionChanged(m_selectedId);
    }
    emit annotationRemoved(id);
    return true;
}

bool AnnotationModel::setGeometry(const std::string& id, core::AnnotationGeometry geometry)
{
    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&id](const core::Annotation& a) { return a.id() == id; });
    if (it == m_annotations.end()) {
        return false;
    }

    it->setGeometry(std::move(geometry));
    emit annotationChanged(id);
    return true;
}

const std::vector<core::Annotation>& AnnotationModel::annotations() const
{
    return m_annotations;
}

const core::Annotation* AnnotationModel::find(const std::string& id) const
{
    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&id](const core::Annotation& a) { return a.id() == id; });
    return it == m_annotations.end() ? nullptr : &(*it);
}

std::string AnnotationModel::hitTest(core::PointF point, double toleranceSlideUnits) const
{
    for (auto it = m_annotations.rbegin(); it != m_annotations.rend(); ++it) {
        if (it->containsPoint(point, toleranceSlideUnits)) {
            return it->id();
        }
    }
    return {};
}

void AnnotationModel::setSelected(const std::string& id)
{
    if (m_selectedId == id) {
        return;
    }
    m_selectedId = id;
    emit selectionChanged(m_selectedId);
}

void AnnotationModel::clearSelection()
{
    setSelected(std::string{});
}

const std::string& AnnotationModel::selectedId() const
{
    return m_selectedId;
}

void AnnotationModel::clear()
{
    m_annotations.clear();
    if (!m_selectedId.empty()) {
        m_selectedId.clear();
        emit selectionChanged(m_selectedId);
    }
    emit cleared();
}

} // namespace slideio::viewer::app
```

- [ ] **Step 5: Wire into CMake**

`src/app/CMakeLists.txt` — add to the library sources:

```cmake
add_library(slideio-viewer-app STATIC
    src/SlideViewerService.cpp
    src/AnnotationModel.cpp
)
```

`src/ui/CMakeLists.txt` line 42 — `slideio-viewer-ui` currently links core and infra only, and nothing in the repository links the app layer. Add it:

```cmake
target_link_libraries(slideio-viewer-ui
    PUBLIC slideio-viewer-core slideio-viewer-app slideio-viewer-infra
    PUBLIC Qt6::Widgets Qt6::OpenGLWidgets
    PRIVATE spdlog::spdlog
)
```

`tests/CMakeLists.txt` — add a new target after the core-tests block:

```cmake
# Application layer tests
add_executable(slideio-viewer-app-tests
    app/AnnotationModelTest.cpp
)

target_link_libraries(slideio-viewer-app-tests
    PRIVATE slideio-viewer-app Catch2::Catch2WithMain Qt6::Core
)

add_test(NAME app-tests COMMAND slideio-viewer-app-tests)
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R app-tests --output-on-failure
```
Expected: PASS. Signals work here without a `QApplication` because every connection is direct.

- [ ] **Step 7: Run the whole suite to confirm nothing regressed**

```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 4 tests pass (core-tests, app-tests, infra-tests, ui-tests).

- [ ] **Step 8: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationModel.h \
        src/app/src/AnnotationModel.cpp \
        src/app/CMakeLists.txt \
        src/ui/CMakeLists.txt \
        tests/app/AnnotationModelTest.cpp \
        tests/CMakeLists.txt
git commit -m "Add the in-memory annotation model and an app test target"
```

---

### Task 5: `ui` — drag routing and tolerance conversion

The highest-risk logic in the slice, extracted from the event handlers so it can be tested without a `QApplication`.

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationInteraction.h`
- Create: `src/ui/src/AnnotationInteraction.cpp`
- Modify: `src/ui/CMakeLists.txt` — add the source
- Test: `tests/ui/AnnotationInteractionTest.cpp`
- Modify: `tests/CMakeLists.txt` — add the test file to `slideio-viewer-ui-tests`

**Interfaces:**
- Consumes: nothing from earlier tasks
- Produces: `ui::AnnotationTool` (`Pan`, `Rectangle`), `ui::DragOwner` (`None`, `Pan`, `Tool`, `MoveAnnotation`), `DragOwner resolveDragOwner(Qt::MouseButton, Qt::KeyboardModifiers, bool spaceHeld, AnnotationTool, bool pressHitAnnotation)`, `double screenToleranceToSlide(double, double)`, `constexpr double kHitToleranceScreenPixels`

- [ ] **Step 1: Write the failing test**

Create `tests/ui/AnnotationInteractionTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationInteraction.h"

using namespace slideio::viewer::ui;

TEST_CASE("middle button always pans, whatever the tool", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, true) == DragOwner::Pan);
}

TEST_CASE("held space pans, whatever the tool", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Rectangle, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Pan, true) == DragOwner::Pan);
}

TEST_CASE("left drag on empty space with the pan tool pans",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, false) == DragOwner::Pan);
}

TEST_CASE("left drag on an annotation with the pan tool moves it",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, true) == DragOwner::MoveAnnotation);
}

TEST_CASE("left drag with a drawing tool belongs to the tool",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, false) == DragOwner::Tool);
    // Pressing on an existing annotation does not steal the drag from a
    // drawing tool: the user asked to draw.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, true) == DragOwner::Tool);
}

TEST_CASE("the right button owns no drag", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::RightButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, false) == DragOwner::None);
    REQUIRE(resolveDragOwner(Qt::RightButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, true) == DragOwner::None);
}

TEST_CASE("tolerance converts from screen pixels to slide units",
          "[ui][AnnotationInteraction]")
{
    // At 2x, 6 screen pixels is 3 slide pixels.
    REQUIRE(screenToleranceToSlide(6.0, 2.0) == 3.0);
    // At 0.5x, the same 6 screen pixels covers 12 slide pixels.
    REQUIRE(screenToleranceToSlide(6.0, 0.5) == 12.0);
}

// Review Focus 1: dividing by a non-positive scale yields an infinite
// tolerance, which makes every hit test succeed -- one click would select an
// annotation anywhere on the slide.
TEST_CASE("a non-positive scale yields zero tolerance, never infinity",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(screenToleranceToSlide(6.0, 0.0) == 0.0);
    REQUIRE(screenToleranceToSlide(6.0, -1.0) == 0.0);
}

TEST_CASE("the hit tolerance constant is a usable click target",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(kHitToleranceScreenPixels > 0.0);
    REQUIRE(kHitToleranceScreenPixels <= 12.0);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build/build --config Release --target slideio-viewer-ui-tests
```
Expected: FAIL — `AnnotationInteraction.h` does not exist.

- [ ] **Step 3: Write `AnnotationInteraction.h`**

```cpp
#pragma once

#include <Qt>

namespace slideio::viewer::ui
{

enum class AnnotationTool
{
    Pan,
    Rectangle,
};

enum class DragOwner
{
    None,
    Pan,
    Tool,
    MoveAnnotation,
};

/// Screen-space radius within which a click counts as hitting an annotation.
/// Screen-space so the target stays the same physical size at 1x and at 40x.
constexpr double kHitToleranceScreenPixels = 6.0;

/// Decides who owns a drag beginning with `button`.
///
/// Pure -- no widget state and no side effects -- so the routing rules can be
/// tested without a QApplication, which no test binary in this repository
/// creates. The rules, in order:
///
///   * middle button pans, whatever the tool (02-user-interface-design.md
///     3.2: pan stays available without leaving annotation mode);
///   * held space pans, whatever the tool;
///   * left button with the pan tool moves an annotation if the press landed
///     on one, and otherwise pans;
///   * left button with a drawing tool belongs to the tool;
///   * anything else owns nothing.
///
/// `modifiers` is unused today and named in the signature for FR-ANN-16's
/// shift-click multi-select, which is the next thing to touch these rules.
[[nodiscard]] DragOwner resolveDragOwner(Qt::MouseButton button,
                                         Qt::KeyboardModifiers modifiers,
                                         bool spaceHeld,
                                         AnnotationTool active,
                                         bool pressHitAnnotation);

/// Converts a screen-space tolerance to slide units at `viewportScale`.
/// Returns 0 for a non-positive scale rather than dividing by it: an infinite
/// tolerance would make every hit test succeed.
[[nodiscard]] double screenToleranceToSlide(double toleranceScreenPixels,
                                            double viewportScale);

} // namespace slideio::viewer::ui
```

- [ ] **Step 4: Write `AnnotationInteraction.cpp`**

```cpp
#include "slideio/viewer/ui/AnnotationInteraction.h"

namespace slideio::viewer::ui
{

DragOwner resolveDragOwner(Qt::MouseButton button,
                           Qt::KeyboardModifiers,
                           bool spaceHeld,
                           AnnotationTool active,
                           bool pressHitAnnotation)
{
    if (button == Qt::MiddleButton) {
        return DragOwner::Pan;
    }
    if (button != Qt::LeftButton) {
        return DragOwner::None;
    }
    if (spaceHeld) {
        return DragOwner::Pan;
    }
    if (active == AnnotationTool::Pan) {
        return pressHitAnnotation ? DragOwner::MoveAnnotation : DragOwner::Pan;
    }
    return DragOwner::Tool;
}

double screenToleranceToSlide(double toleranceScreenPixels, double viewportScale)
{
    if (viewportScale <= 0.0) {
        return 0.0;
    }
    return toleranceScreenPixels / viewportScale;
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 5: Wire into CMake**

`src/ui/CMakeLists.txt` — add to the library sources, after `src/AppPaths.cpp`:

```cmake
    src/AnnotationInteraction.cpp
```

`tests/CMakeLists.txt` — add to `slideio-viewer-ui-tests`:

```cmake
    ui/AnnotationInteractionTest.cpp
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R ui-tests --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationInteraction.h \
        src/ui/src/AnnotationInteraction.cpp \
        src/ui/CMakeLists.txt \
        tests/ui/AnnotationInteractionTest.cpp \
        tests/CMakeLists.txt
git commit -m "Add pure drag routing and screen-to-slide tolerance conversion"
```

---

### Task 6: `ui` — tool modes, middle-button pan, Space-pan

Routes the existing mouse handlers through `resolveDragOwner` and adds a way to switch tools. No annotations are drawn yet; this task is verifiable on navigation behaviour alone.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — public tool API, `activeToolChanged` signal, `keyReleaseEvent` override
- Modify: `src/ui/src/ViewportWidget.cpp` — `Impl` state (near `isPanning` at line 1371), `mousePressEvent` (3176), `mouseReleaseEvent` (3188), `mouseMoveEvent` (3199), `keyPressEvent` (3259), new `keyReleaseEvent`
- Modify: `src/ui/src/MainWindow.cpp` — Tools menu with two exclusive checkable actions

**Interfaces:**
- Consumes: `ui::AnnotationTool`, `ui::DragOwner`, `ui::resolveDragOwner` from Task 5
- Produces: `ViewportWidget::setActiveTool(AnnotationTool)`, `ViewportWidget::activeTool() const`, signal `ViewportWidget::activeToolChanged(AnnotationTool)`

- [ ] **Step 1: Add the tool API to `ViewportWidget.h`**

Add the include alongside the other project headers at the top:

```cpp
#include "slideio/viewer/ui/AnnotationInteraction.h"
```

In the `public:` section, after `void setColorMode(core::ColorMode mode);`:

```cpp
    /// The tool that owns left-drag. Pan is the default, so navigation behaves
    /// exactly as it did before annotations existed until the user picks a
    /// drawing tool.
    void setActiveTool(AnnotationTool tool);
    [[nodiscard]] AnnotationTool activeTool() const;
```

In `signals:`, after `void loadingFinished();`:

```cpp
    void activeToolChanged(AnnotationTool tool);
```

In `protected:`, after `void keyPressEvent(QKeyEvent* event) override;`:

```cpp
    void keyReleaseEvent(QKeyEvent* event) override;
```

- [ ] **Step 2: Add the state to `Impl`**

In `src/ui/src/ViewportWidget.cpp`, in `struct ViewportWidget::Impl`, next to `bool isPanning = false;` (line 1371):

```cpp
    AnnotationTool activeTool = AnnotationTool::Pan;
    bool spaceHeld = false;
    // Set on press and held until release: a tool change mid-drag must not
    // re-route a drag that has already begun.
    DragOwner dragOwner = DragOwner::None;
```

- [ ] **Step 3: Implement the tool accessors**

Add near the other simple accessors in `src/ui/src/ViewportWidget.cpp`:

```cpp
void ViewportWidget::setActiveTool(AnnotationTool tool)
{
    if (m_impl->activeTool == tool) {
        return;
    }
    m_impl->activeTool = tool;
    setCursor(tool == AnnotationTool::Pan ? Qt::ArrowCursor : Qt::CrossCursor);
    emit activeToolChanged(tool);
}

AnnotationTool ViewportWidget::activeTool() const
{
    return m_impl->activeTool;
}
```

- [ ] **Step 4: Route the mouse handlers through `resolveDragOwner`**

Replace `mousePressEvent`, `mouseReleaseEvent` and the panning branch of `mouseMoveEvent`:

```cpp
void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    // A drag already in flight keeps its owner: changing tools mid-drag must
    // not strand half-built state.
    if (m_impl->dragOwner != DragOwner::None) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    const DragOwner owner = resolveDragOwner(event->button(), event->modifiers(),
                                             m_impl->spaceHeld, m_impl->activeTool,
                                             false);

    if (owner == DragOwner::Pan) {
        m_impl->dragOwner = DragOwner::Pan;
        m_impl->isPanning = true;
        m_impl->lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (owner == DragOwner::Tool) {
        // Task 7 begins the rectangle here. Claim the drag so it does not fall
        // through to panning.
        m_impl->dragOwner = DragOwner::Tool;
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_impl->dragOwner == DragOwner::None) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_impl->dragOwner == DragOwner::Pan) {
        m_impl->isPanning = false;
    }

    m_impl->dragOwner = DragOwner::None;
    setCursor(m_impl->activeTool == AnnotationTool::Pan ? Qt::ArrowCursor : Qt::CrossCursor);
    event->accept();
}
```

In `mouseMoveEvent`, change the panning condition so a pan only continues while it owns the drag:

```cpp
    if (m_impl->dragOwner == DragOwner::Pan && m_impl->isPanning && m_impl->controller) {
```

The rest of `mouseMoveEvent` — the `cursorMoved` emission and the fall-through to the base class — is unchanged.

- [ ] **Step 5: Track the Space key**

In `keyPressEvent`, as the first statements inside the function:

```cpp
    // Auto-repeat would otherwise re-enter pan mode on every repeat.
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_impl->spaceHeld = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
```

Add the new handler next to it:

```cpp
void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_impl->spaceHeld = false;
        if (m_impl->dragOwner != DragOwner::Pan) {
            setCursor(m_impl->activeTool == AnnotationTool::Pan ? Qt::ArrowCursor
                                                                : Qt::CrossCursor);
        }
        event->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(event);
}
```

- [ ] **Step 6: Add the Tools menu**

In `src/ui/src/MainWindow.cpp`, add to `Impl`'s action members, after `QAction* manageSlideProfilesAction = nullptr;`:

```cpp
    QAction* panToolAction = nullptr;
    QAction* rectangleToolAction = nullptr;
```

In `createActions()`, after the `manageSlideProfilesAction` block:

```cpp
        panToolAction = new QAction("&Navigate", owner);
        panToolAction->setCheckable(true);
        panToolAction->setChecked(true);
        panToolAction->setShortcut(QKeySequence("V"));
        panToolAction->setStatusTip("Pan and select with the left mouse button");

        rectangleToolAction = new QAction("&Rectangle", owner);
        rectangleToolAction->setCheckable(true);
        rectangleToolAction->setShortcut(QKeySequence("R"));
        rectangleToolAction->setStatusTip("Draw a rectangular annotation by dragging");

        auto* toolGroup = new QActionGroup(owner);
        toolGroup->setExclusive(true);
        toolGroup->addAction(panToolAction);
        toolGroup->addAction(rectangleToolAction);
```

In `createMenus()`, before the Help menu:

```cpp
        QMenu* toolsMenu = owner->menuBar()->addMenu("&Tools");
        toolsMenu->addAction(panToolAction);
        toolsMenu->addAction(rectangleToolAction);
```

In `connectSignals()`, with the other action connections:

```cpp
        QObject::connect(panToolAction, &QAction::triggered, owner, [this]() {
            viewportWidget->setActiveTool(AnnotationTool::Pan);
        });
        QObject::connect(rectangleToolAction, &QAction::triggered, owner, [this]() {
            viewportWidget->setActiveTool(AnnotationTool::Rectangle);
        });
```

Add `#include <QActionGroup>` to the Qt include block.

- [ ] **Step 7: Build and install**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

- [ ] **Step 8: Verify navigation by hand**

Open a brightfield slide and check each:

1. **Navigate tool (default):** left-drag pans exactly as before. No regression.
2. **Middle-drag pans** — new, and works under both tools.
3. **Space + left-drag pans** under both tools. Cursor becomes an open hand on Space.
4. **Holding Space does not flicker or re-enter** — the auto-repeat guard.
5. **Rectangle tool (R or Tools ▸ Rectangle):** cursor becomes a crosshair, and **left-drag no longer pans** — the view stays put.
6. **V returns to Navigate** and left-drag pans again.
7. Switching tools mid-drag (start a left-drag pan, press R while held) does not jump or strand the view; the drag finishes as a pan.

- [ ] **Step 9: Run the suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 4 tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h \
        src/ui/src/ViewportWidget.cpp \
        src/ui/src/MainWindow.cpp
git commit -m "Add tool modes with middle-button and space panning"
```

---

### Task 7: `ui` — overlay painting and rectangle creation

Painting and drawing land together because neither is verifiable without the other: an overlay with nothing to draw shows nothing, and a tool whose output is invisible cannot be checked.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — `annotationModel()` accessor
- Modify: `src/ui/src/ViewportWidget.cpp` — `Impl` state, `paintGL` tail, `mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent`

**Interfaces:**
- Consumes: `app::AnnotationModel` from Task 4; `ui::screenToleranceToSlide`, `kHitToleranceScreenPixels` from Task 5; `core::RectangleGeometry`, `core::boundingBox` from Task 2
- Produces: `ViewportWidget::annotationModel() const -> app::AnnotationModel*`

- [ ] **Step 1: Add the model and drawing state**

In `ViewportWidget.h`, forward-declare above the class:

```cpp
namespace slideio::viewer::app { class AnnotationModel; }
```

In `public:`, after `activeTool()`:

```cpp
    /// The open slide's annotations. Owned by the widget, as the viewport
    /// controller is; never null. Cleared whenever the open slide changes.
    [[nodiscard]] app::AnnotationModel* annotationModel() const;
```

In `ViewportWidget.cpp`, add the includes:

```cpp
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
```

and, to the Qt block:

```cpp
#include <QPainter>
```

In `Impl`, next to the tool state from Task 6:

```cpp
    std::unique_ptr<app::AnnotationModel> annotations;
    bool drawing = false;
    core::PointF drawAnchorSlide;
    core::PointF drawCurrentSlide;
```

In the `ViewportWidget` constructor, before the body ends:

```cpp
    m_impl->annotations = std::make_unique<app::AnnotationModel>();
```

And the accessor:

```cpp
app::AnnotationModel* ViewportWidget::annotationModel() const
{
    return m_impl->annotations.get();
}
```

- [ ] **Step 2: Paint the overlay**

Add a private helper to `ViewportWidget.cpp` (declare it in the `private:` section of the header beside `installSceneOpenResult`):

```cpp
    void paintAnnotations(QPainter& painter);
```

```cpp
void ViewportWidget::paintAnnotations(QPainter& painter)
{
    if (!m_impl->controller) {
        return;
    }

    const core::Viewport& vp = m_impl->controller->viewport();

    // Slide -> screen is done point by point rather than by installing a
    // QTransform on the painter: a world transform scales the pen with the
    // geometry, and FR-ANN-11 fixes line width in SCREEN pixels.
    const auto toScreen = [&vp](core::PointF slidePos) {
        double sx = 0.0;
        double sy = 0.0;
        vp.slideToScreen(slidePos.x, slidePos.y, sx, sy);
        return QPointF(sx, sy);
    };

    const auto screenRect = [&toScreen](const core::RectF& box) {
        const QPointF a = toScreen(core::PointF{box.x, box.y});
        const QPointF b = toScreen(core::PointF{box.x + box.width, box.y + box.height});
        return QRectF(a, b).normalized();
    };

    painter.setRenderHint(QPainter::Antialiasing, true);

    const std::string& selectedId = m_impl->annotations->selectedId();

    for (const core::Annotation& annotation : m_impl->annotations->annotations()) {
        const core::AnnotationProperties& props = annotation.properties();
        const QColor stroke(props.color.r, props.color.g, props.color.b, props.color.a);
        QColor fill = stroke;
        fill.setAlphaF(props.fillOpacity);

        painter.setPen(QPen(stroke, props.lineWidth));
        painter.setBrush(fill);
        painter.drawRect(screenRect(annotation.boundingBox()));
    }

    // The selected annotation is drawn again, last, so a later annotation
    // cannot occlude the selection.
    if (!selectedId.empty()) {
        if (const core::Annotation* selected = m_impl->annotations->find(selectedId)) {
            const QRectF box = screenRect(selected->boundingBox());
            painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
        }
    }

    // The rectangle being dragged right now is not in the model yet.
    if (m_impl->drawing) {
        const QRectF preview =
            QRectF(toScreen(m_impl->drawAnchorSlide), toScreen(m_impl->drawCurrentSlide))
                .normalized();
        painter.setPen(QPen(QColor(0xE6, 0x7E, 0x22), 2.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(preview);
    }
}
```

Call it at the very end of `paintGL()`, after the `captureSnapshotTexture` branch (line 3172) — this ordering keeps annotations out of the zoom-snapshot texture, so they do not smear with the backdrop during a zoom:

```cpp
    QPainter painter(this);
    paintAnnotations(painter);
```

- [ ] **Step 3: Draw with the rectangle tool**

In `mousePressEvent`, replace the `DragOwner::Tool` branch from Task 6:

```cpp
    if (owner == DragOwner::Tool && m_impl->controller) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);

        m_impl->dragOwner = DragOwner::Tool;
        m_impl->drawing = true;
        m_impl->drawAnchorSlide = core::PointF{slideX, slideY};
        m_impl->drawCurrentSlide = m_impl->drawAnchorSlide;
        event->accept();
        update();
        return;
    }
```

In `mouseMoveEvent`, before the `cursorMoved` emission:

```cpp
    if (m_impl->dragOwner == DragOwner::Tool && m_impl->drawing && m_impl->controller) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);
        m_impl->drawCurrentSlide = core::PointF{slideX, slideY};
        event->accept();
        update();
    }
```

In `mouseReleaseEvent`, before the owner is reset to `None`:

```cpp
    if (m_impl->dragOwner == DragOwner::Tool && m_impl->drawing) {
        m_impl->drawing = false;

        const core::AnnotationGeometry geometry =
            core::RectangleGeometry{m_impl->drawAnchorSlide, m_impl->drawCurrentSlide};
        const core::RectF box = core::boundingBox(geometry);

        // A press with no drag is a misclick, not a zero-area shape. The
        // geometry is normalised by boundingBox, so a right-to-left or
        // bottom-to-top drag produces a valid positive-extent rectangle here.
        if (box.width > 0.0 && box.height > 0.0) {
            const std::string id =
                m_impl->annotations->add(core::AnnotationType::Rectangle, geometry);
            m_impl->annotations->setSelected(id);
        }
        update();
    }
```

- [ ] **Step 4: Build and install**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

- [ ] **Step 5: Verify drawing by hand**

Open a brightfield slide, press **R**, then check:

1. Dragging draws a dashed preview that follows the cursor.
2. Releasing leaves a filled orange rectangle with a white dashed selection outline.
3. **Panning moves the rectangle with the tissue** — it is anchored to slide coordinates, not to the screen.
4. **Zooming scales the rectangle with the tissue, but its outline stays the same thickness.** This is FR-ANN-11's screen-space line width.
5. **During a zoom the rectangle does not smear with the blurry backdrop** — the post-snapshot placement.
6. **Review Focus 2:** drag right-to-left and bottom-to-top. The rectangle appears correctly, not inverted or invisible.
7. **A click with no drag creates nothing.**
8. Draw several rectangles; all persist and all stay anchored.

- [ ] **Step 6: Run the suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 4 tests pass.

- [ ] **Step 7: Commit**

Record the Task 1 spike's outcome in the message, since nothing else does:

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h \
        src/ui/src/ViewportWidget.cpp
git commit -m "Draw annotations over the viewport and add the rectangle tool

QPainter over the FBO tile renderer was verified before this was built: it
draws correctly, leaves tile rendering intact, and -- painted after
captureSnapshotTexture -- stays out of the zoom snapshot, so annotations do
not smear with the blurry backdrop during a zoom."
```

---

### Task 8: `ui` — selection, move, delete, and slide-change clearing

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — declare `resetAnnotationState()` in `private:`
- Modify: `src/ui/src/ViewportWidget.cpp` — `Impl` state, `mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent`, `keyPressEvent`, `closeSlide` (2220), `installSceneOpenResult` (2325)
- Modify: `src/ui/src/MainWindow.cpp` — reset the tool to Navigate when no slide is open

**Interfaces:**
- Consumes: everything from Tasks 2–7
- Produces: no new public API

- [ ] **Step 1: Hit-test on press and feed the result to the router**

In `mousePressEvent`, compute the hit before resolving the owner, replacing the `false` argument:

```cpp
    std::string pressHitId;
    if (m_impl->controller) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);
        const double tolerance = screenToleranceToSlide(kHitToleranceScreenPixels,
                                                        m_impl->controller->viewport().scale());
        pressHitId = m_impl->annotations->hitTest(core::PointF{slideX, slideY}, tolerance);
        m_impl->moveLastSlide = core::PointF{slideX, slideY};
    }

    const DragOwner owner = resolveDragOwner(event->button(), event->modifiers(),
                                             m_impl->spaceHeld, m_impl->activeTool,
                                             !pressHitId.empty());
```

Add to `Impl`:

```cpp
    core::PointF moveLastSlide;
    std::string movingId;
    bool moved = false;
```

Add the move branch after the `DragOwner::Pan` branch:

```cpp
    if (owner == DragOwner::MoveAnnotation) {
        m_impl->dragOwner = DragOwner::MoveAnnotation;
        m_impl->movingId = pressHitId;
        m_impl->moved = false;
        m_impl->annotations->setSelected(pressHitId);
        setCursor(Qt::SizeAllCursor);
        event->accept();
        update();
        return;
    }
```

And, in the `DragOwner::Pan` branch, record whether this press hit nothing so a click with no drag can clear the selection:

```cpp
        m_impl->movingId.clear();
        m_impl->moved = false;
```

- [ ] **Step 2: Move on drag**

In `mouseMoveEvent`, beside the drawing branch:

```cpp
    if (m_impl->dragOwner == DragOwner::MoveAnnotation && m_impl->controller
        && !m_impl->movingId.empty()) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(event->pos().x(), event->pos().y(), slideX, slideY);

        const double dx = slideX - m_impl->moveLastSlide.x;
        const double dy = slideY - m_impl->moveLastSlide.y;
        m_impl->moveLastSlide = core::PointF{slideX, slideY};

        if (const core::Annotation* current = m_impl->annotations->find(m_impl->movingId)) {
            const core::RectF box = current->boundingBox();
            m_impl->annotations->setGeometry(
                m_impl->movingId,
                core::RectangleGeometry{
                    core::PointF{box.x + dx, box.y + dy},
                    core::PointF{box.x + box.width + dx, box.y + box.height + dy}});
            m_impl->moved = true;
        }
        event->accept();
        update();
    }
```

Also mark a pan as moved, so a pan-drag is not mistaken for a selection click:

```cpp
    if (m_impl->dragOwner == DragOwner::Pan && m_impl->isPanning && m_impl->controller) {
        m_impl->moved = true;
```

- [ ] **Step 3: Clear the selection on a click that hit nothing**

In `mouseReleaseEvent`, before the owner is reset:

```cpp
    // A left click with no drag and no annotation under it clears the
    // selection; the Navigate tool doubles as select.
    if (m_impl->dragOwner == DragOwner::Pan && !m_impl->moved) {
        m_impl->annotations->clearSelection();
        update();
    }
    m_impl->movingId.clear();
    m_impl->moved = false;
```

- [ ] **Step 4: Delete and Escape**

In `keyPressEvent`, after the Space branch:

```cpp
    if (event->key() == Qt::Key_Delete && !m_impl->annotations->selectedId().empty()) {
        m_impl->annotations->remove(m_impl->annotations->selectedId());
        event->accept();
        update();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        // Cancels a draw in progress, then falls back to leaving the tool.
        if (m_impl->drawing) {
            m_impl->drawing = false;
            m_impl->dragOwner = DragOwner::None;
        } else {
            setActiveTool(AnnotationTool::Pan);
        }
        event->accept();
        update();
        return;
    }
```

- [ ] **Step 5: Clear the model whenever the slide changes**

Two call sites cover every path. Write this helper first, declaring it in the header's `private:` section beside `paintAnnotations`:

```cpp
void ViewportWidget::resetAnnotationState()
{
    // Review Focus 3: annotations must never survive into a different slide.
    // Nothing here is persisted, so a stale model can only produce a clinical
    // misassociation -- one slide's marks drawn over another's tissue.
    m_impl->annotations->clear();
    m_impl->drawing = false;
    m_impl->dragOwner = DragOwner::None;
    m_impl->movingId.clear();
    m_impl->moved = false;
}
```

Call it in exactly two places:

1. **`ViewportWidget::closeSlide()`** (line 2220) — as the first statement, so the model is empty well before `emit slideClosed()` at line 2251 reaches MainWindow.
2. **`ViewportWidget::installSceneOpenResult()`** (line 2325) — as the first statement. This is the single choke point for all three open paths (called from lines 1952, 2215 and 2320), so one call covers opening a slide, switching scene, and opening an associated image.

Scene switches inside one file clear too, and deliberately: geometry is in level-0 pixels of the scene it was drawn on, so scene 1's annotations mean nothing on scene 2.

In `src/ui/src/MainWindow.cpp`, in the `slideClosed` handler, reset the tool so a drawing tool is never active with no slide:

```cpp
            m_impl->viewportWidget->setActiveTool(AnnotationTool::Pan);
            m_impl->panToolAction->setChecked(true);
```

- [ ] **Step 6: Keep the menu in step with the widget**

In `connectSignals()`, so Escape and the slide-close reset update the checkmarks:

```cpp
        QObject::connect(viewportWidget, &ViewportWidget::activeToolChanged, owner,
                         [this](AnnotationTool tool) {
                             panToolAction->setChecked(tool == AnnotationTool::Pan);
                             rectangleToolAction->setChecked(tool == AnnotationTool::Rectangle);
                         });
```

- [ ] **Step 7: Build and install**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

- [ ] **Step 8: Verify the full slice by hand**

Open a brightfield slide, draw three rectangles with **R**, press **V**, then:

1. **Click a rectangle** → white dashed selection outline moves to it.
2. **Click empty tissue** → selection clears.
3. **Drag a rectangle** → it moves with the cursor and stays where released.
4. **Drag empty tissue** → the view pans; no annotation moves.
5. **Delete** removes the selected rectangle; others are untouched.
6. **Zoom to 40×, then click 6-7 pixels outside an edge** → still selects. Zoom back out to fit; the same physical offset still selects. The tolerance is screen-space.
7. **Review Focus 4:** start a left-drag pan, press **R** while still held, release. The drag finishes as a pan and no rectangle is created.
8. **Review Focus 5:** start drawing a rectangle, drag outside the window, release there. The rectangle is created and the drag ends — nothing stays stuck to the cursor.
9. **Escape** during a draw cancels it, leaving nothing behind.
10. **Review Focus 3:** draw annotations, then open a **different** slide. **The viewport must show no annotations.** Repeat with File ▸ Close, then open another slide.
11. With no slide open, the Tools menu shows Navigate checked.

- [ ] **Step 9: Run the suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 4 tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp src/ui/src/MainWindow.cpp
git commit -m "Select, move and delete annotations, and clear them on slide change"
```

---

## Done when

- `ctest --test-dir build/build -C Release --output-on-failure` reports 4 suites passing.
- Every manual check in Tasks 6, 7 and 8 holds on a real brightfield slide.
- A human has reviewed every commit on `annotation-foundation`.

Then use `superpowers:requesting-code-review`, and `superpowers:finishing-a-development-branch` to integrate.
