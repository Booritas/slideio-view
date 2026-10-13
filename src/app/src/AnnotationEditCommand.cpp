#include "slideio/viewer/app/AnnotationEditCommand.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <utility>

namespace slideio::viewer::app
{

std::unique_ptr<AnnotationEditCommand> AnnotationEditCommand::create(
    AnnotationModel& model, std::string id, std::optional<core::Annotation> before,
    std::optional<core::Annotation> after, std::string description)
{
    // A command that records neither state would undo to nothing and redo to
    // nothing. Rejected here rather than at each push site, so a site added
    // later cannot forget the check: push() already ignores a null command.
    if (!before.has_value() && !after.has_value()) {
        return nullptr;
    }

    return std::unique_ptr<AnnotationEditCommand>(new AnnotationEditCommand(
        model, std::move(id), std::move(before), std::move(after), std::move(description)));
}

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

void AnnotationEditCommand::apply(const std::optional<core::Annotation>& state)
{
    // insert() drops silently when the id is already present, so the remove is
    // not optional -- without it, undoing a move would leave the annotation at
    // its moved position. remove() on an absent id is a no-op returning false,
    // which is what makes this safe when some other path already deleted it.
    //
    // remove() also clears the selection when it was this annotation that was
    // selected, which is the whole of what an undone create has to do about it.
    m_model.remove(m_id);

    if (state.has_value()) {
        // insertAt, not insert: re-appending would promote the annotation over
        // everything drawn after it, so an undone move would make a background
        // region paint over a focus inside it and steal clicks in the overlap.
        // kNotFound clamps to the end, which is where insert() would have put it.
        m_model.insertAt(m_index, *state);
        // Selection follows the operation, so the user can see what changed even
        // when the annotation is outside the current view. Refused by the model
        // if the insert above was dropped, which leaves the previous selection
        // standing rather than pointing at nothing.
        m_model.setSelected(m_id);
    }
}

} // namespace slideio::viewer::app
