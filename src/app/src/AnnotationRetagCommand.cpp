#include "slideio/viewer/app/AnnotationRetagCommand.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <utility>

namespace slideio::viewer::app
{

std::unique_ptr<AnnotationRetagCommand> AnnotationRetagCommand::create(
    AnnotationModel& model, std::vector<core::RetagChange> changes, std::string description)
{
    if (changes.empty()) {
        return nullptr;
    }
    return std::unique_ptr<AnnotationRetagCommand>(
        new AnnotationRetagCommand(model, std::move(changes), std::move(description)));
}

AnnotationRetagCommand::AnnotationRetagCommand(AnnotationModel& model,
                                               std::vector<core::RetagChange> changes,
                                               std::string description)
    : m_model(model), m_changes(std::move(changes)), m_description(std::move(description))
{
}

void AnnotationRetagCommand::apply(bool after)
{
    std::vector<core::Annotation> states;
    states.reserve(m_changes.size());
    for (const core::RetagChange& change : m_changes) {
        states.push_back(after ? change.after : change.before);
    }
    m_model.restoreMany(states);
}

void AnnotationRetagCommand::execute()
{
    apply(true);
}

void AnnotationRetagCommand::undo()
{
    apply(false);
}

std::string AnnotationRetagCommand::description() const
{
    return m_description;
}

} // namespace slideio::viewer::app
