#pragma once

#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/Annotation.h"

#include <cstddef>
#include <memory>
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
    /// The only way to build one. `before` absent means the operation created
    /// the annotation; `after` absent means it deleted it; both present means
    /// it modified it in place.
    ///
    /// Returns null when neither state was supplied, because such a command
    /// records nothing: it would sit on the stack as a step that does nothing
    /// when the user presses Ctrl+Z -- worse than no undo, because they will
    /// assume it worked. Null is what UndoRedoStack::push already ignores, so
    /// the rejection happens here rather than being remembered at each of the
    /// push sites.
    [[nodiscard]] static std::unique_ptr<AnnotationEditCommand> create(
        AnnotationModel& model, std::string id, std::optional<core::Annotation> before,
        std::optional<core::Annotation> after, std::string description);

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

private:
    AnnotationEditCommand(AnnotationModel& model, std::string id,
                          std::optional<core::Annotation> before,
                          std::optional<core::Annotation> after,
                          std::string description);

    /// One rule for all three operations: drop whatever is stored under this id,
    /// then put `state` back if there is one.
    void apply(const std::optional<core::Annotation>& state);

    AnnotationModel& m_model;
    std::string m_id;
    /// Where the annotation sat in the model when the operation was recorded.
    /// Restoring it anywhere else changes which shape paints on top and which
    /// one a click finds.
    std::size_t m_index;
    std::optional<core::Annotation> m_before;
    std::optional<core::Annotation> m_after;
    std::string m_description;
};

} // namespace slideio::viewer::app
