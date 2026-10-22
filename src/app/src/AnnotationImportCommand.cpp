#include "slideio/viewer/app/AnnotationImportCommand.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <utility>

namespace slideio::viewer::app
{

std::unique_ptr<AnnotationImportCommand> AnnotationImportCommand::create(
    AnnotationModel& model, std::vector<core::Annotation> added, std::string description)
{
    if (added.empty()) {
        return nullptr;
    }
    return std::unique_ptr<AnnotationImportCommand>(
        new AnnotationImportCommand(model, std::move(added), std::move(description)));
}

AnnotationImportCommand::AnnotationImportCommand(AnnotationModel& model, std::vector<core::Annotation> added,
                                                 std::string description)
    : m_model(model), m_added(std::move(added)), m_description(std::move(description))
{
}

void AnnotationImportCommand::execute()
{
    m_model.insertMany(m_added);
}

void AnnotationImportCommand::undo()
{
    std::vector<std::string> ids;
    ids.reserve(m_added.size());
    for (const core::Annotation& annotation : m_added) {
        ids.push_back(annotation.id());
    }
    m_model.removeMany(ids);
}

std::string AnnotationImportCommand::description() const
{
    return m_description;
}

} // namespace slideio::viewer::app
