#pragma once

#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/AnnotationCategories.h"

#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::app
{

class AnnotationModel;

/// One reversible retag of MANY annotations, recorded as the states they were
/// in before and after.
///
/// AnnotationEditCommand is not reusable for this. It is one annotation by
/// construction and it carries the index the annotation sat at, because
/// restoring a created or deleted annotation at the wrong position changes
/// which shape paints on top and which one a click finds. A retag changes a
/// property in place and moves nothing, so it needs none of that machinery;
/// sharing the class would give every existing command a vector it never uses.
class AnnotationRetagCommand : public ICommand
{
public:
    /// Null when `changes` is empty, because such a command records nothing: it
    /// would sit on the stack as a step that does nothing when the user presses
    /// Ctrl+Z. Null is what UndoRedoStack::push already ignores.
    [[nodiscard]] static std::unique_ptr<AnnotationRetagCommand> create(
        AnnotationModel& model, std::vector<core::RetagChange> changes, std::string description);

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

private:
    AnnotationRetagCommand(AnnotationModel& model, std::vector<core::RetagChange> changes,
                           std::string description);

    void apply(bool after);

    AnnotationModel& m_model;
    std::vector<core::RetagChange> m_changes;
    std::string m_description;
};

} // namespace slideio::viewer::app
