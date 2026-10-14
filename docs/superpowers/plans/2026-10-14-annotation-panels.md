# Annotation Panels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an annotation list panel and an annotation properties panel, and in doing so move `AnnotationModel` out of the presentation layer.

**Architecture:** `MainWindow` takes ownership of `AnnotationModel` and injects it into `ViewportWidget` by constructor reference; the two new docks observe the same model through null-tolerant setters. Mutation that must be undoable moves into app-layer free functions over `(AnnotationModel&, UndoRedoStack&)` so a panel and the viewport share one implementation. Everything worth testing is extracted into pure functions over plain data, because no test binary may construct a widget.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+, Conan 2, Catch2 v3, four ctest suites (core, app, infra, ui).

**Spec:** `docs/superpowers/specs/2026-10-14-annotation-panels-design.md` (committed `84fe820`, amended `8a86c94`)

## Global Constraints

- **No test binary creates a `QApplication`, so no test may construct a widget.** A `QObject` without a `QCoreApplication` is fine — `tests/app/AnnotationModelTest.cpp` already constructs `AnnotationModel`. Panels are verified only by the manual group V list.
- **Strict downward layering:** `core` (Qt-free, links no Qt target) → `app` (core + Qt6::Core) → `infra` → `ui`. Never add a Qt include to a core header.
- **Every source and test file is listed explicitly in CMake.** `src/app/CMakeLists.txt`, `src/ui/CMakeLists.txt` and `tests/CMakeLists.txt` all enumerate their `.cpp` files. The only glob is `file(GLOB_RECURSE _ui_headers ...)` in `src/ui/CMakeLists.txt`, which exists so AUTOMOC finds `Q_OBJECT` headers under `include/` — it does **not** pick up `.cpp` files. A new `.cpp` that is not added by hand simply does not build, and the link error names the symbol, not the cause.
- **Never run a Qt-linked test executable directly.** `app-tests`, `infra-tests` and `ui-tests` find their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property that only applies under `ctest`; running the `.exe` pops a modal Windows error box that blocks until a human dismisses it. Only `core-tests` is safe to run directly.
- **A running viewer silently turns `cmake --install` into a no-op that still exits 0.** Kill the viewer before installing, and verify the artifact by comparing timestamps of `build/install/release/bin/slideio-viewer.exe` and `build/build/Release/slideio-viewer.exe` — not by exit code.
- **Coding conventions:** 4-space indent, 120-char lines, Allman braces for classes/functions and K&R for control flow, `#pragma once`, `m_camelCase` members, `kPascalCase` constants, include order own-header → project → Qt → std.
- **Workflow:** features land as a series of direct commits to `main` (unprotected); no PRs, no feature branches.
- **All AI-generated code must be human-reviewed before commit.**

## Review Focus

Five conditions the spec implies but does not pin with a test. Each has a test added to the task that owns the code.

1. **A degenerate annotation bounding box** — a perfectly horizontal freehand line has zero height, and `fitToRect` would divide by it. Expect: fit on the non-degenerate axis, no NaN, no infinite scale. *(Task 1)*
2. **Deleting the currently-selected annotation** — `AnnotationModel::remove` clears the selection, and `AnnotationEditCommand::apply` re-selects on restore (`AnnotationEditCommand.cpp:74-78`, deliberate: an annotation restored outside the current view has to be findable). Expect: after the delete the selection is empty; after undo the annotation is back *and selected*; after redo it is gone and the selection is empty again. The seam must not disturb that chain. *(Task 2)*
3. **The `"<Type> N"` fallback while a filter is active** — N must count across all annotations in creation order, not across the filtered rows, or a shape is renumbered by typing in the search box. Expect: filtering to one row shows that shape's original number. *(Task 6)*
4. **Properties arriving out of range from the model, not the widgets** — a hand-edited file can carry `lineWidth` 0 or a `fillOpacity` of 4.0, and the properties panel round-trips through `propertiesEdit`. Expect: clamped to 1-5 and 0-1, and the clamp itself counts as a change so it is persisted rather than silently re-clamped on every load. *(Task 8)*
5. **An empty slide** — zero annotations must give an empty list and a title that reads as an empty slide rather than a failure. Expect: `annotationRows` returns empty, `annotationDockTitle(0, 0)` gives `"Annotations (0)"`. *(Task 6)*

---

## File Structure

**Create:**
- `src/core/src/Viewport.cpp` *(modify)* and its header — `fitToRect`
- `src/app/include/slideio/viewer/app/AnnotationEditing.h` — undoable mutation seam
- `src/app/src/AnnotationEditing.cpp`
- `src/ui/include/slideio/viewer/ui/AnnotationShapeName.h` — shared shape naming
- `src/ui/src/AnnotationShapeName.cpp`
- `src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h` — pure list logic
- `src/ui/src/AnnotationListPolicy.cpp`
- `src/ui/include/slideio/viewer/ui/AnnotationListPanel.h` — dock widget
- `src/ui/src/AnnotationListPanel.cpp`
- `src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h` — pure edit logic
- `src/ui/src/AnnotationPropertiesEdit.cpp`
- `src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h` — dock widget
- `src/ui/src/AnnotationPropertiesPanel.cpp`
- `tests/app/AnnotationEditingTest.cpp`
- `tests/ui/AnnotationListPolicyTest.cpp`
- `tests/ui/AnnotationPropertiesEditTest.cpp`

**Modify:**
- `src/core/include/slideio/viewer/core/Viewport.h`, `src/core/src/Viewport.cpp`
- `src/ui/include/slideio/viewer/ui/ViewportController.h`, `src/ui/src/ViewportController.cpp`
- `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, `src/ui/src/ViewportWidget.cpp`
- `src/ui/include/slideio/viewer/ui/MainWindow.h`, `src/ui/src/MainWindow.cpp`
- `src/app/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`
- `tests/core/ViewportTest.cpp`

---

### Task 1: `Viewport::fitToRect` — the arithmetic behind navigate-to-annotation

`ViewportWidget` has only `fitToSlide()`. Double-clicking a list row must bring one annotation into view, and the arithmetic is the only part worth testing — so it lands in core, where a test needs nothing but the class.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/Viewport.h:29` (after `fitToSlide`)
- Modify: `src/core/src/Viewport.cpp:103-115` (after `fitToSlide`)
- Test: `tests/core/ViewportTest.cpp` (append)

**Interfaces:**
- Consumes: `core::Rect<double>` (alias `core::RectF`, fields `x`, `y`, `width`, `height`), existing `Viewport` members `m_centerX`, `m_centerY`, `m_scale`, `m_screenWidth`, `m_screenHeight`.
- Produces: `void Viewport::fitToRect(const Rect<double>& target, double marginFraction)` — centres on `target` and sets `m_scale` so the rect plus margin fits both axes.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/ViewportTest.cpp`:

```cpp
TEST_CASE("fitToRect centres on the target and fits it", "[core][Viewport]")
{
    core::Viewport viewport(0.0, 0.0, 1.0, 800, 600);
    viewport.fitToRect(core::RectF{100.0, 200.0, 400.0, 300.0}, 0.1);

    REQUIRE(viewport.centerX() == Approx(300.0));
    REQUIRE(viewport.centerY() == Approx(350.0));

    // 400 wide into 800 screen px with 10% margin on each side: 640/400 = 1.6.
    // 300 tall into 600 screen px likewise: 480/300 = 1.6. Both agree here.
    REQUIRE(viewport.scale() == Approx(1.6));
}

TEST_CASE("fitToRect fits the tighter axis", "[core][Viewport]")
{
    core::Viewport viewport(0.0, 0.0, 1.0, 800, 600);
    // Wide and short: width is the binding constraint.
    viewport.fitToRect(core::RectF{0.0, 0.0, 800.0, 100.0}, 0.0);
    REQUIRE(viewport.scale() == Approx(1.0));
}

TEST_CASE("fitToRect survives a degenerate box", "[core][Viewport]")
{
    // Review Focus 1. A perfectly horizontal freehand line has zero height, and
    // a vertical one zero width. Fitting the zero axis would divide by it; the
    // annotation still has to end up centred and visible rather than at an
    // infinite or NaN scale.
    core::Viewport viewport(0.0, 0.0, 1.0, 800, 600);
    viewport.fitToRect(core::RectF{100.0, 50.0, 400.0, 0.0}, 0.1);

    REQUIRE(viewport.centerX() == Approx(300.0));
    REQUIRE(viewport.centerY() == Approx(50.0));
    REQUIRE(viewport.scale() == Approx(1.6));
    REQUIRE(std::isfinite(viewport.scale()));

    // Both axes degenerate -- a point annotation, or a trace whose vertices all
    // coincide. There is no meaningful zoom, so the previous one is kept and
    // only the centre moves.
    core::Viewport point(0.0, 0.0, 2.5, 800, 600);
    point.fitToRect(core::RectF{10.0, 20.0, 0.0, 0.0}, 0.1);
    REQUIRE(point.centerX() == Approx(10.0));
    REQUIRE(point.centerY() == Approx(20.0));
    REQUIRE(point.scale() == Approx(2.5));
}

TEST_CASE("fitToRect ignores a screen with no area", "[core][Viewport]")
{
    // Matches fitToSlide's guard: a widget that has not been laid out yet.
    core::Viewport viewport(5.0, 6.0, 1.0, 0, 0);
    viewport.fitToRect(core::RectF{100.0, 200.0, 400.0, 300.0}, 0.1);
    REQUIRE(viewport.centerX() == Approx(5.0));
    REQUIRE(viewport.centerY() == Approx(6.0));
    REQUIRE(viewport.scale() == Approx(1.0));
}
```

Add `#include <cmath>` to the test file's includes if it is not already there, and confirm `catch2/catch_approx.hpp` is included for `Approx` — check the top of `tests/core/ViewportTest.cpp` and match whatever the existing cases use.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```

Expected: compile error, `'fitToRect': is not a member of 'slideio::viewer::core::Viewport'`.

- [ ] **Step 3: Declare it**

In `src/core/include/slideio/viewer/core/Viewport.h`, directly after `void fitToSlide(int slideWidth, int slideHeight);`:

```cpp
    /// Centres on `target` and zooms so it fits the screen with
    /// `marginFraction` of the screen left free on each side (0.1 leaves 10%
    /// top, bottom, left and right). Used to bring one annotation into view.
    ///
    /// A degenerate axis is skipped rather than fitted: a perfectly horizontal
    /// freehand line has zero height, and fitting that axis would divide by
    /// zero. When both axes are degenerate there is no zoom the box implies, so
    /// the current scale is kept and only the centre moves.
    void fitToRect(const Rect<double>& target, double marginFraction);
```

- [ ] **Step 4: Implement it**

In `src/core/src/Viewport.cpp`, directly after `fitToSlide`:

```cpp
void Viewport::fitToRect(const Rect<double>& target, double marginFraction)
{
    if (m_screenWidth <= 0 || m_screenHeight <= 0) {
        return;
    }

    m_centerX = target.x + target.width / 2.0;
    m_centerY = target.y + target.height / 2.0;

    // Clamped so a caller cannot ask for a margin that consumes the whole
    // screen, which would leave no room for the annotation it is trying to show.
    const double margin = std::max(0.0, std::min(marginFraction, 0.45));
    const double usableWidth = m_screenWidth * (1.0 - 2.0 * margin);
    const double usableHeight = m_screenHeight * (1.0 - 2.0 * margin);

    // Each axis contributes a candidate scale only if it has extent. A zero
    // width or height is not a malformed annotation -- a freehand line drawn
    // along one axis produces exactly that -- so it is skipped, not rejected.
    double scale = 0.0;
    if (target.width > 0.0) {
        scale = usableWidth / target.width;
    }
    if (target.height > 0.0) {
        const double scaleY = usableHeight / target.height;
        scale = scale > 0.0 ? std::min(scale, scaleY) : scaleY;
    }

    // Both axes degenerate: a point, or a trace whose vertices coincide. There
    // is no zoom the box implies, so the current one is kept.
    if (scale > 0.0) {
        m_scale = scale;
    }
}
```

