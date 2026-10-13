# Annotation Undo/Redo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add undo and redo over annotation create, delete and move, so that no single mis-click costs a pathologist work.

**Architecture:** One `ICommand` implementation — `AnnotationEditCommand`, carrying an annotation id with its state before and after — recorded on an `UndoRedoStack` in the application layer. Commands are pushed *after* the mutation has already gone through `AnnotationModel`'s own API, at the four points where an operation completes. `ViewportWidget` stops repainting only from its own mutation sites and starts repainting from the model's signals, because undo edits the model from outside the widget.

**Tech Stack:** C++17, Qt 6 Widgets, Catch2 v3, CMake + Conan 2.

**Spec:** `docs/superpowers/specs/2026-10-13-annotation-undo-redo-design.md`

## Global Constraints

- C++17. 4-space indent, 120-character lines, Allman braces for classes and functions, K&R for control flow, `#pragma once`.
- Naming: `PascalCase` types and files, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, lowercase namespaces.
- Include order: own header, project headers, Qt headers, std headers.
- `slideio-viewer-app` links **only** `slideio-viewer-core` and `Qt6::Core`. It does **not** link spdlog — no logging in the app layer.
- `slideio-viewer-core` is Qt-free. Nothing added here goes in core.
- **No test binary creates a `QApplication`, so no test may construct a widget.** Logic that needs testing lives in the app layer.
- Undo depth is `kDefaultUndoDepth = 50` (FR-ANN-18 minimum). The 50–200 preference is explicitly out of scope.
- Build: `cmake --build build/build --config Release`. Test: `ctest --test-dir build/build -C Release --output-on-failure`.
- **Never run a Qt-linked test executable directly** — only under `ctest`. Only `core-tests` is safe to run directly.
- **Kill any running viewer before `cmake --install`.** Windows locks the running `.exe`, the copy is skipped, and the command still exits 0 — everything measured afterwards is the old binary.
- `main` is unprotected; commit directly to it. All AI-generated code is human-reviewed before commit.

## Review Focus

Five failure modes the spec implies that an obvious happy-path test suite would miss. Each has a test in the task that owns the code.

1. **Undo or redo on an empty stack** — must do nothing and not crash, not index past the end. (Task 1)
2. **The depth cap boundary** — at exactly `maxDepth`, one more push must drop the *oldest* command and leave the stack fully usable, not corrupt the current index. (Task 1)
3. **Undoing an edit whose annotation is no longer in the model** — another path removed it; the undo must leave the model consistent rather than crash or double-insert. (Task 2)
4. **Push after undo** — the superseded redo tail must be discarded, so redo cannot resurrect a future the user has already branched away from. (Task 1)
5. **A command with neither a before nor an after state** — records nothing; must be rejected at construction rather than becoming an undo step that silently does nothing when the user presses Ctrl+Z. (Task 2)

---

## File Structure

**Create:**
- `src/app/include/slideio/viewer/app/UndoRedoStack.h` — `ICommand` interface and the stack. No Qt, no `Q_OBJECT`.
- `src/app/src/UndoRedoStack.cpp`
- `src/app/include/slideio/viewer/app/AnnotationEditCommand.h` — the one concrete command.
- `src/app/src/AnnotationEditCommand.cpp`
- `tests/app/UndoRedoStackTest.cpp`
- `tests/app/AnnotationEditCommandTest.cpp`

**Modify:**
- `src/app/CMakeLists.txt` — two new sources.
- `tests/CMakeLists.txt` — two new test sources.
- `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — `setUndoStack`, `undoLastPendingVertex`, `isDragInFlight`.
- `src/ui/src/ViewportWidget.cpp` — model-signal connections, four push sites, pending-vertex entry point.
- `src/ui/src/MainWindow.cpp` — own the stack, Edit menu, actions, clearing, status messages.
- `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md` — the U-group manual checks.

---

## Task 1: UndoRedoStack and the ICommand interface

**Files:**
- Create: `src/app/include/slideio/viewer/app/UndoRedoStack.h`
- Create: `src/app/src/UndoRedoStack.cpp`
- Test: `tests/app/UndoRedoStackTest.cpp`
- Modify: `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `app::ICommand` (pure virtual `execute()`, `undo()`, `description()`); `app::kDefaultUndoDepth` (`constexpr int`, 50); `app::UndoRedoStack` with `push(std::unique_ptr<ICommand>)`, `undo()`, `redo()`, `clear()`, `canUndo()`, `canRedo()`, `undoDescription()`, `redoDescription()`, and the `std::function<void()> onStackChanged` callback.

- [ ] **Step 1: Write the failing tests**

