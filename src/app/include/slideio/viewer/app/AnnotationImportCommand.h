#pragma once

#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/Annotation.h"

#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::app
{

class AnnotationModel;

/// One reversible import of MANY annotations, recorded as the annotations it
/// added.
///
/// Neither existing command fits. AnnotationEditCommand is one annotation by
/// construction; AnnotationRetagCommand changes annotations in place and moves
/// nothing, while an import creates them. Undo removes exactly the ids it
/// added, so an annotation drawn before the import -- or after it and undone
/// first -- is never touched; redo appends them again, which is where they
/// were, since the stack is linear and nothing after them survives an undo.
class AnnotationImportCommand : public ICommand
{
public:
    /// Null when `added` is empty, because such a command records nothing: it
    /// would sit on the stack as a step that does nothing when the user presses
    /// Ctrl+Z. Null is what UndoRedoStack::push already ignores.
    [[nodiscard]] static std::unique_ptr<AnnotationImportCommand> create(
        AnnotationModel& model, std::vector<core::Annotation> added, std::string description);

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

private:
    AnnotationImportCommand(AnnotationModel& model, std::vector<core::Annotation> added,
                            std::string description);

    AnnotationModel& m_model;
    std::vector<core::Annotation> m_added;
    std::string m_description;
};

} // namespace slideio::viewer::app
