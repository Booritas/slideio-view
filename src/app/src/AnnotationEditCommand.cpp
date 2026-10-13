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