`<algorithm>` is already included for `std::min` in `fitToSlide`; confirm it is, and add it if not.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/Viewport.h src/core/src/Viewport.cpp tests/core/ViewportTest.cpp
git commit -m "Add Viewport::fitToRect for navigating to one annotation"
```

---

### Task 2: The app-layer mutation seam

`ViewportWidget::pushCreateCommand` and `pushDeleteCommand` touch no widget state — they are app-layer operations stranded in a presentation widget. A panel that deleted an annotation by calling `model.remove(id)` would produce an edit undo cannot reach. This task creates the shared seam and gives annotation mutation its first automated coverage.

**Files:**
- Create: `src/app/include/slideio/viewer/app/AnnotationEditing.h`
- Create: `src/app/src/AnnotationEditing.cpp`
- Modify: `src/app/CMakeLists.txt:2-10`
- Create: `tests/app/AnnotationEditingTest.cpp`
- Modify: `tests/CMakeLists.txt:30-34`

**Interfaces:**
- Consumes: `app::AnnotationModel` (`find`, `remove`, `indexOf`, `annotations`, `restore`), `app::UndoRedoStack::push`, `app::AnnotationEditCommand::create(model, id, before, after, description)`, `core::Annotation`, `core::AnnotationProperties`.
- Produces:
  - `bool app::deleteWithUndo(AnnotationModel&, UndoRedoStack&, const std::string& id, std::string description)`
  - `bool app::setPropertiesWithUndo(AnnotationModel&, UndoRedoStack&, const std::string& id, core::AnnotationProperties, std::string description)`

- [ ] **Step 1: Write the failing tests**

Create `tests/app/AnnotationEditingTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationEditing.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"

#include <string>

using namespace slideio::viewer;

namespace
{

core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    // RectangleGeometry stores two corners, not an origin-plus-size rect.
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

/// Adds a 10x10 annotation at `x0` and returns its id.
std::string addRect(app::AnnotationModel& model, double x0)
{
    return model.add(rect(x0, 0.0, x0 + 10.0, 10.0));
}

} // namespace

TEST_CASE("deleteWithUndo removes the annotation and records one step", "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);

    REQUIRE(app::deleteWithUndo(model, stack, id, "Delete Rectangle"));

    REQUIRE(model.annotations().empty());
    REQUIRE(stack.canUndo());
    REQUIRE(stack.undoDescription() == "Delete Rectangle");
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("undoing a delete puts the annotation back at its original index",
          "[app][AnnotationEditing]")
{
    // The vector's order is paint order and hit-test order both, so an
    // annotation restored at the end would silently start painting on top of
    // everything drawn after it and winning clicks it used to lose.
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string first = addRect(model, 0.0);
    const std::string second = addRect(model, 20.0);
    const std::string third = addRect(model, 40.0);

    REQUIRE(app::deleteWithUndo(model, stack, second, "Delete Rectangle"));
    stack.undo();

    REQUIRE(model.annotations().size() == 3);
    REQUIRE(model.indexOf(first) == 0);
    REQUIRE(model.indexOf(second) == 1);
    REQUIRE(model.indexOf(third) == 2);
}

TEST_CASE("deleting a selected annotation clears the selection, and undo brings it back",
          "[app][AnnotationEditing]")
{
    // Review Focus 2. Three states, and the seam must not disturb any of them:
    // remove() clears the selection when it named the removed annotation;
    // AnnotationEditCommand::apply re-selects on restore, deliberately, so an
    // annotation put back outside the current view can be found
    // (AnnotationEditCommand.cpp:74-78); and redoing the delete clears it again
    // through the same remove().
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);
    model.setSelected(id);
    REQUIRE(model.selectedId() == id);

    REQUIRE(app::deleteWithUndo(model, stack, id, "Delete Rectangle"));
    REQUIRE(model.selectedId().empty());

    stack.undo();
    REQUIRE(model.find(id) != nullptr);
    REQUIRE(model.selectedId() == id);

    stack.redo();
    REQUIRE(model.find(id) == nullptr);
    REQUIRE(model.selectedId().empty());
}

TEST_CASE("deleteWithUndo refuses an id the model does not hold", "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    addRect(model, 0.0);

    REQUIRE_FALSE(app::deleteWithUndo(model, stack, "no-such-id", "Delete Rectangle"));
    REQUIRE(model.annotations().size() == 1);
    REQUIRE_FALSE(stack.canUndo());
}

TEST_CASE("setPropertiesWithUndo stores the properties and records one step",
          "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);

    core::AnnotationProperties properties = model.find(id)->properties();
    properties.label = "Tumour front";
    properties.classification = "Tumor";

    REQUIRE(app::setPropertiesWithUndo(model, stack, id, properties, "Rename annotation"));

    REQUIRE(model.find(id)->properties().label == "Tumour front");
    REQUIRE(stack.undoDescription() == "Rename annotation");
}

TEST_CASE("a properties edit round-trips through undo and redo", "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);

    core::AnnotationProperties properties = model.find(id)->properties();
    properties.label = "Second look";
    REQUIRE(app::setPropertiesWithUndo(model, stack, id, properties, "Rename annotation"));

    stack.undo();
    REQUIRE(model.find(id)->properties().label.empty());

    stack.redo();
    REQUIRE(model.find(id)->properties().label == "Second look");
}

TEST_CASE("an unchanged properties edit records nothing and does not restamp",
          "[app][AnnotationEditing]")
{
    // Tabbing through a field without touching it must not push a step, and
    // must not restamp modifiedAt -- autosave would then write a change that
    // did not happen, which is the defect 9826da6 fixed on the Escape path.
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);
    const auto modifiedBefore = model.find(id)->metadata().modifiedAt;

    REQUIRE_FALSE(app::setPropertiesWithUndo(model, stack, id,
                                             model.find(id)->properties(), "Rename annotation"));

    REQUIRE_FALSE(stack.canUndo());
    REQUIRE(model.find(id)->metadata().modifiedAt == modifiedBefore);
}

TEST_CASE("setPropertiesWithUndo refuses an id the model does not hold",
          "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;

    REQUIRE_FALSE(app::setPropertiesWithUndo(model, stack, "no-such-id",
                                             core::AnnotationProperties{}, "Rename annotation"));
    REQUIRE_FALSE(stack.canUndo());
}
```

Check the `rect` helper against the one already in `tests/app/AnnotationModelTest.cpp` and copy that version verbatim if the `RectangleGeometry` field name differs — the two files must agree.

- [ ] **Step 2: Register the test file and run it to verify it fails**

In `tests/CMakeLists.txt`, add to the `slideio-viewer-app-tests` source list:

```cmake
    app/AnnotationEditingTest.cpp
```

```bash
cmake --build build/build --config Release --target slideio-viewer-app-tests
```

Expected: `Cannot open include file: 'slideio/viewer/app/AnnotationEditing.h'`.

- [ ] **Step 3: Write the header**

Create `src/app/include/slideio/viewer/app/AnnotationEditing.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <string>

namespace slideio::viewer::app
{

class AnnotationModel;
class UndoRedoStack;

/// Undoable mutation of one annotation.
///
/// These live here rather than on the widget that happens to call them because
/// F gives the model a second mutator: a panel calling AnnotationModel::remove
/// directly would produce an edit the undo stack never saw, and the user would
/// press Ctrl+Z on an annotation that does not come back. Both functions mutate
/// through the model's own API first and record afterwards, which is the order
/// AnnotationEditCommand is built for -- execute() runs only on redo.
///
/// `description` is supplied by the caller rather than generated here: the
/// wording is user-facing ui text ("Delete Polygon"), and app must not reach
/// into Qt's translation machinery to produce it.

/// Removes `id` and records the removal. False when the model does not hold
/// `id`, in which case nothing is removed and nothing is pushed.
bool deleteWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                    std::string description);

/// Stores `properties` on `id` and records the change. False when the model
/// does not hold `id`, and also when `properties` equals what is already
/// stored: an edit that changes nothing must not push a step the user would
/// have to press Ctrl+Z twice to get past, and must not restamp modifiedAt --
/// autosave would otherwise write a modification that did not happen.
bool setPropertiesWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                           core::AnnotationProperties properties, std::string description);

} // namespace slideio::viewer::app
```

- [ ] **Step 4: Write the implementation**

Create `src/app/src/AnnotationEditing.cpp`:

```cpp
#include "slideio/viewer/app/AnnotationEditing.h"

#include "slideio/viewer/app/AnnotationEditCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"

#include <utility>

namespace slideio::viewer::app
{

namespace
{

/// Equality for AnnotationProperties, which has no operator==. Written here
/// rather than on the struct because core is the wrong place to decide what
/// "the user changed nothing" means for an editor's field set.
bool sameProperties(const core::AnnotationProperties& a, const core::AnnotationProperties& b)
{
    return a.label == b.label && a.classification == b.classification && a.notes == b.notes
           && a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b
           && a.color.a == b.color.a && a.lineWidth == b.lineWidth
           && a.fillOpacity == b.fillOpacity;
}

} // namespace

bool deleteWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                    std::string description)
{
    const core::Annotation* stored = model.find(id);
    if (stored == nullptr) {
        return false;
    }

    // Copied before the removal: find() returns a pointer any mutation
    // invalidates, and this copy is what undo puts back.
    core::Annotation before = *stored;

    // Copied too: `id` may alias the caller's own storage, and remove() can
    // destroy what it names.
    const std::string targetId = id;

    // Built BEFORE the removal, and pushed after. AnnotationEditCommand captures
    // the annotation's index in its constructor (AnnotationEditCommand.cpp:40,
    // whose comment says a delete is recorded before the removal), and undo puts
    // the annotation back at that index. Built after the remove, indexOf would
    // return kNotFound, insertAt would clamp it to the end, and an undone delete
    // would silently promote the annotation over everything drawn after it --
    // changing which shape paints on top and which one a click finds.
    auto command = AnnotationEditCommand::create(model, targetId, std::move(before), std::nullopt,
                                                 std::move(description));

    if (!model.remove(targetId)) {
        return false;
    }

    stack.push(std::move(command));
    return true;
}

bool setPropertiesWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                           core::AnnotationProperties properties, std::string description)
{
    const core::Annotation* stored = model.find(id);
    if (stored == nullptr) {
        return false;
    }
    if (sameProperties(stored->properties(), properties)) {
        return false;
    }

    const std::string targetId = id;
    core::Annotation before = *stored;

    // Built from the before state so everything except the properties -- id,
    // geometry, createdAt, author -- is carried across untouched. setProperties
    // restamps modifiedAt, which is correct here: this is a real edit.
    core::Annotation after = before;
    after.setProperties(std::move(properties));

    if (!model.restore(after)) {
        return false;
    }

    stack.push(AnnotationEditCommand::create(model, targetId, std::move(before), std::move(after),
                                             std::move(description)));
    return true;
}

} // namespace slideio::viewer::app
```

Note `restore` rather than a hypothetical `setProperties` on the model: the model has no per-field properties setter, and `restore` is exactly "put this whole annotation back under its own id, metadata and all", which is what an already-stamped `after` needs.

- [ ] **Step 5: Register the source file**

In `src/app/CMakeLists.txt`, add to the `add_library(slideio-viewer-app STATIC ...)` source list, after `src/AnnotationEditCommand.cpp`:

```cmake
    src/AnnotationEditing.cpp
