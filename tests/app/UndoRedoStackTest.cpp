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
    stack.push(cmd("D", log)); // one past the cap

    stack.undo();
    stack.undo();
    stack.undo();

    REQUIRE(log == std::vector<std::string>{"undo:D", "undo:C", "undo:B"});
    REQUIRE_FALSE(stack.canUndo()); // A was dropped, not D
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