Create `tests/app/UndoRedoStackTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/UndoRedoStack.h"

#include <memory>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

/// Records what the stack asked it to do, so a test can assert on order.
class RecordingCommand : public app::ICommand
{
public:
    RecordingCommand(std::string name, std::vector<std::string>& log)
        : m_name(std::move(name))
        , m_log(log)
    {
    }

    void execute() override { m_log.push_back("redo:" + m_name); }
    void undo() override { m_log.push_back("undo:" + m_name); }
    [[nodiscard]] std::string description() const override { return m_name; }

private:
    std::string m_name;
    std::vector<std::string>& m_log;
};

std::unique_ptr<app::ICommand> cmd(const std::string& name, std::vector<std::string>& log)
{
    return std::make_unique<RecordingCommand>(name, log);
}

} // namespace

TEST_CASE("a new stack can neither undo nor redo", "[app][UndoRedoStack]")
{
    app::UndoRedoStack stack;

    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.undoDescription().empty());
    REQUIRE(stack.redoDescription().empty());
}

TEST_CASE("undo and redo on an empty stack do nothing", "[app][UndoRedoStack]")
{
    app::UndoRedoStack stack;

    stack.undo();
    stack.redo();

    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("push does not execute the command", "[app][UndoRedoStack]")
{
    std::vector<std::string> log;
    app::UndoRedoStack stack;

    stack.push(cmd("A", log));

    REQUIRE(log.empty());
    REQUIRE(stack.canUndo());
    REQUIRE(stack.undoDescription() == "A");
}

TEST_CASE("undo walks backwards and redo walks forwards", "[app][UndoRedoStack]")
{
    std::vector<std::string> log;
    app::UndoRedoStack stack;
    stack.push(cmd("A", log));
    stack.push(cmd("B", log));

    stack.undo();
    stack.undo();
    stack.redo();
    stack.redo();

    REQUIRE(log == std::vector<std::string>{"undo:B", "undo:A", "redo:A", "redo:B"});
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("push after undo discards the redo tail", "[app][UndoRedoStack]")
{
    std::vector<std::string> log;
    app::UndoRedoStack stack;
    stack.push(cmd("A", log));
    stack.push(cmd("B", log));
    stack.undo();

    REQUIRE(stack.canRedo());
    stack.push(cmd("C", log));

    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.undoDescription() == "C");
}

TEST_CASE("the depth cap drops the oldest command and stays usable", "[app][UndoRedoStack]")
{
    std::vector<std::string> log;
    app::UndoRedoStack stack(3);
    stack.push(cmd("A", log));
    stack.push(cmd("B", log));
    stack.push(cmd("C", log));
    stack.push(cmd("D", log));   // one past the cap

    stack.undo();
    stack.undo();
    stack.undo();

    REQUIRE(log == std::vector<std::string>{"undo:D", "undo:C", "undo:B"});
    REQUIRE_FALSE(stack.canUndo());   // A was dropped, not D
}

TEST_CASE("clear empties both directions", "[app][UndoRedoStack]")
{
    std::vector<std::string> log;
    app::UndoRedoStack stack;
    stack.push(cmd("A", log));
    stack.undo();

    stack.clear();

    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("onStackChanged fires on push, undo, redo and clear", "[app][UndoRedoStack]")
{
    std::vector<std::string> log;
    app::UndoRedoStack stack;
    int changes = 0;
    stack.onStackChanged = [&changes]() { ++changes; };

    stack.push(cmd("A", log));
    stack.undo();
    stack.redo();
    stack.clear();

    REQUIRE(changes == 4);
}
```

- [ ] **Step 2: Add both files to the build**

In `src/app/CMakeLists.txt`, add `src/UndoRedoStack.cpp` to `add_library(slideio-viewer-app STATIC ...)`. Do **not** add the header to the source list — that listing exists only so AUTOMOC sees `Q_OBJECT` headers, and `UndoRedoStack.h` has no `Q_OBJECT`.

In `tests/CMakeLists.txt`, add `app/UndoRedoStackTest.cpp` to `add_executable(slideio-viewer-app-tests ...)`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`
Expected: FAIL to compile — `Cannot open include file: 'slideio/viewer/app/UndoRedoStack.h'`.

- [ ] **Step 4: Write the header**

Create `src/app/include/slideio/viewer/app/UndoRedoStack.h`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::app
{

/// One reversible operation. Architecture section 10.7.
class ICommand
{
public:
    virtual ~ICommand() = default;

    /// Re-applies the operation. Called by redo(), never by push(): a command
    /// is recorded after its edit has already been made through the model's own
    /// API, so the first application never goes through here.
    virtual void execute() = 0;

    virtual void undo() = 0;

    /// Fit for a menu item and a status message: "Delete Polygon".
    [[nodiscard]] virtual std::string description() const = 0;
};

/// FR-ANN-18 requires at least 50 levels. UI design section 7.4 allows
/// configuring up to 200; that preference is deferred, so this is the only
/// depth in use.
constexpr int kDefaultUndoDepth = 50;

/// Per-slide stack of reversible annotation operations.
///
/// Cleared whenever the open slide or scene changes, so a command can never
/// outlive the annotations it refers to. MainWindow hangs that off
/// AnnotationModel's cleared() and modelReset() signals.
class UndoRedoStack
{
public:
    explicit UndoRedoStack(int maxDepth = kDefaultUndoDepth);
    ~UndoRedoStack();

    UndoRedoStack(const UndoRedoStack&) = delete;
    UndoRedoStack& operator=(const UndoRedoStack&) = delete;

    /// Records a command whose edit has already been applied. Discards any redo
    /// tail: the new edit supersedes the future the user branched away from.
    /// A null command is ignored.
    void push(std::unique_ptr<ICommand> command);

    void undo();
    void redo();
    void clear();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    /// Empty when the corresponding direction is unavailable.
    [[nodiscard]] std::string undoDescription() const;
    [[nodiscard]] std::string redoDescription() const;

    /// Fires after any change to either end. The ui enables and labels its menu
    /// items from this rather than polling.
    std::function<void()> onStackChanged;

private:
    void notifyChanged() const;

    std::vector<std::unique_ptr<ICommand>> m_commands;
    /// Index of the command undo() would apply; -1 when there is nothing to undo.
    int m_currentIndex = -1;
    int m_maxDepth;
};

} // namespace slideio::viewer::app
```

- [ ] **Step 5: Write the implementation**

Create `src/app/src/UndoRedoStack.cpp`:

