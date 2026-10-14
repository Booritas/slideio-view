#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <optional>
#include <string>

namespace slideio::viewer::core
{

/// True when any two non-adjacent segments of `outline` cross, or when two
/// segments overlap along a line.
///
/// Takes an Outline rather than a geometry because outline() is already the one
/// place that turns any shape into points-plus-closed, and a rectangle's
/// outline is its *normalised* corners -- the same box hitTest uses. Deriving
/// the measurement, the painted region and the hit-tested region from one
/// source is what keeps them from disagreeing.
///
/// Segments meeting at a shared endpoint are NOT an intersection: consecutive
/// edges of every polygon touch, so counting that would make nothing simple.
/// Collinear overlap IS: a trace doubling back along itself has no
/// well-defined interior.
///
/// When `outline.closed` is true the closing edge is included in the test.
///
/// O(n^2) in the point count. Callers measuring long freehand traces should
/// note that an open path's area is absent regardless, so there is no reason to
/// ask this question about one.
[[nodiscard]] bool isSelfIntersecting(const Outline& outline);

} // namespace slideio::viewer::core