```

No header entry is needed — `AnnotationEditing.h` declares no `Q_OBJECT`, so AUTOMOC has nothing to do with it.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-app-tests
ctest --test-dir build/build -C Release -R app-tests --output-on-failure
```

Expected: PASS. If "an unchanged properties edit records nothing" fails, check that `sameProperties` compares every field of `AnnotationProperties` — a field left out makes a real edit look like a no-op and silently discards the user's change.

- [ ] **Step 7: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationEditing.h src/app/src/AnnotationEditing.cpp src/app/CMakeLists.txt tests/app/AnnotationEditingTest.cpp tests/CMakeLists.txt
git commit -m "Add an app-layer seam for undoable annotation mutation"
```

---

### Task 3: Promote `shapeName`, and route deletion through the seam

`shapeName` sits in an anonymous namespace in `ViewportWidget.cpp:120`. After F, three callers need it, and it is a `std::visit` over the geometry variant — one of the exhaustive dispatch sites a new shape must update — so copying it would create a second place to forget.

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationShapeName.h`
- Create: `src/ui/src/AnnotationShapeName.cpp`
- Modify: `src/ui/src/ViewportWidget.cpp:100-123` (remove `ShapeNameVisitor` and `shapeName`), `:2831-2843` (`pushDeleteCommand`)
- Modify: `src/ui/CMakeLists.txt:9-35`

**Interfaces:**
- Consumes: `core::Annotation::geometry()`, `core::RectangleGeometry`, `core::PolygonGeometry`, `app::deleteWithUndo` (Task 2).
- Produces: `QString ui::shapeName(const core::Annotation&)` — returns the translated `"Rectangle"`, `"Polygon"` or `"Freehand Line"`.

- [ ] **Step 1: Create the header**

Create `src/ui/include/slideio/viewer/ui/AnnotationShapeName.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QString>

namespace slideio::viewer::ui
{

/// The user's word for an annotation's shape: "Rectangle", "Polygon" or
/// "Freehand Line". Used by the viewport's undo descriptions, by MainWindow
/// when it labels a deletion requested from the list panel, and by the list
/// panel's Type column.
///
/// One std::visit over the geometry variant, in one place. A new geometry type
/// fails to compile here until it is named, which is the point -- three copies
/// of this dispatch would be three chances to ship a shape the user sees
/// labelled as nothing at all.
QString shapeName(const core::Annotation& annotation);

} // namespace slideio::viewer::ui
```

- [ ] **Step 2: Move the implementation**

Create `src/ui/src/AnnotationShapeName.cpp`, moving `ShapeNameVisitor` and `shapeName` out of `ViewportWidget.cpp` unchanged except for the namespace and the `tr` context:

```cpp
#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QCoreApplication>

#include <variant>

namespace slideio::viewer::ui
{

namespace
{

struct ShapeNameVisitor
{
    QString operator()(const core::RectangleGeometry&) const
    {
        return QCoreApplication::translate("AnnotationShapeName", "Rectangle");
    }

    QString operator()(const core::PolygonGeometry& polygon) const
    {
        // One geometry, two shapes: the flag is the only difference, and the
        // user's word for them is not the same.
        return polygon.closed
                   ? QCoreApplication::translate("AnnotationShapeName", "Polygon")
                   : QCoreApplication::translate("AnnotationShapeName", "Freehand Line");
    }
};

} // namespace

QString shapeName(const core::Annotation& annotation)
{
    return std::visit(ShapeNameVisitor{}, annotation.geometry());
}

} // namespace slideio::viewer::ui
```

`QCoreApplication::translate` replaces `ViewportWidget::tr` because this is no longer a member of a `QObject` subclass. The context string changes from `"ViewportWidget"` to `"AnnotationShapeName"`; there are no `.ts` translation files in the repo yet, so nothing breaks.

- [ ] **Step 3: Delete the originals and include the new header**

In `src/ui/src/ViewportWidget.cpp`, delete the `ShapeNameVisitor` struct and the `shapeName` function from the anonymous namespace (around lines 100-123), and add to the project-header include block:

```cpp
#include "slideio/viewer/ui/AnnotationShapeName.h"
```

The three call sites (`:2826`, `:2841`, `:2912`) need no change — unqualified `shapeName` still resolves, now to `ui::shapeName`, because `ViewportWidget.cpp`'s code is inside `namespace slideio::viewer::ui`.

- [ ] **Step 4: Route `pushDeleteCommand` through the seam**

Replace the body of `ViewportWidget::pushDeleteCommand` (`src/ui/src/ViewportWidget.cpp:2831`):

```cpp
void ViewportWidget::pushDeleteCommand(const std::string& id)
{
    if (m_impl->undoStack == nullptr || id.empty()) {
        return;
    }
    const core::Annotation* stored = m_impl->annotations->find(id);
    if (stored == nullptr) {
        return;
    }
    const QString what = tr("Delete %1").arg(shapeName(*stored));
    app::deleteWithUndo(*m_impl->annotations, *m_impl->undoStack, id, what.toStdString());
}
```

This changes the shape of the call site. Previously `pushDeleteCommand(id)` recorded the deletion *while the annotation was still present* and the caller removed it on the next line; now `deleteWithUndo` does both.

There is exactly one caller, at `src/ui/src/ViewportWidget.cpp:4232-4233` (the Delete-key handler in `keyPressEvent`):

```cpp
        const std::string doomedId = m_impl->annotations->selectedId();
        pushDeleteCommand(doomedId);
        m_impl->annotations->remove(doomedId);   // <-- delete this line
```

Delete the `remove` line. Leaving it is not catastrophic — `remove()` on an id already gone returns false and does nothing, so the annotation is still deleted once and the undo step is still pushed — but it leaves a line that reads as if it were doing the work, which is how the next person to touch this handler gets it wrong. Confirm the count before and after:

```bash
grep -n "pushDeleteCommand\|annotations->remove(" src/ui/src/ViewportWidget.cpp
```

Add to the include block:

```cpp
#include "slideio/viewer/app/AnnotationEditing.h"
```

- [ ] **Step 5: Register the new source file**

In `src/ui/CMakeLists.txt`, add to the `add_library(slideio-viewer-ui STATIC ...)` list, after `src/AnnotationInteraction.cpp`:

```cmake
    src/AnnotationShapeName.cpp
```

- [ ] **Step 6: Build and run every suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass. There is no new automated test here — this task moves code and re-points one call. Its correctness is carried by the existing suites compiling and by group V check V10, which is the one that proves a deletion still produces exactly one correctly-labelled undo step.

- [ ] **Step 7: Manual smoke before committing**

Kill any running viewer, then:

```bash
cmake --install build/build --config Release
ls -l build/install/release/bin/slideio-viewer.exe build/build/Release/slideio-viewer.exe
```

Confirm the two timestamps match — a running viewer makes the install a silent no-op that still exits 0. Then open a slide with annotations, select one, press Delete, and check the Edit menu reads "Undo Delete Rectangle" (or Polygon / Freehand Line). Press Ctrl+Z and confirm it comes back.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationShapeName.h src/ui/src/AnnotationShapeName.cpp src/ui/src/ViewportWidget.cpp src/ui/CMakeLists.txt
git commit -m "Promote shapeName and route deletion through the app-layer seam"
```

---

### Task 4: Move `AnnotationModel` ownership to `MainWindow`

This is follow-up item 8. It is the riskiest task in the plan: it rewrites a constructor and a destructor, it has no automated test that can observe it, and a lifetime mistake here surfaces as a crash on exit rather than a failing build. Read the whole task before starting.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h:52` (constructor), `:133` (remove `annotationModel()`)
- Modify: `src/ui/src/ViewportWidget.cpp:1965-2000` (constructor)
- Modify: `src/ui/src/MainWindow.cpp:137-210` (Impl members), `:1506` (construction), `:1160`, `:1180`, `:1182`, `:1420`, `:1588` (call sites), `:1632-1648` (destructor)

**Interfaces:**
- Consumes: `app::AnnotationModel`.
- Produces: `ViewportWidget(app::AnnotationModel& model, QWidget* parent = nullptr)`. `ViewportWidget::annotationModel()` no longer exists — later tasks reach the model through `MainWindow::Impl::annotations`.

- [ ] **Step 1: Change the constructor signature**

In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, replace line 52:

```cpp
    /// `model` must outlive this widget. MainWindow owns it and deletes this
    /// widget explicitly before releasing it; see ~MainWindow.
    ///
    /// A reference rather than a setter, unlike setPersistenceService and
    /// setUndoStack: this widget works perfectly well without those two and
    /// null-guards both, but it cannot paint, hit-test or draw without a model.
    /// A null-tolerant model pointer would be a dozen early returns describing a
    /// state that must never occur.
    explicit ViewportWidget(app::AnnotationModel& model, QWidget* parent = nullptr);
```

- [ ] **Step 2: Remove the accessor**

Delete lines 131-133 of the same header — the `annotationModel()` declaration and its doc comment. Leaving it as a convenience is what let application state be owned by a presentation widget in the first place; a panel that can still reach the model through the viewport will.

- [ ] **Step 3: Change the member and the constructor**

In `src/ui/src/ViewportWidget.cpp`, find the `Impl` member declaration `std::unique_ptr<app::AnnotationModel> annotations;` and replace it with:

```cpp
    /// Non-owning. MainWindow owns the model and guarantees it outlives this
    /// widget by deleting the widget first.
    app::AnnotationModel* annotations = nullptr;
```

Then change the constructor (line 1965):

```cpp
ViewportWidget::ViewportWidget(app::AnnotationModel& model, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Prevent Qt from painting a background over the OpenGL content.
    // Without these, the parent's stylesheet background can cover the FBO.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    m_impl->annotations = &model;
```

Leave the six `connect` calls that follow exactly as they are, changing only `m_impl->annotations.get()` to `m_impl->annotations` in each. The comment above them, which records that these close follow-up item 7, stays.

- [ ] **Step 4: Delete the accessor definition**

Remove `ViewportWidget::annotationModel()` at `src/ui/src/ViewportWidget.cpp:2917`.

All 24 `m_impl->annotations->` use sites elsewhere in the file compile unchanged — `operator->` on a raw pointer reads identically to `operator->` on a `unique_ptr`.

- [ ] **Step 5: Give MainWindow the model**

In `src/ui/src/MainWindow.cpp`, add to the `Impl` member block (near the existing `std::unique_ptr<app::UndoRedoStack> undoStack;` at line 209):

```cpp
    /// Owned here, not by the viewport. The viewport renders it, the panels
    /// list and edit it, and persistence saves it -- four consumers across two
    /// layers, so the application layer's state belongs to the window that
    /// coordinates them rather than to one of the things that draws it.
    std::unique_ptr<app::AnnotationModel> annotations;
```

Then at line 1506, construct it first:

```cpp
    m_impl->annotations = std::make_unique<app::AnnotationModel>();
    m_impl->viewportWidget = new ViewportWidget(*m_impl->annotations, this);
```

- [ ] **Step 6: Update the five call sites**

Replace `viewportWidget->annotationModel()` with `annotations.get()` — or `*annotations` where a reference is wanted — at each of:

- `:1160` — `*viewportWidget->annotationModel()` becomes `*annotations`
- `:1180` and `:1182` — `viewportWidget->annotationModel()` becomes `annotations.get()`
- `:1420` and `:1588` — `viewportWidget->annotationModel()->setDefaultAuthor(...)` becomes `annotations->setDefaultAuthor(...)`

Note that `:1588` currently runs through `m_impl->` from `MainWindow`'s own body rather than from inside `Impl`; keep whichever qualification the surrounding line already uses. Verify none remain:

```bash
grep -n "annotationModel()" src/ui/src/MainWindow.cpp
```

Expected: no output.

- [ ] **Step 7: Fix the destructor**

Replace the first block of `MainWindow::~MainWindow` (`src/ui/src/MainWindow.cpp:1632-1642`):

```cpp
MainWindow::~MainWindow()
{
    // m_impl dies before ~QMainWindow deletes this window's children, so
    // anything Qt-owned that holds the model, the persistence service or the
    // undo stack would outlive all three. The viewport holds the model by
    // reference and cannot be cut loose from it, so it is deleted here instead,
    // ahead of m_impl -- rather than relying on ~ViewportWidget never touching
    // what it was given, which is a fact about another file that nobody
    // changing it will think to come back and check.
    delete m_impl->viewportWidget;
    m_impl->viewportWidget = nullptr;
```

The panel deletions join this block in Tasks 7 and 9; the `QSettings` geometry save below it stays where it is.

- [ ] **Step 8: Build and run every suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass. Nothing here is automatically testable — the verification is that the whole ui layer still compiles against the new constructor, plus the manual check in the next step.

- [ ] **Step 9: Manual lifetime check**

This is the step that matters. Kill the viewer, install, verify by timestamp as in Task 3, then:

1. Open a slide that has saved annotations. Confirm they appear.
2. Draw one, wait three seconds for the autosave debounce, switch to another slide and back. Confirm it is still there.
3. Change the colour profile on a slide with annotations (View → per-slide profile). Confirm the annotations survive and Ctrl+Z still undoes the last edit — this is the `isPreservingUndoContext()` window, and the ownership move must not have disturbed it.
4. Draw an annotation and immediately close the window, inside the 2-second autosave debounce. Confirm the application exits without a crash dialog and the annotation is in the file afterwards. This is the case the destructor change is about.

- [ ] **Step 10: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp src/ui/src/MainWindow.cpp
git commit -m "Move AnnotationModel ownership from the viewport to MainWindow"
```

---

### Task 5: `showAnnotation` on the viewport

Wires Task 1's arithmetic up through the controller to a call the list panel's double-click can reach.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportController.h:38` (after `fitToSlide`)
- Modify: `src/ui/src/ViewportController.cpp:77-81` (after `fitToSlide`)
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h:91` (after `fitToSlide`)
- Modify: `src/ui/src/ViewportWidget.cpp:2679-2689` (after `fitToSlide`)

**Interfaces:**
- Consumes: `core::Viewport::fitToRect` (Task 1), `app::AnnotationModel::find`, `core::Annotation::boundingBox()`.
- Produces: `void ViewportController::showRect(const core::RectF& target, double marginFraction)` and `void ViewportWidget::showAnnotation(const std::string& id)`.

- [ ] **Step 1: Add the controller method**

In `src/ui/include/slideio/viewer/ui/ViewportController.h`, after `void fitToSlide();`:

```cpp
    /// Centres the view on `target` and zooms so it fits with `marginFraction`
    /// of the screen free on each side.
    void showRect(const core::RectF& target, double marginFraction);
```

In `src/ui/src/ViewportController.cpp`, after `fitToSlide`:

```cpp
void ViewportController::showRect(const core::RectF& target, double marginFraction)
{
    m_viewport.fitToRect(target, marginFraction);
    requestVisibleTiles();
}
```

- [ ] **Step 2: Add the widget method**

In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, after `void fitToSlide();`:

```cpp
    /// Brings one annotation into view, centred, with a margin around it. A
    /// no-op when `id` names nothing the model holds -- a list row can outlive
    /// its annotation by one event-loop turn when an undo removes it while the
    /// double-click is already queued.
    void showAnnotation(const std::string& id);
```

In `src/ui/src/ViewportWidget.cpp`, after `fitToSlide`:

```cpp
void ViewportWidget::showAnnotation(const std::string& id)
{
    if (m_impl->controller == nullptr) {
        return;
    }
    const core::Annotation* annotation = m_impl->annotations->find(id);
    if (annotation == nullptr) {
        return;
    }

    if (m_impl->glInitialized && m_impl->slideOpen) {
        m_impl->activateSnapshot();
    }
    // 0.15 leaves enough slide around the shape to see what it sits in. A
    // pathologist navigating to a mark is looking at its surroundings, not
    // only at the outline.
    m_impl->controller->showRect(annotation->boundingBox(), 0.15);
    emit viewportChanged();
    update();
}
```

The `activateSnapshot` / `emit viewportChanged` / `update` sequence is copied from `fitToSlide` deliberately — every navigation method in this file does exactly this, and a new one that skips a step produces a view that is correct but does not repaint until the next pan.

- [ ] **Step 3: Build and run every suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass. The arithmetic is already covered by Task 1; this task is wiring, verified by group V check V9.

- [ ] **Step 4: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportController.h src/ui/src/ViewportController.cpp src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp
git commit -m "Add ViewportWidget::showAnnotation to navigate to one mark"
```

---

### Task 6: `AnnotationListPolicy` — the list's testable core

Everything the list panel decides — which rows to show, in what order, labelled how — lives here, in functions that take plain data and return plain data, because the panel itself can never be constructed in a test.

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h`
- Create: `src/ui/src/AnnotationListPolicy.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Create: `tests/ui/AnnotationListPolicyTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::Annotation` (`id`, `type`, `properties`, `metadata`), `core::AnnotationType`.
- Produces: `ui::AnnotationSortKey`, `ui::AnnotationRow`, `ui::AnnotationTypeNames`, `ui::annotationRows(...)`, `ui::annotationDockTitle(total, shown)`.

- [ ] **Step 1: Write the failing tests**

Create `tests/ui/AnnotationListPolicyTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

/// Plain ASCII names, so these tests need no QApplication and no translation.
std::string testTypeName(core::AnnotationType type)
{
    switch (type) {
    case core::AnnotationType::Rectangle:
        return "Rectangle";
    case core::AnnotationType::Polygon:
        return "Polygon";
    case core::AnnotationType::Polyline:
        return "Freehand Line";
    }
    return "Shape";
}

core::AnnotationGeometry rect()
{
    // RectangleGeometry stores two corners, not an origin-plus-size rect.
    return core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{10.0, 10.0}};
}

core::Annotation annotation(std::string id, std::string label, std::string classification,
                            int minutesOld = 0)
{
    core::AnnotationProperties properties;
    properties.label = std::move(label);
    properties.classification = std::move(classification);

    core::AnnotationMetadata metadata;
    metadata.author = "tester";
    metadata.createdAt = std::chrono::system_clock::time_point{} + std::chrono::minutes(minutesOld);
    metadata.modifiedAt = metadata.createdAt;

    return core::Annotation{std::move(id), rect(), std::move(properties), metadata};
}

} // namespace

TEST_CASE("an empty model gives an empty list", "[ui][AnnotationList]")
{
    // Review Focus 5. A slide with no annotations is the ordinary state of
    // every slide before anyone marks it, not an error.
    const auto rows = ui::annotationRows({}, "", ui::AnnotationSortKey::CreationOrder,
                                         testTypeName);
    REQUIRE(rows.empty());
    REQUIRE(ui::annotationDockTitle(0, 0) == "Annotations (0)");
}

TEST_CASE("the dock title counts, and says so when filtered", "[ui][AnnotationList]")
{
    REQUIRE(ui::annotationDockTitle(12, 12) == "Annotations (12)");
    REQUIRE(ui::annotationDockTitle(12, 3) == "Annotations (3 of 12)");
}

TEST_CASE("an unlabeled annotation is named by type and creation number",
          "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "", ""));
    annotations.push_back(annotation("b", "Tumour front", ""));
    annotations.push_back(annotation("c", "", ""));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::CreationOrder,
                                         testTypeName);

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].label == "Rectangle 1");
    REQUIRE(rows[1].label == "Tumour front");
    REQUIRE(rows[2].label == "Rectangle 3");
}

TEST_CASE("the creation number survives a filter", "[ui][AnnotationList]")
{
    // Review Focus 3. The number counts across every annotation the slide
    // holds, not across the rows that survive the filter -- otherwise typing in
    // the search box renames the shapes, and the one the user is looking at
    // changes its name as they type.
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "", "Tumor"));
    annotations.push_back(annotation("b", "", "Stroma"));
    annotations.push_back(annotation("c", "", "Tumor"));

    const auto rows = ui::annotationRows(annotations, "stroma",
                                         ui::AnnotationSortKey::CreationOrder, testTypeName);

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == "b");
    REQUIRE(rows[0].label == "Rectangle 2");
}

TEST_CASE("the creation number survives a sort", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "", "Zeta"));
    annotations.push_back(annotation("b", "", "Alpha"));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Classification,
                                         testTypeName);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].id == "b");
    REQUIRE(rows[0].label == "Rectangle 2");
    REQUIRE(rows[1].id == "a");
    REQUIRE(rows[1].label == "Rectangle 1");
}

TEST_CASE("filtering is case-insensitive over label and classification",
          "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Tumour Front", ""));
    annotations.push_back(annotation("b", "", "NECROSIS"));
    annotations.push_back(annotation("c", "Nothing", "Normal"));

    REQUIRE(ui::annotationRows(annotations, "tumour", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).size() == 1);
    REQUIRE(ui::annotationRows(annotations, "necro", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).size() == 1);
    REQUIRE(ui::annotationRows(annotations, "zzz", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).empty());
}

TEST_CASE("notes are not searched", "[ui][AnnotationList]")
{
    // A row matching on notes would show nothing resembling the search text in
    // any of its three visible columns, and read as a bug.
    core::AnnotationProperties properties;
    properties.notes = "mitotic figures throughout";
    core::AnnotationMetadata metadata;
    std::vector<core::Annotation> annotations;
    annotations.push_back(core::Annotation{"a", rect(), properties, metadata});

    REQUIRE(ui::annotationRows(annotations, "mitotic", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).empty());
}

TEST_CASE("sorts break ties by creation order", "[ui][AnnotationList]")
{
    // Two rows with the same sort key must not swap between rebuilds, and the
    // list rebuilds on every model change.
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Same", ""));
    annotations.push_back(annotation("b", "Same", ""));
    annotations.push_back(annotation("c", "Same", ""));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Label,
                                         testTypeName);
    REQUIRE(rows[0].id == "a");
    REQUIRE(rows[1].id == "b");
    REQUIRE(rows[2].id == "c");
}

TEST_CASE("sorting by modified time puts the most recent first", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("old", "", "", 0));
    annotations.push_back(annotation("new", "", "", 10));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::ModifiedTime,
                                         testTypeName);
    REQUIRE(rows[0].id == "new");
    REQUIRE(rows[1].id == "old");
}

TEST_CASE("the type column carries the supplied name", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Labelled", "Tumor"));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::CreationOrder,
                                         testTypeName);
    REQUIRE(rows[0].type == "Rectangle");
    REQUIRE(rows[0].classification == "Tumor");
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `tests/CMakeLists.txt`, add to the `slideio-viewer-ui-tests` source list:

```cmake
    ui/AnnotationListPolicyTest.cpp