```cpp
#include "slideio/viewer/app/UndoRedoStack.h"

#include <utility>

namespace slideio::viewer::app
{

UndoRedoStack::UndoRedoStack(int maxDepth)
    : m_maxDepth(maxDepth > 0 ? maxDepth : 1)
{
}

UndoRedoStack::~UndoRedoStack() = default;

void UndoRedoStack::push(std::unique_ptr<ICommand> command)
{
    if (command == nullptr) {
        return;
    }

    // Everything after the current position is a future this edit supersedes.
    m_commands.erase(m_commands.begin() + (static_cast<std::ptrdiff_t>(m_currentIndex) + 1),
                     m_commands.end());
    m_commands.push_back(std::move(command));

    // Cap at the oldest end: recent history is the part the user will reach for.
    while (static_cast<int>(m_commands.size()) > m_maxDepth) {
        m_commands.erase(m_commands.begin());
    }

    m_currentIndex = static_cast<int>(m_commands.size()) - 1;
    notifyChanged();
}

void UndoRedoStack::undo()
{
    if (!canUndo()) {
        return;
    }
    m_commands[static_cast<std::size_t>(m_currentIndex)]->undo();
    --m_currentIndex;
    notifyChanged();
}

void UndoRedoStack::redo()
{
    if (!canRedo()) {
        return;
    }
    ++m_currentIndex;
    m_commands[static_cast<std::size_t>(m_currentIndex)]->execute();
    notifyChanged();
}

void UndoRedoStack::clear()
{
    m_commands.clear();
    m_currentIndex = -1;
    notifyChanged();
}

bool UndoRedoStack::canUndo() const
{
    return m_currentIndex >= 0;
}

bool UndoRedoStack::canRedo() const
{
    return (m_currentIndex + 1) < static_cast<int>(m_commands.size());
}

std::string UndoRedoStack::undoDescription() const
{
    if (!canUndo()) {
        return {};
    }
    return m_commands[static_cast<std::size_t>(m_currentIndex)]->description();
}

std::string UndoRedoStack::redoDescription() const
{
    if (!canRedo()) {
        return {};
    }
    return m_commands[static_cast<std::size_t>(m_currentIndex + 1)]->description();
}

void UndoRedoStack::notifyChanged() const
{
    if (onStackChanged) {
        onStackChanged();
    }
}

} // namespace slideio::viewer::app
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure -R app-tests`
Expected: PASS, all eight new cases.

- [ ] **Step 7: Commit**

```bash
git add src/app/include/slideio/viewer/app/UndoRedoStack.h src/app/src/UndoRedoStack.cpp \
        tests/app/UndoRedoStackTest.cpp src/app/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add UndoRedoStack and the ICommand interface"
```

---

## Task 2: AnnotationEditCommand

**Files:**
- Create: `src/app/include/slideio/viewer/app/AnnotationEditCommand.h`
- Create: `src/app/src/AnnotationEditCommand.cpp`
- Test: `tests/app/AnnotationEditCommandTest.cpp`
- Modify: `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `app::ICommand` from Task 1; `app::AnnotationModel` (`insert(core::Annotation)`, `remove(const std::string&) -> bool`, `find(const std::string&) -> const core::Annotation*`, `setSelected(const std::string&)`, `clearSelection()`).
- Produces: `app::AnnotationEditCommand(AnnotationModel&, std::string id, std::optional<core::Annotation> before, std::optional<core::Annotation> after, std::string description)` and `static bool AnnotationEditCommand::isValid(const std::optional<core::Annotation>&, const std::optional<core::Annotation>&)`.

**Note on rejecting the empty command.** The spec says a command with neither state is "rejected in the constructor". This codebase does not throw — the repository returns result structs and the model drops invalid input silently — so rejection is realized as a **checked precondition**: `isValid()` is public and static, every push site calls it before constructing, and an invalid command applies nothing if one is ever built anyway.

- [ ] **Step 1: Write the failing tests**

Create `tests/app/AnnotationEditCommandTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationEditCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"

#include <optional>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

core::AnnotationGeometry polyline(int vertexCount)
{
    std::vector<core::PointF> vertices;
    vertices.reserve(static_cast<std::size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        vertices.push_back(core::PointF{static_cast<double>(i), static_cast<double>(i) * 2.0});
    }
    return core::PolygonGeometry{std::move(vertices), false};
}

/// The command a create push site builds: no before, the stored annotation after.
std::unique_ptr<app::AnnotationEditCommand> createCommand(app::AnnotationModel& model,
                                                          const std::string& id)
{
    const core::Annotation* stored = model.find(id);
    REQUIRE(stored != nullptr);
    return std::make_unique<app::AnnotationEditCommand>(model, id, std::nullopt, *stored,
                                                        "Draw Rectangle");
}

} // namespace

TEST_CASE("undoing a create removes the annotation", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 10.0, 10.0));
    auto command = createCommand(model, id);

    command->undo();

    REQUIRE(model.annotations().empty());
    REQUIRE(model.find(id) == nullptr);
}

TEST_CASE("redoing a create restores it under the same id", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 10.0, 10.0));
    auto command = createCommand(model, id);

    command->undo();
    command->execute();

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("a create survives several undo/redo cycles without drifting",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(1.0, 2.0, 3.0, 4.0));
    auto command = createCommand(model, id);

    for (int i = 0; i < 5; ++i) {
        command->undo();
        command->execute();
    }

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("undoing a delete restores geometry and metadata exactly",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    model.setDefaultAuthor("booritas");
    const std::string id = model.add(rect(5.0, 6.0, 7.0, 8.0));
    const core::Annotation before = *model.find(id);

    app::AnnotationEditCommand command(model, id, before, std::nullopt, "Delete Rectangle");
    command.execute();   // the delete itself
    REQUIRE(model.find(id) == nullptr);

    command.undo();

    const core::Annotation* restored = model.find(id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->metadata().author == "booritas");
    REQUIRE(restored->metadata().createdAt == before.metadata().createdAt);
    REQUIRE(restored->metadata().modifiedAt == before.metadata().modifiedAt);
    REQUIRE(std::get<core::RectangleGeometry>(restored->geometry()).topLeft.x == 5.0);
}

