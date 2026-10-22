#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <vector>

namespace slideio::viewer::app
{

class AnnotationModel;

/// What an import will actually add, decided before anything changes so the
/// user can be told.
struct ImportPlan
{
    /// Every id non-empty and new to the model, in file order.
    std::vector<core::Annotation> toAdd;
    /// Annotations not added because the scene already holds their id, or the
    /// file repeats one it already gave.
    int alreadyPresent = 0;
};

/// Turns what the GeoJSON reader returned into what an import adds.
///
/// An annotation with an empty id -- the reader leaves one wherever the file
/// gave none, since core cannot mint a UUID -- gets a fresh one, exactly as
/// AnnotationModel::add() makes them. An id the scene already holds is the
/// SAME annotation, imported before or exported from here, so it is skipped
/// rather than added twice: that is what keeps a second import of one file
/// from doubling a clinical count with nothing visibly wrong.
[[nodiscard]] ImportPlan planImport(std::vector<core::Annotation> imported, const AnnotationModel& model);

} // namespace slideio::viewer::app
