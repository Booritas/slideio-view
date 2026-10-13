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
    // Captured at construction, while the annotation is still in the model: the
    // push sites record an operation after it has already been applied, and for
    // a delete they record it before the removal.
    , m_index(0)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_description(std::move(description))
{
    m_index = m_model.indexOf(m_id);
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
        // insertAt, not insert: re-appending would promote the annotation over
        // everything drawn after it, so an undone move would make a background
        // region paint over a focus inside it and steal clicks in the overlap.
        // kNotFound clamps to the end, which is where insert() would have put it.
        m_model.insertAt(m_index, *state);
        // Selection follows the operation, so the user can see what changed even
        // when the annotation is outside the current view.
        m_model.setSelected(m_id);
    } else {
        m_model.clearSelection();
    }
}

} // namespace slideio::viewer::app