TEST_CASE("undoing a move returns the geometry vertex for vertex",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(polyline(32));
    const core::Annotation before = *model.find(id);

    core::Annotation after = before;
    after.setGeometry(core::translated(before.geometry(), 100.0, -50.0));
    model.setGeometry(id, after.geometry());

    app::AnnotationEditCommand command(model, id, before, after, "Move Freehand Line");
    command.undo();

    const auto& restored = std::get<core::PolygonGeometry>(model.find(id)->geometry());
    const auto& original = std::get<core::PolygonGeometry>(before.geometry());
    REQUIRE(restored.vertices.size() == original.vertices.size());
    for (std::size_t i = 0; i < original.vertices.size(); ++i) {
        REQUIRE(restored.vertices[i].x == original.vertices[i].x);
        REQUIRE(restored.vertices[i].y == original.vertices[i].y);
    }
}

TEST_CASE("undo selects the annotation it restored", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const core::Annotation before = *model.find(id);

    app::AnnotationEditCommand command(model, id, before, std::nullopt, "Delete Rectangle");
    command.execute();
    command.undo();

    REQUIRE(model.selectedId() == id);
}

TEST_CASE("undoing a create clears the selection", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    model.setSelected(id);
    auto command = createCommand(model, id);

    command->undo();

    REQUIRE(model.selectedId().empty());
}

