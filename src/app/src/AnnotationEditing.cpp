#include "slideio/viewer/app/AnnotationEditing.h"

#include "slideio/viewer/app/AnnotationEditCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"

#include <optional>
#include <utility>

namespace slideio::viewer::app
{

namespace
{

/// Equality for AnnotationProperties, which has no operator==. Written here
/// rather than on the struct because core is the wrong place to decide what
/// "the user changed nothing" means for an editor's field set.
bool sameProperties(const core::AnnotationProperties& a, const core::AnnotationProperties& b)
{
    return a.label == b.label && a.classification == b.classification && a.notes == b.notes
           && a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b
           && a.color.a == b.color.a && a.lineWidth == b.lineWidth
           && a.fillOpacity == b.fillOpacity;
}

} // namespace

bool deleteWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                    std::string description)
{
    const core::Annotation* stored = model.find(id);
    if (stored == nullptr) {
        return false;
    }

    // Copied before the removal: find() returns a pointer any mutation
    // invalidates, and this copy is what undo puts back.
    core::Annotation before = *stored;

    // Copied too: `id` may alias the caller's own storage, and remove() can
    // destroy what it names.
    const std::string targetId = id;

    // Built BEFORE the removal, and pushed after. AnnotationEditCommand captures
    // the annotation's index in its constructor (AnnotationEditCommand.cpp:40,
    // whose comment says a delete is recorded before the removal), and undo puts
    // the annotation back at that index. Built after the remove, indexOf would
    // return kNotFound, insertAt would clamp it to the end, and an undone delete
    // would silently promote the annotation over everything drawn after it --
    // changing which shape paints on top and which one a click finds.
    auto command = AnnotationEditCommand::create(model, targetId, std::move(before), std::nullopt,
                                                 std::move(description));

    if (!model.remove(targetId)) {
        return false;
    }

    stack.push(std::move(command));
    return true;
}

bool setPropertiesWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                           core::AnnotationProperties properties, std::string description)
{
    const core::Annotation* stored = model.find(id);
    if (stored == nullptr) {
        return false;
    }
    if (sameProperties(stored->properties(), properties)) {
        return false;
    }

    const std::string targetId = id;
    core::Annotation before = *stored;

    // Built from the before state so everything except the properties -- id,
    // geometry, createdAt, author -- is carried across untouched. setProperties
    // restamps modifiedAt, which is correct here: this is a real edit.
    core::Annotation after = before;
    after.setProperties(std::move(properties));

    if (!model.restore(after)) {
        return false;
    }

    stack.push(AnnotationEditCommand::create(model, targetId, std::move(before), std::move(after),
                                             std::move(description)));
    return true;
}

} // namespace slideio::viewer::app
