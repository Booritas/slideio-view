#include "slideio/viewer/app/AnnotationImport.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <QUuid>

#include <set>
#include <string>
#include <utility>

namespace slideio::viewer::app
{

ImportPlan planImport(std::vector<core::Annotation> imported, const AnnotationModel& model)
{
    std::set<std::string> taken;
    for (const core::Annotation& annotation : model.annotations()) {
        taken.insert(annotation.id());
    }

    ImportPlan plan;
    plan.toAdd.reserve(imported.size());
    for (core::Annotation& annotation : imported) {
        if (annotation.id().empty()) {
            // An Annotation's id is fixed at construction, so a minted one
            // means rebuilding it with everything else carried over.
            std::string id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            taken.insert(id);
            plan.toAdd.emplace_back(std::move(id), annotation.geometry(), annotation.properties(),
                                    annotation.metadata());
            continue;
        }
        if (!taken.insert(annotation.id()).second) {
            ++plan.alreadyPresent;
            continue;
        }
        plan.toAdd.push_back(std::move(annotation));
    }
    return plan;
}

} // namespace slideio::viewer::app
