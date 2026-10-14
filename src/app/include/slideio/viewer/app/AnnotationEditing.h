#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <string>

namespace slideio::viewer::app
{

class AnnotationModel;
class UndoRedoStack;

/// Undoable mutation of one annotation.
///
/// These live here rather than on the widget that happens to call them because
/// F gives the model a second mutator: a panel calling AnnotationModel::remove
/// directly would produce an edit the undo stack never saw, and the user would
/// press Ctrl+Z on an annotation that does not come back. Both functions mutate
/// through the model's own API first and record afterwards, which is the order
/// AnnotationEditCommand is built for -- execute() runs only on redo.
///
/// `description` is supplied by the caller rather than generated here: the
/// wording is user-facing ui text ("Delete Polygon"), and app must not reach
/// into Qt's translation machinery to produce it.

/// Removes `id` and records the removal. False when the model does not hold
/// `id`, in which case nothing is removed and nothing is pushed.
bool deleteWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                    std::string description);

/// Stores `properties` on `id` and records the change. False when the model
/// does not hold `id`, and also when `properties` equals what is already
/// stored: an edit that changes nothing must not push a step the user would
/// have to press Ctrl+Z twice to get past, and must not restamp modifiedAt --
/// autosave would otherwise write a modification that did not happen.
bool setPropertiesWithUndo(AnnotationModel& model, UndoRedoStack& stack, const std::string& id,
                           core::AnnotationProperties properties, std::string description);

} // namespace slideio::viewer::app
