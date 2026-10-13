#include "slideio/viewer/app/UndoRedoStack.h"

#include <cstddef>
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
    // One erase rather than a loop of them -- push adds one command, so the
    // overshoot is at most one, and the shift it costs is over 50 pointers.
    // A deque would avoid the shift and pay for it in locality on every walk
    // of the stack; at this depth neither is measurable.
    const int overshoot = static_cast<int>(m_commands.size()) - m_maxDepth;
    if (overshoot > 0) {
        m_commands.erase(m_commands.begin(),
                         m_commands.begin() + static_cast<std::ptrdiff_t>(overshoot));
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
