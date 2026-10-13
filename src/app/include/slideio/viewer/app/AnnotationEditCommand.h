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