```

```bash
cmake --build build/build --config Release --target slideio-viewer-ui-tests
```

Expected: `Cannot open include file: 'slideio/viewer/ui/AnnotationListPolicy.h'`.

- [ ] **Step 3: Write the header**

Create `src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// What the list's sort combo offers.
enum class AnnotationSortKey
{
    CreationOrder,
    Label,
    Classification,
    Type,
    ModifiedTime,
};

/// One row of the annotation list, already formatted. The panel copies these
/// straight into its tree; it makes no further decisions about text or order.
struct AnnotationRow
{
    std::string id;
    std::string label;
    std::string type;
    std::string classification;
};

/// The user's word for a geometry type. Passed in rather than looked up here
/// because the real one (ui::shapeName) returns a translated QString, and this
/// function has to stay free of Qt to be worth testing -- no test binary
/// creates a QApplication, so anything needing one is untestable by
/// construction.
using AnnotationTypeNames = std::function<std::string(core::AnnotationType)>;

/// The rows to display, filtered and sorted.
///
/// `filterText` is matched case-insensitively as a substring of the label and
/// the classification, and of nothing else: a row matching on its notes would
/// show nothing resembling the search text in any visible column.
///
/// An annotation with no label displays as "<Type> N", where N is its 1-based
/// position in `annotations` -- that is, in creation order, across every
/// annotation on the slide. Not across the filtered rows and not across the
/// sorted ones: a shape that renames itself when the user types in the search
/// box or changes the sort is a shape they cannot keep track of.
///
/// Every sort breaks ties by creation order, so two rows with equal keys never
/// swap between rebuilds -- and the list rebuilds on every model change.
[[nodiscard]] std::vector<AnnotationRow> annotationRows(
    const std::vector<core::Annotation>& annotations, const std::string& filterText,
    AnnotationSortKey sort, const AnnotationTypeNames& typeName);

/// "Annotations (12)", or "Annotations (3 of 12)" when a filter is hiding some
/// -- so a filtered list is never mistaken for an empty slide.
[[nodiscard]] std::string annotationDockTitle(std::size_t total, std::size_t shown);

} // namespace slideio::viewer::ui
```

- [ ] **Step 4: Write the implementation**

Create `src/ui/src/AnnotationListPolicy.cpp`:

```cpp
#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <algorithm>
#include <cctype>

namespace slideio::viewer::ui
{

namespace
{

std::string toLower(const std::string& text)
{
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

bool containsFold(const std::string& haystack, const std::string& needleLower)
{
    return toLower(haystack).find(needleLower) != std::string::npos;
}

/// Everything a row needs plus the creation index the display label is built
/// from. Kept beside the row through the sort so the number cannot drift.
struct IndexedRow
{
    AnnotationRow row;
    std::size_t creationIndex;
    std::chrono::system_clock::time_point modifiedAt;
};

} // namespace

std::vector<AnnotationRow> annotationRows(const std::vector<core::Annotation>& annotations,
                                          const std::string& filterText, AnnotationSortKey sort,
                                          const AnnotationTypeNames& typeName)
{
    const std::string needle = toLower(filterText);

    std::vector<IndexedRow> indexed;
    indexed.reserve(annotations.size());

    for (std::size_t i = 0; i < annotations.size(); ++i) {
        const core::Annotation& annotation = annotations[i];
        const core::AnnotationProperties& properties = annotation.properties();

        if (!needle.empty() && !containsFold(properties.label, needle)
            && !containsFold(properties.classification, needle)) {
            continue;
        }

        const std::string type = typeName ? typeName(annotation.type()) : std::string{"Shape"};

        AnnotationRow row;
        row.id = annotation.id();
        // The number is the position on the slide, not in this list: it is
        // computed before any filtering or sorting touches the order.
        row.label = properties.label.empty() ? type + " " + std::to_string(i + 1)
                                             : properties.label;
        row.type = type;
        row.classification = properties.classification;

        indexed.push_back(IndexedRow{std::move(row), i, annotation.metadata().modifiedAt});
    }

    const auto byCreation = [](const IndexedRow& a, const IndexedRow& b) {
        return a.creationIndex < b.creationIndex;
    };

    switch (sort) {
    case AnnotationSortKey::CreationOrder:
        std::sort(indexed.begin(), indexed.end(), byCreation);
        break;
    case AnnotationSortKey::Label:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.row.label != b.row.label) {
                return a.row.label < b.row.label;
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::Classification:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.row.classification != b.row.classification) {
                return a.row.classification < b.row.classification;
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::Type:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.row.type != b.row.type) {
                return a.row.type < b.row.type;
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::ModifiedTime:
        // Most recent first: the mark someone just touched is the one they are
        // looking for.
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.modifiedAt != b.modifiedAt) {
                return a.modifiedAt > b.modifiedAt;
            }
            return byCreation(a, b);
        });
        break;
    }

    std::vector<AnnotationRow> rows;
    rows.reserve(indexed.size());
    for (IndexedRow& entry : indexed) {
        rows.push_back(std::move(entry.row));
    }
    return rows;
}

std::string annotationDockTitle(std::size_t total, std::size_t shown)
{
    if (shown == total) {
        return "Annotations (" + std::to_string(total) + ")";
    }
    return "Annotations (" + std::to_string(shown) + " of " + std::to_string(total) + ")";
}

} // namespace slideio::viewer::ui
```

Add `#include <chrono>` to the header if `IndexedRow`'s `time_point` member does not compile — `core/Annotation.h` includes `<chrono>`, so it should already be available transitively, but relying on that is how transitive-include breakages start.

- [ ] **Step 5: Register the source file**

In `src/ui/CMakeLists.txt`, add after `src/AnnotationShapeName.cpp`:

```cmake
    src/AnnotationListPolicy.cpp
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-ui-tests
ctest --test-dir build/build -C Release -R ui-tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h src/ui/src/AnnotationListPolicy.cpp src/ui/CMakeLists.txt tests/ui/AnnotationListPolicyTest.cpp tests/CMakeLists.txt
git commit -m "Add AnnotationListPolicy, the annotation list's testable core"
```

---

### Task 7: `AnnotationListPanel` and its wiring

The dock itself. No test can construct it, so it must contain as little decision-making as possible — everything it decides, Task 6 already decided.

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationListPanel.h`
- Create: `src/ui/src/AnnotationListPanel.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Modify: `src/ui/src/MainWindow.cpp:137-210` (Impl members), `:402-429` (View menu), `:1520-1580` (dock creation), `:1632` (destructor)

**Interfaces:**
- Consumes: `ui::annotationRows`, `ui::annotationDockTitle`, `ui::AnnotationSortKey`, `ui::AnnotationRow` (Task 6); `ui::shapeName` (Task 3); `app::AnnotationModel`.
- Produces: `AnnotationListPanel::setAnnotationModel(app::AnnotationModel*)`, signals `annotationActivated(const std::string&)` and `deleteRequested(const std::string&)`.

- [ ] **Step 1: Write the header**

Create `src/ui/include/slideio/viewer/ui/AnnotationListPanel.h`:

```cpp
#pragma once

#include <QDockWidget>

#include <memory>
#include <string>

namespace slideio::viewer::app
{
class AnnotationModel;
}

namespace slideio::viewer::ui
{

/// Docking list of the open scene's annotations, with a filter and a sort.
///
/// Holds no state of its own beyond the filter text and the sort key: the rows
/// are rebuilt from the model on every change, and what they say is decided by
/// annotationRows() rather than here. That is deliberate -- no test binary
/// creates a QApplication, so nothing inside this class can be asserted on, and
/// the way to keep it correct is to give it as little to decide as possible.
class AnnotationListPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnnotationListPanel(QWidget* parent = nullptr);
    ~AnnotationListPanel() override;

    AnnotationListPanel(const AnnotationListPanel&) = delete;
    AnnotationListPanel& operator=(const AnnotationListPanel&) = delete;

    /// Non-owning, and null is a legitimate state: a panel with no model shows
    /// nothing. MainWindow owns the model and sets this once, after
    /// construction -- which is the window this null tolerance covers, since a
    /// restored dock layout can make this panel visible before the call.
    void setAnnotationModel(app::AnnotationModel* model);

signals:
    /// A row was double-clicked. MainWindow routes this to the viewport; this
    /// panel never touches the viewport itself, which is the whole point of
    /// moving the model out of it.
    void annotationActivated(const std::string& id);

    /// Delete was pressed on a row. MainWindow routes this through
    /// app::deleteWithUndo so the deletion is undoable -- removing it here
    /// would produce an edit Ctrl+Z cannot reach.
    void deleteRequested(const std::string& id);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// Rebuilds every row from the model. Called on every model signal except
    /// selectionChanged.
    void rebuild();
    /// Moves the highlight to `id` without emitting, so it is not mistaken for
    /// the user clicking a row.
    void applySelection(const std::string& id);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
```

`QKeyEvent` needs a forward declaration above the namespace block:

```cpp
class QKeyEvent;
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/src/AnnotationListPanel.cpp`:

```cpp
#include "slideio/viewer/ui/AnnotationListPanel.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/ui/AnnotationListPolicy.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QComboBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kIdRole = Qt::UserRole;
}

struct AnnotationListPanel::Impl
{
    app::AnnotationModel* model = nullptr;
    QLineEdit* filter = nullptr;
    QComboBox* sort = nullptr;
    QTreeWidget* tree = nullptr;
};

AnnotationListPanel::AnnotationListPanel(QWidget* parent)
    : QDockWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle(QString::fromStdString(annotationDockTitle(0, 0)));

    auto* contents = new QWidget(this);
    auto* layout = new QVBoxLayout(contents);

    m_impl->filter = new QLineEdit(contents);
    m_impl->filter->setPlaceholderText(tr("Filter by label or classification"));
    m_impl->filter->setClearButtonEnabled(true);
    layout->addWidget(m_impl->filter);

    m_impl->sort = new QComboBox(contents);
    m_impl->sort->addItem(tr("Creation order"), static_cast<int>(AnnotationSortKey::CreationOrder));
    m_impl->sort->addItem(tr("Label"), static_cast<int>(AnnotationSortKey::Label));
    m_impl->sort->addItem(tr("Classification"), static_cast<int>(AnnotationSortKey::Classification));
    m_impl->sort->addItem(tr("Type"), static_cast<int>(AnnotationSortKey::Type));
    m_impl->sort->addItem(tr("Last modified"), static_cast<int>(AnnotationSortKey::ModifiedTime));
    layout->addWidget(m_impl->sort);

    m_impl->tree = new QTreeWidget(contents);
    m_impl->tree->setColumnCount(3);
    m_impl->tree->setHeaderLabels({tr("Label"), tr("Type"), tr("Classification")});
    m_impl->tree->setRootIsDecorated(false);
    m_impl->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->tree->header()->setStretchLastSection(true);
    layout->addWidget(m_impl->tree);

    setWidget(contents);

    connect(m_impl->filter, &QLineEdit::textChanged, this, [this](const QString&) { rebuild(); });
    connect(m_impl->sort, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { rebuild(); });

    connect(m_impl->tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_impl->model == nullptr) {
            return;
        }
        const QList<QTreeWidgetItem*> selected = m_impl->tree->selectedItems();
        m_impl->model->setSelected(selected.isEmpty()
                                       ? std::string{}
                                       : selected.first()->data(0, kIdRole).toString().toStdString());
    });

    connect(m_impl->tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item != nullptr) {
                    emit annotationActivated(item->data(0, kIdRole).toString().toStdString());
                }
            });
}

AnnotationListPanel::~AnnotationListPanel() = default;

void AnnotationListPanel::setAnnotationModel(app::AnnotationModel* model)
{
    if (m_impl->model == model) {
        return;
    }
    if (m_impl->model != nullptr) {
        disconnect(m_impl->model, nullptr, this, nullptr);
    }
    m_impl->model = model;

    if (m_impl->model != nullptr) {
        // Every signal that changes what the rows should say rebuilds them.
        // selectionChanged deliberately does not: it moves a highlight, and
        // rebuilding would destroy the item under the user's cursor mid-click.
        const auto rebuildRows = [this]() { rebuild(); };
        connect(m_impl->model, &app::AnnotationModel::annotationAdded, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::annotationRemoved, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::annotationChanged, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::cleared, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::modelReset, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::selectionChanged, this,
                [this](const std::string& id) { applySelection(id); });
    }
    rebuild();
}

void AnnotationListPanel::rebuild()
{
    const QSignalBlocker blocker(m_impl->tree);
    m_impl->tree->clear();

    if (m_impl->model == nullptr) {
        setWindowTitle(QString::fromStdString(annotationDockTitle(0, 0)));
        return;
    }

    const auto sortKey = static_cast<AnnotationSortKey>(m_impl->sort->currentData().toInt());
    const auto typeName = [](core::AnnotationType type) {
        // shapeName takes an annotation because the polygon/polyline split
        // lives in the geometry; a bare type still maps one-to-one onto the
        // three names, so a representative is enough here.
        switch (type) {
        case core::AnnotationType::Rectangle:
            return tr("Rectangle").toStdString();
        case core::AnnotationType::Polygon:
            return tr("Polygon").toStdString();
        case core::AnnotationType::Polyline:
            return tr("Freehand Line").toStdString();
        }
        return tr("Shape").toStdString();
    };

    const std::vector<core::Annotation>& annotations = m_impl->model->annotations();
    const auto rows = annotationRows(annotations, m_impl->filter->text().toStdString(), sortKey,
                                     typeName);

    for (const AnnotationRow& row : rows) {
        auto* item = new QTreeWidgetItem(m_impl->tree);
        item->setText(0, QString::fromStdString(row.label));
        item->setText(1, QString::fromStdString(row.type));
        item->setText(2, QString::fromStdString(row.classification));
        item->setData(0, kIdRole, QString::fromStdString(row.id));
    }

    setWindowTitle(QString::fromStdString(annotationDockTitle(annotations.size(), rows.size())));
    applySelection(m_impl->model->selectedId());
}

void AnnotationListPanel::applySelection(const std::string& id)
{
    // Blocked so highlighting the model's selection does not look like the user
    // clicking a row, which would call setSelected again. setSelected returns
    // early on an unchanged id so the loop would terminate anyway, but relying
    // on that makes this panel's correctness a fact about another class.
    const QSignalBlocker blocker(m_impl->tree);
    m_impl->tree->clearSelection();
    if (id.empty()) {
        return;
    }
    const QString target = QString::fromStdString(id);
    for (int i = 0; i < m_impl->tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_impl->tree->topLevelItem(i);
        if (item->data(0, kIdRole).toString() == target) {
            item->setSelected(true);
            m_impl->tree->scrollToItem(item);
            return;
        }
    }
}

void AnnotationListPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete && m_impl->tree->hasFocus()) {
        const QList<QTreeWidgetItem*> selected = m_impl->tree->selectedItems();
        if (!selected.isEmpty()) {
            emit deleteRequested(selected.first()->data(0, kIdRole).toString().toStdString());
            event->accept();
            return;
        }
    }
    QDockWidget::keyPressEvent(event);
}

} // namespace slideio::viewer::ui
```

`rebuild`, `applySelection` and `keyPressEvent` are already declared in the header from Step 1 — no further header edits are needed.

- [ ] **Step 3: Register the source file**

In `src/ui/CMakeLists.txt`, after `src/AnnotationListPolicy.cpp`:

```cmake
    src/AnnotationListPanel.cpp
```

The header needs no entry: `file(GLOB_RECURSE _ui_headers ...)` already feeds every header under `include/` to AUTOMOC, which is what a `Q_OBJECT` class needs.

- [ ] **Step 4: Wire it into MainWindow**

In `src/ui/src/MainWindow.cpp`:

Add to the `Impl` member block near `metadataPanel`:

```cpp
    AnnotationListPanel* annotationListPanel = nullptr;
    QAction* annotationListToggleAction = nullptr;
```

Add the include beside the other panel includes:

```cpp
#include "slideio/viewer/ui/AnnotationListPanel.h"
#include "slideio/viewer/app/AnnotationEditing.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"
```

Create the dock after the metadata panel (around line 1556):

```cpp
    // Annotation list dock (hidden by default)
    m_impl->annotationListPanel = new AnnotationListPanel(this);
    m_impl->annotationListPanel->setObjectName(QStringLiteral("AnnotationListPanel"));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->annotationListPanel);
    m_impl->annotationListPanel->hide();
```

Add the toggle action beside the others (around line 1578):

```cpp
    m_impl->annotationListToggleAction = m_impl->annotationListPanel->toggleViewAction();
    m_impl->annotationListToggleAction->setText("Annotation &List");
    m_impl->annotationListToggleAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    m_impl->annotationListToggleAction->setStatusTip("Toggle the annotation list panel");
```

Add it to the View menu after `metadataToggleAction` (line 420):

```cpp
        viewMenu->addAction(annotationListToggleAction);
```

Bind the model and route the two signals, immediately after `m_impl->buildAnnotationPersistence();` (around line 1590):

```cpp
    m_impl->annotationListPanel->setAnnotationModel(m_impl->annotations.get());

    connect(m_impl->annotationListPanel, &AnnotationListPanel::annotationActivated, this,
            [this](const std::string& id) { m_impl->viewportWidget->showAnnotation(id); });

    connect(m_impl->annotationListPanel, &AnnotationListPanel::deleteRequested, this,
            [this](const std::string& id) {
                const core::Annotation* annotation = m_impl->annotations->find(id);
                if (annotation == nullptr || m_impl->undoStack == nullptr) {
                    return;
                }
                const QString what = tr("Delete %1").arg(shapeName(*annotation));
                app::deleteWithUndo(*m_impl->annotations, *m_impl->undoStack, id,
                                    what.toStdString());
            });
```

Add the panel deletion to `~MainWindow`, in the block from Task 4:

```cpp
    delete m_impl->annotationListPanel;
    m_impl->annotationListPanel = nullptr;
```

placed before the `delete m_impl->viewportWidget;` line.

- [ ] **Step 5: Build and run every suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass.

- [ ] **Step 6: Manual check**

Kill the viewer, install, verify by timestamp. Then run group V checks V1-V10 from the spec. Note any failures before committing.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationListPanel.h src/ui/src/AnnotationListPanel.cpp src/ui/src/MainWindow.cpp src/ui/CMakeLists.txt
git commit -m "Add the annotation list panel"
```

---

### Task 8: `AnnotationPropertiesEdit` — the editor's testable core

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h`
- Create: `src/ui/src/AnnotationPropertiesEdit.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Create: `tests/ui/AnnotationPropertiesEditTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::AnnotationProperties`, `core::Color`.
- Produces: `ui::AnnotationFieldValues`, `ui::propertiesEdit(...)`, `ui::propertiesEditDescription(...)`, `ui::kAnnotationClassifications`.

- [ ] **Step 1: Write the failing tests**

Create `tests/ui/AnnotationPropertiesEditTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationPropertiesEdit.h"

#include <string>

using namespace slideio::viewer;

namespace
{

ui::AnnotationFieldValues fieldsFrom(const core::AnnotationProperties& properties)
{
    ui::AnnotationFieldValues fields;
    fields.label = properties.label;
    fields.classification = properties.classification;
    fields.notes = properties.notes;
    fields.color = properties.color;
    fields.lineWidth = properties.lineWidth;
    fields.fillOpacity = properties.fillOpacity;
    return fields;
}

} // namespace

TEST_CASE("an unchanged field set is not an edit", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    REQUIRE_FALSE(ui::propertiesEdit(current, fieldsFrom(current)).has_value());
}

TEST_CASE("a changed label is an edit", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    ui::AnnotationFieldValues fields = fieldsFrom(current);
    fields.label = "Tumour front";

    const auto edited = ui::propertiesEdit(current, fields);
    REQUIRE(edited.has_value());
    REQUIRE(edited->label == "Tumour front");
}

TEST_CASE("line width and fill opacity are clamped", "[ui][AnnotationProperties]")
{
    // Review Focus 4. These values do not only come from the spin box and the
    // slider -- they come from the model, which was loaded from a file a person
    // can edit by hand. A lineWidth of 0 draws nothing and a fillOpacity of 4
    // is not a colour the renderer can produce.
    core::AnnotationProperties current;
    current.lineWidth = 0.0f;
    current.fillOpacity = 4.0f;

    const auto edited = ui::propertiesEdit(current, fieldsFrom(current));
    REQUIRE(edited.has_value());
    REQUIRE(edited->lineWidth == 1.0f);
    REQUIRE(edited->fillOpacity == 1.0f);

    core::AnnotationProperties negative;
    negative.lineWidth = 99.0f;
    negative.fillOpacity = -2.0f;

    const auto fixed = ui::propertiesEdit(negative, fieldsFrom(negative));
    REQUIRE(fixed.has_value());
    REQUIRE(fixed->lineWidth == 5.0f);
    REQUIRE(fixed->fillOpacity == 0.0f);
}

TEST_CASE("a free-text classification is kept", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    ui::AnnotationFieldValues fields = fieldsFrom(current);
    fields.classification = "Lymphovascular invasion";

    const auto edited = ui::propertiesEdit(current, fields);
    REQUIRE(edited.has_value());
    REQUIRE(edited->classification == "Lymphovascular invasion");
}

TEST_CASE("the five offered classifications are the agreed set", "[ui][AnnotationProperties]")
{
    REQUIRE(ui::kAnnotationClassifications.size() == 5);
    REQUIRE(ui::kAnnotationClassifications[0] == std::string("Tumor"));
    REQUIRE(ui::kAnnotationClassifications[1] == std::string("Stroma"));
    REQUIRE(ui::kAnnotationClassifications[2] == std::string("Necrosis"));
    REQUIRE(ui::kAnnotationClassifications[3] == std::string("Normal"));
    REQUIRE(ui::kAnnotationClassifications[4] == std::string("Artifact"));
}

TEST_CASE("the edit description names the field that changed",
          "[ui][AnnotationProperties]")
{
    // The Edit menu renders undoDescription(). Without this every properties
    // edit reads identically and two of them cannot be told apart on the stack.
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.label = "Renamed";
    REQUIRE(ui::propertiesEditDescription(before, after) == "Rename annotation");

    after = before;
    after.classification = "Tumor";
    REQUIRE(ui::propertiesEditDescription(before, after) == "Reclassify annotation");

    after = before;
    after.color = core::Color{1, 2, 3, 255};
    REQUIRE(ui::propertiesEditDescription(before, after) == "Recolour annotation");

    after = before;
    after.notes = "Mitoses";
    REQUIRE(ui::propertiesEditDescription(before, after) == "Edit annotation notes");

    after = before;
    after.lineWidth = 4.0f;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Restyle annotation");
}