TEST_CASE("undoing an edit whose annotation is already gone is harmless",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const core::Annotation before = *model.find(id);
    core::Annotation after = before;
    after.setGeometry(rect(5.0, 5.0, 6.0, 6.0));

    app::AnnotationEditCommand command(model, id, before, after, "Move Rectangle");
    model.remove(id);   // some other path removed it

    command.undo();     // must not crash, must not double-insert

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("a command with neither state is rejected", "[app][AnnotationEditCommand]")
{
    REQUIRE_FALSE(app::AnnotationEditCommand::isValid(std::nullopt, std::nullopt));
}

TEST_CASE("commands with either state are accepted", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const core::Annotation stored = *model.find(id);

    REQUIRE(app::AnnotationEditCommand::isValid(std::nullopt, stored));
    REQUIRE(app::AnnotationEditCommand::isValid(stored, std::nullopt));
    REQUIRE(app::AnnotationEditCommand::isValid(stored, stored));
}
```

- [ ] **Step 2: Add both files to the build**

In `src/app/CMakeLists.txt`, add `src/AnnotationEditCommand.cpp`. In `tests/CMakeLists.txt`, add `app/AnnotationEditCommandTest.cpp`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build build/build --config Release`
Expected: FAIL to compile — `Cannot open include file: 'slideio/viewer/app/AnnotationEditCommand.h'`.

- [ ] **Step 4: Write the header**

Create `src/app/include/slideio/viewer/app/AnnotationEditCommand.h`:

```cpp
#pragma once

#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/Annotation.h"

#include <optional>
#include <string>

namespace slideio::viewer::app
{

class AnnotationModel;

/// One reversible edit to one annotation, recorded as the state it was in
/// before and the state it was in after.
///
/// Create, delete and move are all this shape, which is why there is one
/// command class rather than three: the inverse logic is identical in all of
/// them. Property changes, reshaping and vertex editing fit it unchanged when
/// they arrive.
class AnnotationEditCommand : public ICommand
{
public:
    /// `before` absent means the operation created the annotation; `after`
    /// absent means it deleted it; both present means it modified it in place.
    /// Callers check isValid() first -- see the note there.
    AnnotationEditCommand(AnnotationModel& model, std::string id,
                          std::optional<core::Annotation> before,
                          std::optional<core::Annotation> after,
                          std::string description);

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

    /// False when neither state was supplied. Such a command records nothing,
    /// so it would sit on the stack as an undo step that does nothing when the
    /// user presses Ctrl+Z -- worse than no undo, because they will assume it
    /// worked. Push sites check this before constructing.
    [[nodiscard]] static bool isValid(const std::optional<core::Annotation>& before,
                                      const std::optional<core::Annotation>& after);

private:
    /// One rule for all three operations: drop whatever is stored under this id,
    /// then put `state` back if there is one.
    void apply(const std::optional<core::Annotation>& state);

    AnnotationModel& m_model;
    std::string m_id;
    std::optional<core::Annotation> m_before;
    std::optional<core::Annotation> m_after;
    std::string m_description;
};

} // namespace slideio::viewer::app
```

- [ ] **Step 5: Write the implementation**

Create `src/app/src/AnnotationEditCommand.cpp`:

```cpp
#include "slideio/viewer/app/AnnotationEditCommand.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <utility>

namespace slideio::viewer::app
{

AnnotationEditCommand::AnnotationEditCommand(AnnotationModel& model, std::string id,
                                             std::optional<core::Annotation> before,
                                             std::optional<core::Annotation> after,
                                             std::string description)
    : m_model(model)
    , m_id(std::move(id))
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_description(std::move(description))
{
}

void AnnotationEditCommand::execute()
{
    apply(m_after);
}

void AnnotationEditCommand::undo()
{
    apply(m_before);
}

std::string AnnotationEditCommand::description() const
{
    return m_description;
}

bool AnnotationEditCommand::isValid(const std::optional<core::Annotation>& before,
                                    const std::optional<core::Annotation>& after)
{
    return before.has_value() || after.has_value();
}

void AnnotationEditCommand::apply(const std::optional<core::Annotation>& state)
{
    if (!isValid(m_before, m_after)) {
        return;
    }

    // insert() drops silently when the id is already present, so the remove is
    // not optional -- without it, undoing a move would leave the annotation at
    // its moved position. remove() on an absent id is a no-op returning false,
    // which is what makes this safe when some other path already deleted it.
    m_model.remove(m_id);

    if (state.has_value()) {
        m_model.insert(*state);
        // Selection follows the operation, so the user can see what changed even
        // when the annotation is outside the current view.
        m_model.setSelected(m_id);
    } else {
        m_model.clearSelection();
    }
}

} // namespace slideio::viewer::app
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure -R app-tests`
Expected: PASS, all ten new cases.

- [ ] **Step 7: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationEditCommand.h \
        src/app/src/AnnotationEditCommand.cpp tests/app/AnnotationEditCommandTest.cpp \
        src/app/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add AnnotationEditCommand, one command for create, delete and move"
```

---

## Task 3: The viewport repaints from the model's signals

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` (near line 1933, where the model is constructed)

**Interfaces:**
- Consumes: `app::AnnotationModel`'s existing signals.
- Produces: nothing new. This is a prerequisite for undo being visible.

**Why there is no automated test.** No suite creates a `QApplication`, so `ViewportWidget` cannot be constructed in a test. Verification is the build plus manual check **U1** in Task 8. This is the one task in the plan with no test, and it is structural rather than behavioural: with it absent, an undone create stays painted on screen until an unrelated pan or zoom.

- [ ] **Step 1: Connect the six signals**

In `src/ui/src/ViewportWidget.cpp`, immediately after `m_impl->annotations = std::make_unique<app::AnnotationModel>();` (line 1933), insert:

```cpp
    // Until undo/redo, this widget was the only thing that mutated the model, so
    // repainting from its own mutation sites was sufficient. It no longer is: an
    // undone create must vanish from the screen without this widget having done
    // anything. The existing direct update() calls stay -- Qt coalesces repaints
    // within an event-loop turn, so the duplicate costs nothing, and auditing
    // each one to prove it was only ever about the model is not worth the risk.
    //
    // Closes item 7 of the annotation persistence follow-ups.
    const auto repaintOnModelChange = [this]() { update(); };
    connect(m_impl->annotations.get(), &app::AnnotationModel::annotationAdded, this,
            repaintOnModelChange);
    connect(m_impl->annotations.get(), &app::AnnotationModel::annotationRemoved, this,
            repaintOnModelChange);
    connect(m_impl->annotations.get(), &app::AnnotationModel::annotationChanged, this,
            repaintOnModelChange);
    connect(m_impl->annotations.get(), &app::AnnotationModel::selectionChanged, this,
            repaintOnModelChange);
    connect(m_impl->annotations.get(), &app::AnnotationModel::cleared, this,
            repaintOnModelChange);
    connect(m_impl->annotations.get(), &app::AnnotationModel::modelReset, this,
            repaintOnModelChange);
```

A functor taking fewer arguments than the signal is allowed by Qt's connect, so the `const std::string&` carried by four of these signals needs no parameter here.

- [ ] **Step 2: Build and run the full suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`
Expected: builds with no new warnings; all four suites pass (nothing should change — this task adds a repaint path, not behaviour).

- [ ] **Step 3: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Repaint the viewport from AnnotationModel's signals"
```

---

## Task 4: MainWindow owns the stack, with an Edit menu

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — add `setUndoStack`
- Modify: `src/ui/src/ViewportWidget.cpp` — store the pointer

**Interfaces:**
- Consumes: `app::UndoRedoStack` from Task 1.
- Produces: `ViewportWidget::setUndoStack(app::UndoRedoStack*)`; `MainWindow::Impl::undoStack`, `undoAction`, `redoAction`, and `updateUndoActions()`.

- [ ] **Step 1: Give ViewportWidget somewhere to hold the stack**

In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, beside the existing `setPersistenceService` declaration, add a forward declaration `namespace slideio::viewer::app { class UndoRedoStack; }` if one is not already present, and the setter:

```cpp
    /// Non-owning. MainWindow owns the stack and clears it on slide change.
    /// Null until set, and the push sites check for that.
    void setUndoStack(app::UndoRedoStack* stack);
```

In `src/ui/src/ViewportWidget.cpp`, add `app::UndoRedoStack* undoStack = nullptr;` to `ViewportWidget::Impl` beside the persistence pointer, and:

```cpp
void ViewportWidget::setUndoStack(app::UndoRedoStack* stack)
{
    m_impl->undoStack = stack;
}
```

Add `#include "slideio/viewer/app/UndoRedoStack.h"` to the project-header include block.

- [ ] **Step 2: Add the members to MainWindow::Impl**

In `src/ui/src/MainWindow.cpp`, in the `Impl` struct beside `annotationPersistence`:

```cpp
    std::unique_ptr<app::UndoRedoStack> undoStack;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
```

Add `#include "slideio/viewer/app/UndoRedoStack.h"` to the project-header include block.

- [ ] **Step 3: Create the Edit menu between File and View**

In `src/ui/src/MainWindow.cpp`, immediately **after** the File menu block ends and **before** `QMenu* viewMenu = owner->menuBar()->addMenu("&View");` (line 339), insert:

```cpp
        // Between File and View, where every application that has one puts it.
        QMenu* editMenu = owner->menuBar()->addMenu("&Edit");
        undoAction = new QAction("&Undo", owner);
        // The standard key for the platform rather than a hard-coded Ctrl+Z.
        undoAction->setShortcut(QKeySequence::Undo);
        undoAction->setEnabled(false);
        editMenu->addAction(undoAction);

        redoAction = new QAction("&Redo", owner);
        // Ctrl+Shift+Z alongside the platform standard: users arrive with both.
        redoAction->setShortcuts({QKeySequence(QKeySequence::Redo), QKeySequence("Ctrl+Shift+Z")});
        redoAction->setEnabled(false);
        editMenu->addAction(redoAction);
```

These are window-level menu shortcuts, deliberately **not** `Qt::WidgetWithChildrenShortcut` like the tool keys: undo must work regardless of which child has focus, as `Ctrl+0` does.

- [ ] **Step 4: Build the stack and wire it up**

In `MainWindow::Impl::buildAnnotationPersistence()`, after `viewportWidget->setPersistenceService(annotationPersistence.get());`, insert:

```cpp
    undoStack = std::make_unique<app::UndoRedoStack>();
    viewportWidget->setUndoStack(undoStack.get());
    undoStack->onStackChanged = [this]() { updateUndoActions(); };

    // Hung off the model rather than called at each slide-switch site: a future
    // way to change slides cannot forget it. Both signals fire only on the
    // slide-change and load paths, never on an edit.
    QObject::connect(viewportWidget->annotationModel(), &app::AnnotationModel::cleared, owner,
                     [this]() { undoStack->clear(); });
    QObject::connect(viewportWidget->annotationModel(), &app::AnnotationModel::modelReset, owner,
                     [this]() { undoStack->clear(); });

    QObject::connect(undoAction, &QAction::triggered, owner, [this]() {
        // The description has to be read before the undo: afterwards the top of
        // the stack is a different command.
        const QString what = QString::fromStdString(undoStack->undoDescription());
        undoStack->undo();
        statusBarManager->showTransientMessage(tr("Undo: %1").arg(what));
    });
    QObject::connect(redoAction, &QAction::triggered, owner, [this]() {
        const QString what = QString::fromStdString(undoStack->redoDescription());
        undoStack->redo();
        statusBarManager->showTransientMessage(tr("Redo: %1").arg(what));
    });

    updateUndoActions();
```

- [ ] **Step 5: Implement updateUndoActions()**

Declare `void updateUndoActions();` in `MainWindow::Impl` and define it:

```cpp
void MainWindow::Impl::updateUndoActions()
{
    if (undoAction == nullptr || redoAction == nullptr || undoStack == nullptr) {
        return;
    }

    const bool canUndo = undoStack->canUndo();
    undoAction->setEnabled(canUndo);
    // Naming the operation is most of the value: "Undo" alone does not tell the
    // user whether Ctrl+Z is about to remove the shape they just drew or undo a
    // move they have forgotten about.
    undoAction->setText(canUndo
                            ? tr("&Undo %1").arg(QString::fromStdString(undoStack->undoDescription()))
                            : tr("&Undo"));

    const bool canRedo = undoStack->canRedo();
    redoAction->setEnabled(canRedo);
    redoAction->setText(canRedo
                            ? tr("&Redo %1").arg(QString::fromStdString(undoStack->redoDescription()))
                            : tr("&Redo"));
}
```

- [ ] **Step 6: Build and run the full suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`
Expected: builds with no new warnings; all four suites pass. The Edit menu now exists with both items permanently greyed out — nothing pushes to the stack until Task 5.

- [ ] **Step 7: Commit**

```bash
git add src/ui/src/MainWindow.cpp src/ui/src/ViewportWidget.cpp \
        src/ui/include/slideio/viewer/ui/ViewportWidget.h
git commit -m "Add the Edit menu and the undo stack it drives"
```

---

## Task 5: Push sites for create and delete

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` (lines ~2992, ~3804, ~4001)

**Interfaces:**
- Consumes: `app::AnnotationEditCommand` and `app::UndoRedoStack`.
- Produces: `ViewportWidget::pushCreateCommand(const std::string& id)` and `ViewportWidget::pushDeleteCommand(const std::string& id)`, both private.

- [ ] **Step 1: Add a shape-name helper**

In the anonymous namespace near the top of `src/ui/src/ViewportWidget.cpp`, add:

```cpp
/// The shape's name for an undo description. Derived from the geometry through
/// core::typeOf(), so it cannot disagree with what was actually drawn.
QString shapeName(const core::Annotation& annotation)
{
    switch (core::typeOf(annotation.geometry())) {
    case core::AnnotationType::Rectangle:
        return QObject::tr("Rectangle");
    case core::AnnotationType::Polygon:
        return QObject::tr("Polygon");
    case core::AnnotationType::Polyline:
        return QObject::tr("Freehand Line");
    }
    return QObject::tr("Annotation");
}
```

- [ ] **Step 2: Add the two push helpers**

Declare both in `ViewportWidget`'s private section in the header, and define them in the `.cpp`:

```cpp
void ViewportWidget::pushCreateCommand(const std::string& id)
{
    if (m_impl->undoStack == nullptr || id.empty()) {
        return;
    }
    const core::Annotation* stored = m_impl->annotations->find(id);
    if (stored == nullptr) {
        return;
    }
    // Copied immediately: find() returns a pointer invalidated by any mutation.
    // The copy is also what redo re-inserts, which is why it must be taken after
    // the add -- add() mints the id, and redoing through add() again would mint a
    // different one and dangle every other command referring to this annotation.
    core::Annotation after = *stored;
    const QString what = tr("Draw %1").arg(shapeName(after));
    if (!app::AnnotationEditCommand::isValid(std::nullopt, after)) {
        return;
    }
    m_impl->undoStack->push(std::make_unique<app::AnnotationEditCommand>(
        *m_impl->annotations, id, std::nullopt, std::move(after), what.toStdString()));
}

void ViewportWidget::pushDeleteCommand(const std::string& id)
{
    if (m_impl->undoStack == nullptr || id.empty()) {
        return;
    }
    const core::Annotation* stored = m_impl->annotations->find(id);
    if (stored == nullptr) {
        return;
    }
    core::Annotation before = *stored;
    const QString what = tr("Delete %1").arg(shapeName(before));
    if (!app::AnnotationEditCommand::isValid(before, std::nullopt)) {
        return;
    }
    m_impl->undoStack->push(std::make_unique<app::AnnotationEditCommand>(
        *m_impl->annotations, id, std::move(before), std::nullopt, what.toStdString()));
}
```

Add `#include "slideio/viewer/app/AnnotationEditCommand.h"` to the project-header include block.

- [ ] **Step 3: Push on the polygon and freehand create (line ~2992)**

Change:

```cpp
    const std::string id = m_impl->annotations->add(core::PolygonGeometry{std::move(vertices), closed});
```

to:

```cpp
    const std::string id = m_impl->annotations->add(core::PolygonGeometry{std::move(vertices), closed});
    pushCreateCommand(id);
```

- [ ] **Step 4: Push on the rectangle create (line ~3804)**

Change:

```cpp
            const std::string id = m_impl->annotations->add(geometry);
```

to:

```cpp
            const std::string id = m_impl->annotations->add(geometry);
            pushCreateCommand(id);
```

- [ ] **Step 5: Push on delete (line ~4001)**

Change:

```cpp
        m_impl->annotations->remove(m_impl->annotations->selectedId());
```

to:

```cpp
        // Captured before the remove: afterwards there is nothing left to copy.
        const std::string doomedId = m_impl->annotations->selectedId();
        pushDeleteCommand(doomedId);
        m_impl->annotations->remove(doomedId);
```

Note the ordering — `pushDeleteCommand` reads the annotation out of the model, so it must run while the annotation is still there.

- [ ] **Step 6: Build and run the full suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`
Expected: builds with no new warnings; all four suites pass.

- [ ] **Step 7: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp src/ui/include/slideio/viewer/ui/ViewportWidget.h
git commit -m "Record create and delete on the undo stack"
```

---

## Task 6: Push site for move

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` — the mouse-release path that ends a move drag

**Interfaces:**
- Consumes: `pushCreateCommand`/`pushDeleteCommand` pattern from Task 5; `m_impl->moveOriginalGeometry`, `m_impl->movingId`.
- Produces: `ViewportWidget::pushMoveCommand(const std::string& id, const core::AnnotationGeometry& originalGeometry)`, private.

**The two non-sites.** A move drag calls `setGeometry` on **every mouse-move event** (line ~3863) — that is live preview, not a sequence of edits. Pushing there would fill the stack with hundreds of micro-moves from one gesture and push the user's real history off the end. And Escape-cancelling a move (line ~4023) restores `moveOriginalGeometry` itself, so there is no net change to record; a command there would be a no-op the user has to press Ctrl+Z through. Neither gets a push.

- [ ] **Step 1: Add the push helper**

Declare it in the private section of the header and define it:

```cpp
void ViewportWidget::pushMoveCommand(const std::string& id,
                                     const core::AnnotationGeometry& originalGeometry)
{
    if (m_impl->undoStack == nullptr || id.empty()) {
        return;
    }
    const core::Annotation* stored = m_impl->annotations->find(id);
    if (stored == nullptr) {
        return;
    }

    core::Annotation after = *stored;
    core::Annotation before = after;
    before.setGeometry(originalGeometry);

    // A drag that ended where it started is not an edit. Without this, a stray
    // click on an annotation would put a no-op on the stack that the user has to
    // press Ctrl+Z through before reaching a real one.
    //
    // core::Rect is an aggregate of x/y/width/height with no operator==, so the
    // four fields are compared directly.
    const core::RectF boxBefore = core::boundingBox(before.geometry());
    const core::RectF boxAfter = core::boundingBox(after.geometry());
    if (boxBefore.x == boxAfter.x && boxBefore.y == boxAfter.y
        && boxBefore.width == boxAfter.width && boxBefore.height == boxAfter.height) {
        return;
    }

    const QString what = tr("Move %1").arg(shapeName(after));
    if (!app::AnnotationEditCommand::isValid(before, after)) {
        return;
    }
    m_impl->undoStack->push(std::make_unique<app::AnnotationEditCommand>(
        *m_impl->annotations, id, std::move(before), std::move(after), what.toStdString()));
}
```

- [ ] **Step 2: Push on move release**

In `mouseReleaseEvent`, in the branch that ends a move drag (where `m_impl->dragOwner == DragOwner::MoveAnnotation`), call the helper **before** `movingId` is cleared:

```cpp
        pushMoveCommand(m_impl->movingId, m_impl->moveOriginalGeometry);
```

- [ ] **Step 3: Build and run the full suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`
Expected: builds with no new warnings; all four suites pass.

- [ ] **Step 4: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp src/ui/include/slideio/viewer/ui/ViewportWidget.h
git commit -m "Record a completed move as one undo step"
```

---

## Task 7: Ctrl+Z routing — mid-trace vertex removal and the drag guard

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp`
- Modify: `src/ui/src/MainWindow.cpp` — the undo action's handler

**Interfaces:**
- Consumes: the undo action handler from Task 4.
- Produces: `ViewportWidget::undoLastPendingVertex() -> bool` (true when a live trace consumed the key) and `ViewportWidget::isDragInFlight() const -> bool`.

- [ ] **Step 1: Add both entry points**

Declare in the public section of `ViewportWidget.h`:

```cpp
    /// Removes the last placed vertex of a live trace and returns true. Returns
    /// false when no trace is in progress, so the caller can fall through to the
    /// undo stack. Ctrl+Z during path drawing steps back one point in every
    /// vector editor, which is the instinct users arrive with; a pending shape is
    /// not an annotation and never reaches the model, so the stack is not
    /// involved.
    bool undoLastPendingVertex();

    /// True while a tool drag, a move drag or a pending trace with the button
    /// held is in progress. Undo and redo are refused during one: the widget
    /// caches movingId and pending-shape state, and an undo that removed the
    /// annotation being dragged would leave those pointing at nothing.
    [[nodiscard]] bool isDragInFlight() const;
```

- [ ] **Step 2: Implement them**

In `src/ui/src/ViewportWidget.cpp`:

```cpp
bool ViewportWidget::undoLastPendingVertex()
{
    if (!m_impl->pending.active || m_impl->pending.vertices.empty()) {
        return false;
    }
    // Deliberately the same effect as Backspace, which this reuses rather than
    // reimplements: two keys with different names must not drift apart.
    removeLastPendingVertex();
    return true;
}

bool ViewportWidget::isDragInFlight() const
{
    return m_impl->dragOwner != DragOwner::None;
}
```

**The Backspace handler is currently inline** (`ViewportWidget.cpp:3978-3990`), so `removeLastPendingVertex()` does not exist yet and must be extracted first — otherwise the two keys drift apart the next time one is touched. Declare `void removeLastPendingVertex();` in the private section and move the existing body into it unchanged:

```cpp
void ViewportWidget::removeLastPendingVertex()
{
    if (!m_impl->pending.vertices.empty()) {
        m_impl->pending.vertices.pop_back();
    }
    if (m_impl->pending.vertices.empty()) {
        // Pair discard with endToolDrag the way Escape does: a drag still
        // owned by the tool with nothing pending would let its release fall
        // into the selection-click path below.
        discardPendingShape();
        endToolDrag();
        updateCursor();
    }
    update();
}
```

Then reduce the Backspace branch to:

```cpp
    if (m_impl->pending.active && event->key() == Qt::Key_Backspace) {
        removeLastPendingVertex();
        event->accept();
```

leaving the rest of that branch as it is.

- [ ] **Step 3: Branch in the undo handler**

In `src/ui/src/MainWindow.cpp`, replace the body of the undo action's `triggered` handler from Task 4 with:

```cpp
        // One entry point for the key, with the context test in one place rather
        // than split between a shortcut and a key handler.
        if (viewportWidget->isDragInFlight()) {
            return;
        }
        if (viewportWidget->undoLastPendingVertex()) {
            return;
        }
        const QString what = QString::fromStdString(undoStack->undoDescription());
        undoStack->undo();
        statusBarManager->showTransientMessage(tr("Undo: %1").arg(what));
```

And add the same guard as the first line of the redo handler:

```cpp
        if (viewportWidget->isDragInFlight()) {
            return;
        }
```

- [ ] **Step 4: Build and run the full suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`
Expected: builds with no new warnings; all four suites pass.

- [ ] **Step 5: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp src/ui/include/slideio/viewer/ui/ViewportWidget.h \
        src/ui/src/MainWindow.cpp
git commit -m "Route Ctrl+Z to vertex removal during a live trace"
```

---

## Task 8: The manual checks

**Files:**
- Modify: `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md`

No suite creates a `QApplication`, so none of the wiring in Tasks 3–7 is reachable from a test. That list is the only coverage this layer has, and it has just been proven to work, so this increment arrives with its own checks rather than relying on someone remembering.

- [ ] **Step 1: Add the U group to item 0**

After the P-check results table in item 0, add:

```markdown
   **Group U — undo/redo, added 2026-10-13 with sub-project D. Not yet run.**
   Spec: `2026-10-13-annotation-undo-redo-design.md`. Run on one slide with
   annotations already present.

   U1. **Draw a polygon, Ctrl+Z.** It disappears. Ctrl+Y: it returns with the
       same vertices over the same tissue.
       *Break:* it stays painted until an unrelated pan or zoom — the viewport
       is not repainting from the model's signals.
   U2. **Delete an annotation, Ctrl+Z.** It returns, and returns **selected**.
   U3. **Move an annotation, Ctrl+Z.** It returns to its exact original
       position, not approximately. Compare against the saved JSON, not by eye.
   U4. **One move drag is one undo step.** After a single drag, press Ctrl+Z
       once: the annotation is fully back where it started.
       *Break:* it creeps back a few pixels per press — a command is being
       pushed per mouse-move frame, and the user's real history has been pushed
       off the end of the stack.
   U5. **Ctrl+Z mid-trace removes the last vertex** and the trace stays live.
       *Break:* a previously committed annotation disappears instead.
   U6. **Ctrl+Z with the mouse button held** during a drag does nothing.
   U7. **Escape-cancel a move, then Ctrl+Z.** The *previous* operation is
       undone, not a no-op step.
   U8. **Switch slides and back.** Edit ▸ Undo is greyed out.
   U9. **On a read-only slide** both Edit items are greyed out.
   U10. **Undo, let autosave run, reopen the slide.** The undone state is what
       loads from disk.
   U11. **The Edit menu items name the operation** ("Undo Delete Polygon") and
       the status bar shows the same text after the key.
```

- [ ] **Step 2: Update the item 0 header line**

Change the "Still open" line in item 0 to note that group U is added and unrun, so the item is no longer fully closed.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md
git commit -m "Add the undo/redo manual checks as group U"
```

---

## Self-review notes

**Spec coverage.** §3.2 command shape → Task 2. §3.3 id preservation → Task 2 Step 1 (`redoing a create restores it under the same id`) and Task 5 Step 2's comment. §3.4 stack → Task 1. §4 push sites → Tasks 5 and 6. §4.1 non-sites → Task 6 preamble, with U4 as the check. §4.2 descriptions → Task 5 Step 1. §5 signal rewiring → Task 3. §5.1 drag guard → Task 7. §5.2 selection → Task 2 (`undo selects the annotation it restored`). §6.1 menu → Task 4 Step 3. §6.2 handler branch → Task 7 Step 3. §6.3 read-only → no code, checked by U9. §6.4 clearing → Task 4 Step 4. §6.5 persistence → no code, checked by U10. §6.6 status message → Task 4 Step 4. §7.1 automated tests → Tasks 1 and 2. §7.2 manual → Task 8.

**One addition beyond the spec:** Task 6 skips a move command when the bounding box is unchanged. The spec does not mention it; a click that nudges nothing would otherwise put a no-op on the stack, which §8 of the spec calls out as the worst failure mode here ("an undo that silently does nothing").
