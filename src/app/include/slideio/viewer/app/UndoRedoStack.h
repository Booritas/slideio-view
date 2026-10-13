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