TEST_CASE("a multi-field change gets the generic description",
          "[ui][AnnotationProperties]")
{
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.label = "Renamed";
    after.classification = "Tumor";

    REQUIRE(ui::propertiesEditDescription(before, after) == "Edit annotation");
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `tests/CMakeLists.txt`, add to `slideio-viewer-ui-tests`:

```cmake
    ui/AnnotationPropertiesEditTest.cpp
```

```bash
cmake --build build/build --config Release --target slideio-viewer-ui-tests
```

Expected: `Cannot open include file: 'slideio/viewer/ui/AnnotationPropertiesEdit.h'`.

- [ ] **Step 3: Write the header**

Create `src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <array>
#include <optional>
#include <string>

namespace slideio::viewer::ui
{

/// Offered in the classification combo, which stays editable: these are
/// suggestions, not a controlled vocabulary. classification remains a plain
/// string in the stored file, so the serializer and schemaVersion are untouched
/// by what is listed here.
inline constexpr std::array<const char*, 5> kAnnotationClassifications{
    "Tumor", "Stroma", "Necrosis", "Normal", "Artifact"};

/// What the properties panel's widgets currently hold.
struct AnnotationFieldValues
{
    std::string label;
    std::string classification;
    std::string notes;
    core::Color color;
    float lineWidth = 2.0f;
    float fillOpacity = 0.3f;
};

/// The properties to store, or nullopt when `fields` would change nothing.
///
/// Returning nullopt for a no-op is what keeps tabbing through an untouched
/// field from pushing an undo step, restamping modifiedAt and triggering an
/// autosave that writes a modification nobody made.
///
/// lineWidth is clamped to 1-5 (FR-ANN-11) and fillOpacity to 0-1. The clamp
/// applies to values arriving from the model as well as from the widgets: the
/// model was loaded from a file a person can edit by hand, and a lineWidth of 0
/// draws nothing at all. A clamp counts as a change, so an out-of-range value
/// is corrected and persisted once rather than re-clamped on every load.
[[nodiscard]] std::optional<core::AnnotationProperties> propertiesEdit(
    const core::AnnotationProperties& current, const AnnotationFieldValues& fields);

/// What the Edit menu should call this change: "Rename annotation",
/// "Reclassify annotation", "Recolour annotation", "Edit annotation notes",
/// "Restyle annotation", or "Edit annotation" when more than one field moved.
[[nodiscard]] std::string propertiesEditDescription(const core::AnnotationProperties& before,
                                                    const core::AnnotationProperties& after);

} // namespace slideio::viewer::ui
```

- [ ] **Step 4: Write the implementation**

Create `src/ui/src/AnnotationPropertiesEdit.cpp`:

```cpp
#include "slideio/viewer/ui/AnnotationPropertiesEdit.h"

#include <algorithm>

namespace slideio::viewer::ui
{

namespace
{

bool sameColor(const core::Color& a, const core::Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

std::optional<core::AnnotationProperties> propertiesEdit(
    const core::AnnotationProperties& current, const AnnotationFieldValues& fields)
{
    core::AnnotationProperties edited;
    edited.label = fields.label;
    edited.classification = fields.classification;
    edited.notes = fields.notes;
    edited.color = fields.color;
    edited.lineWidth = std::max(1.0f, std::min(fields.lineWidth, 5.0f));
    edited.fillOpacity = std::max(0.0f, std::min(fields.fillOpacity, 1.0f));

    const bool unchanged = edited.label == current.label
                           && edited.classification == current.classification
                           && edited.notes == current.notes
                           && sameColor(edited.color, current.color)
                           && edited.lineWidth == current.lineWidth
                           && edited.fillOpacity == current.fillOpacity;
    if (unchanged) {
        return std::nullopt;
    }
    return edited;
}

std::string propertiesEditDescription(const core::AnnotationProperties& before,
                                      const core::AnnotationProperties& after)
{
    const bool label = before.label != after.label;
    const bool classification = before.classification != after.classification;
    const bool color = !sameColor(before.color, after.color);
    const bool notes = before.notes != after.notes;
    const bool style = before.lineWidth != after.lineWidth
                       || before.fillOpacity != after.fillOpacity;

    const int changed = static_cast<int>(label) + static_cast<int>(classification)
                        + static_cast<int>(color) + static_cast<int>(notes)
                        + static_cast<int>(style);
    if (changed != 1) {
        return "Edit annotation";
    }
    if (label) {
        return "Rename annotation";
    }
    if (classification) {
        return "Reclassify annotation";
    }
    if (color) {
        return "Recolour annotation";
    }
    if (notes) {
        return "Edit annotation notes";
    }
    return "Restyle annotation";
}

} // namespace slideio::viewer::ui
```

These strings are not run through `tr()` because this file is deliberately Qt-free so the tests can reach it. The panel is where translation belongs if `.ts` files ever arrive; note it in the header when they do.

- [ ] **Step 5: Register the source file**

In `src/ui/CMakeLists.txt`, after `src/AnnotationListPanel.cpp`:

```cmake
    src/AnnotationPropertiesEdit.cpp
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-ui-tests
ctest --test-dir build/build -C Release -R ui-tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h src/ui/src/AnnotationPropertiesEdit.cpp src/ui/CMakeLists.txt tests/ui/AnnotationPropertiesEditTest.cpp tests/CMakeLists.txt
git commit -m "Add AnnotationPropertiesEdit, the properties editor's testable core"
```

---

### Task 9: `AnnotationPropertiesPanel` and its wiring

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h`
- Create: `src/ui/src/AnnotationPropertiesPanel.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Modify: `src/ui/src/MainWindow.cpp` (Impl members, View menu, dock creation, wiring, destructor)

**Interfaces:**
- Consumes: `ui::propertiesEdit`, `ui::propertiesEditDescription`, `ui::kAnnotationClassifications` (Task 8); `app::AnnotationModel`; `app::setPropertiesWithUndo` (Task 2); `app::UndoRedoStack`.
- Produces: `AnnotationPropertiesPanel::setAnnotationModel(app::AnnotationModel*)`, `setUndoStack(app::UndoRedoStack*)`.

- [ ] **Step 1: Write the header**

Create `src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h`:

```cpp
#pragma once

#include <QDockWidget>

#include <memory>
#include <string>

namespace slideio::viewer::app
{
class AnnotationModel;
class UndoRedoStack;
}

namespace slideio::viewer::ui
{

/// Docking editor for the selected annotation's label, classification, colour,
/// line width, fill opacity and notes, with its type, author and timestamps
/// shown read-only.
///
/// Entirely selection-driven: with nothing selected every field is cleared and
/// disabled, because a disabled panel still showing the last annotation's label
/// reads as though that annotation were selected.
///
/// Edits commit on editingFinished, on the combo's activation and on the colour
/// dialog's acceptance -- never on textChanged. A command per keystroke would
/// fill the 50-step undo stack from a single label.
class AnnotationPropertiesPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnnotationPropertiesPanel(QWidget* parent = nullptr);
    ~AnnotationPropertiesPanel() override;

    AnnotationPropertiesPanel(const AnnotationPropertiesPanel&) = delete;
    AnnotationPropertiesPanel& operator=(const AnnotationPropertiesPanel&) = delete;

    /// Non-owning; null is a legitimate state, as for the list panel.
    void setAnnotationModel(app::AnnotationModel* model);

    /// Non-owning. Without it, edits still apply but are not undoable, so the
    /// panel refuses to commit rather than produce an edit Ctrl+Z cannot reach.
    void setUndoStack(app::UndoRedoStack* stack);

private:
    void repopulate();
    void setFieldsEnabled(bool enabled);
    /// Builds the field values, asks propertiesEdit whether anything moved, and
    /// if so routes it through setPropertiesWithUndo.
    void commit();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/src/AnnotationPropertiesPanel.cpp`. The structure:

```cpp
#include "slideio/viewer/ui/AnnotationPropertiesPanel.h"

#include "slideio/viewer/app/AnnotationEditing.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/Iso8601.h"
#include "slideio/viewer/ui/AnnotationPropertiesEdit.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

namespace slideio::viewer::ui
{

struct AnnotationPropertiesPanel::Impl
{
    app::AnnotationModel* model = nullptr;
    app::UndoRedoStack* undoStack = nullptr;

    QLineEdit* label = nullptr;
    QComboBox* classification = nullptr;
    QPushButton* colorButton = nullptr;
    core::Color color{};
    QSpinBox* lineWidth = nullptr;
    QSlider* fillOpacity = nullptr;
    QPlainTextEdit* notes = nullptr;

    QLabel* type = nullptr;
    QLabel* author = nullptr;
    QLabel* created = nullptr;
    QLabel* modified = nullptr;

    /// True while repopulate() is writing into the widgets, so the signals that
    /// writing emits are not mistaken for the user editing.
    bool populating = false;
};
```

The constructor:

```cpp
AnnotationPropertiesPanel::AnnotationPropertiesPanel(QWidget* parent)
    : QDockWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle(tr("Annotation Properties"));

    auto* contents = new QWidget(this);
    auto* form = new QFormLayout(contents);

    m_impl->label = new QLineEdit(contents);
    form->addRow(tr("Label"), m_impl->label);

    m_impl->classification = new QComboBox(contents);
    m_impl->classification->setEditable(true);
    for (const char* name : kAnnotationClassifications) {
        m_impl->classification->addItem(QString::fromLatin1(name));
    }
    // Cleared after populating: the combo must start empty rather than
    // asserting "Tumor" for an annotation nobody has classified.
    m_impl->classification->setCurrentText(QString());
    form->addRow(tr("Classification"), m_impl->classification);

    m_impl->colorButton = new QPushButton(contents);
    m_impl->colorButton->setFlat(true);
    form->addRow(tr("Colour"), m_impl->colorButton);

    m_impl->lineWidth = new QSpinBox(contents);
    m_impl->lineWidth->setRange(1, 5);
    form->addRow(tr("Line width"), m_impl->lineWidth);

    m_impl->fillOpacity = new QSlider(Qt::Horizontal, contents);
    m_impl->fillOpacity->setRange(0, 100);
    form->addRow(tr("Fill opacity"), m_impl->fillOpacity);

    m_impl->notes = new QPlainTextEdit(contents);
    form->addRow(tr("Notes"), m_impl->notes);

    m_impl->type = new QLabel(contents);
    form->addRow(tr("Type"), m_impl->type);
    m_impl->author = new QLabel(contents);
    form->addRow(tr("Author"), m_impl->author);
    m_impl->created = new QLabel(contents);
    form->addRow(tr("Created"), m_impl->created);
    m_impl->modified = new QLabel(contents);
    form->addRow(tr("Modified"), m_impl->modified);

    setWidget(contents);
    setFieldsEnabled(false);
```

`setFieldsEnabled` disables the six editable widgets and leaves the four read-only labels alone:

```cpp
void AnnotationPropertiesPanel::setFieldsEnabled(bool enabled)
{
    m_impl->label->setEnabled(enabled);
    m_impl->classification->setEnabled(enabled);
    m_impl->colorButton->setEnabled(enabled);
    m_impl->lineWidth->setEnabled(enabled);
    m_impl->fillOpacity->setEnabled(enabled);
    m_impl->notes->setEnabled(enabled);
}
```

The colour swatch is restyled from `m_impl->color` in one place, called from both `repopulate` and the colour dialog's acceptance:

```cpp
void AnnotationPropertiesPanel::updateColorSwatch()
{
    const QColor shown(m_impl->color.r, m_impl->color.g, m_impl->color.b);
    m_impl->colorButton->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid palette(mid);")
            .arg(shown.name()));
    m_impl->colorButton->setText(shown.name().toUpper());
}
```

Declare `updateColorSwatch()` beside `repopulate()` in the header's private section.

Still inside the constructor, connect the commit triggers:

```cpp
    connect(m_impl->label, &QLineEdit::editingFinished, this, [this]() { commit(); });
    connect(m_impl->classification, &QComboBox::currentTextChanged, this,
            [this](const QString&) {
                if (!m_impl->populating) {
                    commit();
                }
            });
    connect(m_impl->lineWidth, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        if (!m_impl->populating) {
            commit();
        }
    });
    connect(m_impl->fillOpacity, &QSlider::sliderReleased, this, [this]() { commit(); });
    connect(m_impl->notes, &QPlainTextEdit::textChanged, this, [this]() {
        // Deliberately does not commit: notes commit on focus loss, through the
        // event filter below. textChanged fires per keystroke.
    });
    m_impl->notes->installEventFilter(this);
```

The notes field commits on focus loss, through an event filter:

```cpp
bool AnnotationPropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_impl->notes && event->type() == QEvent::FocusOut) {
        commit();
    }
    return QDockWidget::eventFilter(watched, event);
}
```

Declare it `protected` in the header alongside the other overrides, and include `<QEvent>`.

The colour button:

```cpp
    connect(m_impl->colorButton, &QPushButton::clicked, this, [this]() {
        const QColor current(m_impl->color.r, m_impl->color.g, m_impl->color.b);
        const QColor picked = QColorDialog::getColor(current, this, tr("Annotation Colour"));
        if (!picked.isValid()) {
            return;
        }
        m_impl->color = core::Color{static_cast<uint8_t>(picked.red()),
                                    static_cast<uint8_t>(picked.green()),
                                    static_cast<uint8_t>(picked.blue()),
                                    m_impl->color.a};
        updateColorSwatch();
        commit();
    });
```

The alpha channel is carried across rather than taken from the dialog: `fillOpacity` is the slider's job, and `Color::a` is the stroke's own alpha, which nothing in the UI currently edits. Taking the dialog's alpha would silently make the outline translucent.

`repopulate()`:

```cpp
void AnnotationPropertiesPanel::repopulate()
{
    const core::Annotation* annotation =
        m_impl->model == nullptr ? nullptr : m_impl->model->find(m_impl->model->selectedId());

    if (annotation == nullptr) {
        m_impl->populating = true;
        m_impl->label->clear();
        m_impl->classification->setCurrentText(QString());
        m_impl->notes->clear();
        m_impl->type->clear();
        m_impl->author->clear();
        m_impl->created->clear();
        m_impl->modified->clear();
        m_impl->populating = false;
        setFieldsEnabled(false);
        return;
    }

    setFieldsEnabled(true);
    m_impl->populating = true;

    const core::AnnotationProperties& properties = annotation->properties();
    // Never written into while it has focus: setPlainText and setText move the
    // caret to the start, which would pull it out from under someone typing.
    // The panel's own commit writes identical values back anyway, since the
    // commit already compared and skipped no-ops.
    if (!m_impl->label->hasFocus()) {
        m_impl->label->setText(QString::fromStdString(properties.label));
    }
    if (!m_impl->classification->hasFocus()) {
        m_impl->classification->setCurrentText(QString::fromStdString(properties.classification));
    }
    if (!m_impl->notes->hasFocus()) {
        m_impl->notes->setPlainText(QString::fromStdString(properties.notes));
    }
    if (!m_impl->lineWidth->hasFocus()) {
        m_impl->lineWidth->setValue(static_cast<int>(properties.lineWidth));
    }
    if (!m_impl->fillOpacity->hasFocus()) {
        m_impl->fillOpacity->setValue(static_cast<int>(properties.fillOpacity * 100.0f));
    }
    m_impl->color = properties.color;
    // Restyle the swatch button here from m_impl->color.

    m_impl->type->setText(shapeName(*annotation));
    m_impl->author->setText(QString::fromStdString(annotation->metadata().author));
    m_impl->created->setText(
        QString::fromStdString(core::formatIso8601Utc(annotation->metadata().createdAt)));
    m_impl->modified->setText(
        QString::fromStdString(core::formatIso8601Utc(annotation->metadata().modifiedAt)));

    m_impl->populating = false;
}
```

`formatIso8601Utc` is the function's actual name — verified in `src/core/include/slideio/viewer/core/Iso8601.h`. It returns an empty string for any instant before the epoch, and a default-constructed `time_point` *is* the epoch, so an annotation carrying unset metadata displays `1970-01-01T00:00:00Z` rather than blank. Every annotation this application writes carries real timestamps, so this only shows up on a hand-written file; leave it rather than inventing a placeholder, and let the odd date be the signal that the file is odd.

`commit()`:

```cpp
void AnnotationPropertiesPanel::commit()
{
    if (m_impl->populating || m_impl->model == nullptr || m_impl->undoStack == nullptr) {
        return;
    }
    const std::string id = m_impl->model->selectedId();
    const core::Annotation* annotation = m_impl->model->find(id);
    if (annotation == nullptr) {
        return;
    }

    AnnotationFieldValues fields;
    fields.label = m_impl->label->text().toStdString();
    fields.classification = m_impl->classification->currentText().toStdString();
    fields.notes = m_impl->notes->toPlainText().toStdString();
    fields.color = m_impl->color;
    fields.lineWidth = static_cast<float>(m_impl->lineWidth->value());
    fields.fillOpacity = static_cast<float>(m_impl->fillOpacity->value()) / 100.0f;

    const auto edited = propertiesEdit(annotation->properties(), fields);
    if (!edited.has_value()) {
        return;
    }

    const std::string description =
        propertiesEditDescription(annotation->properties(), *edited);
    app::setPropertiesWithUndo(*m_impl->model, *m_impl->undoStack, id, *edited, description);
}
```

`setAnnotationModel` mirrors the list panel's: disconnect the old model, connect `selectionChanged` to a lambda that calls `commit()` **before** `repopulate()` — so a label typed and then abandoned by clicking another annotation is committed rather than discarded — and connect `annotationChanged`, `cleared` and `modelReset` to `repopulate()` directly.

```cpp
    connect(m_impl->model, &app::AnnotationModel::selectionChanged, this,
            [this](const std::string&) {
                // Commit first: Qt does not guarantee focus-out reaches the
                // field before the new selection arrives, and a label typed and
                // then abandoned by clicking another row must not be lost.
                commit();
                repopulate();
            });
```

- [ ] **Step 3: Register the source file**

In `src/ui/CMakeLists.txt`, after `src/AnnotationPropertiesEdit.cpp`:

```cmake
    src/AnnotationPropertiesPanel.cpp
```

- [ ] **Step 4: Wire it into MainWindow**

Mirror Task 7 exactly: an `annotationPropertiesPanel` member and an `annotationPropertiesToggleAction`; the include; dock creation after the annotation list dock with `objectName` `"AnnotationPropertiesPanel"` and `hide()`; a toggle action with text `"Annotation &Properties"` and shortcut `Ctrl+Shift+E`; the View menu entry after `annotationListToggleAction`; and after `buildAnnotationPersistence()`:

```cpp
    m_impl->annotationPropertiesPanel->setAnnotationModel(m_impl->annotations.get());
    m_impl->annotationPropertiesPanel->setUndoStack(m_impl->undoStack.get());
```

Add its deletion to `~MainWindow` beside the list panel's.

- [ ] **Step 5: Build and run every suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass.

- [ ] **Step 6: Manual check**

Kill the viewer, install, verify by timestamp. Run group V checks V11-V16 from the spec, plus a re-run of V1 and V2 now that a second dock exists.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h src/ui/src/AnnotationPropertiesPanel.cpp src/ui/src/MainWindow.cpp src/ui/CMakeLists.txt
git commit -m "Add the annotation properties panel"
```

---

### Task 10: Run group V and record the results

The panels have no automated coverage by construction. This task is where F is actually verified, and it is not optional.

**Files:**
- Modify: `docs/superpowers/specs/2026-10-14-annotation-panels-design.md` (group V results)
- Modify: `CLAUDE.md` (the "Not built yet" list and the architecture table)
- Modify: `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md` (close item 8)

- [ ] **Step 1: Run every automated suite one more time**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: four suites, all passing. Record the counts.

- [ ] **Step 2: Install and verify the artifact**

Kill the viewer, then:

```bash
cmake --install build/build --config Release
ls -l build/install/release/bin/slideio-viewer.exe build/build/Release/slideio-viewer.exe
```

The timestamps must match. A running viewer makes this a silent no-op that still exits 0, and everything measured afterwards would be the old code.

- [ ] **Step 3: Work through V1-V16**

Use the table in the spec. For each check record pass, fail, or not-run with the reason. **Write down what was actually observed, not what was expected** — the P-group and U-group entries are useful precisely because they say what happened.

Remember the group V setup note: the fixture slides carry annotations from the U-group run of 2026-10-14 (the H&E slide holds 5, `pJP31mCherry.czi` holds 1), so V3 checks the list count against what the slide actually holds, not against a number from an older document.

- [ ] **Step 4: Record the results in the spec**

Add a results table to `docs/superpowers/specs/2026-10-14-annotation-panels-design.md` under group V, in the same shape as the U-group results in the persistence follow-ups spec: the date, the commit it was run against, and a row per check. Write up any new defect found the way the Escape-cancels-a-move defect was written up — what was observed, what the cause was, and what the fix was.

- [ ] **Step 5: Update CLAUDE.md**

Two edits:

1. In **Not built yet**, change "annotation panels (list, properties, layers)" to "annotation layers panel" — the list and properties panels now exist.
2. In the **Architecture** diagram, add `AnnotationEditing` to the Application layer line and the two new panels plus their policy files to the Presentation line, and update the `.cpp` counts.

Do **not** claim in `CLAUDE.md` that any runtime path was verified beyond what step 3 actually observed. The file says so itself: a claim about runtime behaviour there is only as good as the last time somebody ran it, and earlier drafts asserting otherwise were wrong.

- [ ] **Step 6: Close follow-up item 8**

In `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md`, mark the sub-project F entry closed with the date and the commit that moved ownership, in the same style as the item 7 note already there.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-10-14-annotation-panels-design.md docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md CLAUDE.md
git commit -m "Record the group V manual run and close follow-up item 8"
```

---

## Notes for the implementer

**The three tasks most likely to go wrong, and why:**

- **Task 4 (ownership).** No test can observe it, and a lifetime mistake surfaces as a crash on exit rather than a build failure. Do step 9's manual checks properly, especially the fourth one — closing the window inside the autosave debounce is the case the destructor change exists for.
- **Task 3 (deletion routing).** `pushDeleteCommand` changes from "record what the caller already did" to "do it and record it". There is exactly one caller (`ViewportWidget.cpp:4232-4233`) and its `remove()` call *follows* the push, so leaving it behind is redundant rather than destructive — the annotation is deleted once and the undo step is still recorded. Delete it anyway: a line that looks like it performs the deletion but does not is how the next change to that handler goes wrong.
- **Task 9 (properties commit loop).** The panel writes into its own widgets on repopulate, and those writes emit the same signals a user's edit does. The `populating` flag is what keeps a repopulation from being mistaken for an edit; if it is missed on any connection, selecting an annotation pushes a spurious undo step and restamps `modifiedAt`.

**On the `restore` call in Task 2.** `AnnotationModel` has no properties setter — `setGeometry` restamps `modifiedAt` and would be wrong for a revert, which is exactly why `restore` was added in `9826da6`. `setPropertiesWithUndo` builds the `after` state itself (so `setProperties` stamps it once, correctly) and then calls `restore` to put the whole annotation back under its own id without a second stamp.
