#include "slideio/viewer/app/AnnotationImport.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <QByteArray>
#include <QUuid>

#include <set>
#include <string>
#include <utility>

namespace slideio::viewer::app
{

namespace
{

/// The namespace every part UUID is made in. Fixed forever: changing it
/// changes every derived id, and a re-import after the change would add every
/// point of every multi-part feature again.
const QUuid kImportedPartNamespace(QStringLiteral("{6f1a9c2e-3b7d-4e58-9a41-2c8d5e7f0b13}"));

std::string toId(const QUuid& uuid)
{
    return uuid.toString(QUuid::WithoutBraces).toStdString();
}

} // namespace

ImportPlan planImport(std::vector<core::Annotation> imported, std::vector<std::string> idSeeds,
                      const AnnotationModel& model)
{
    std::set<std::string> taken;
    for (const core::Annotation& annotation : model.annotations()) {
        taken.insert(annotation.id());
    }

    ImportPlan plan;
    plan.toAdd.reserve(imported.size());
    for (std::size_t i = 0; i < imported.size(); ++i) {
        core::Annotation& annotation = imported[i];
        const std::string seed = i < idSeeds.size() ? idSeeds[i] : std::string();
        std::string id = annotation.id();
        if (!seed.empty()) {
            id = toId(QUuid::createUuidV5(kImportedPartNamespace, QByteArray::fromStdString(seed)));
        } else if (id.empty()) {
            id = toId(QUuid::createUuid());
        }
        if (!taken.insert(id).second) {
            ++plan.alreadyPresent;
            continue;
        }
        if (id == annotation.id()) {
            plan.toAdd.push_back(std::move(annotation));
        } else {
            // An Annotation's id is fixed at construction, so a settled one
            // means rebuilding it with everything else carried over.
            plan.toAdd.emplace_back(std::move(id), annotation.geometry(), annotation.properties(),
                                    annotation.metadata());
        }
    }
    return plan;
}

} // namespace slideio::viewer::app
