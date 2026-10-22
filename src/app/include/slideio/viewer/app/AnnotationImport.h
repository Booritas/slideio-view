#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <string>
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
/// Each annotation's id is settled first. A non-empty `idSeeds[i]` -- the
/// reader gives one to every part of a multi-part feature -- becomes a
/// name-based (version 5) UUID, so the same part of the same QuPath class gets
/// the same id on every import. Otherwise the annotation keeps the id it
/// carries, or, if that is empty, gets a fresh random one exactly as
/// AnnotationModel::add() makes them. `idSeeds` may be shorter than
/// `imported`, or empty; a missing seed is an empty one.
///
/// An id the scene already holds is then the SAME annotation, imported before
/// or exported from here, so it is skipped rather than added twice: that is
/// what keeps a second import of one file from doubling a clinical count with
/// nothing visibly wrong.
[[nodiscard]] ImportPlan planImport(std::vector<core::Annotation> imported, std::vector<std::string> idSeeds,
                                    const AnnotationModel& model);

} // namespace slideio::viewer::app
