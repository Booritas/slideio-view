#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <array>
#include <optional>

namespace slideio::viewer::core
{

/// The eight grab points of a box-shaped annotation: four corners and four
/// edge midpoints. A corner drags two edges, an edge midpoint drags one.
enum class BoxHandle
{
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
};

/// The eight handle centres of `box`, in the enumerator order above, so a
/// BoxHandle can index this array directly.
///
/// Positions are derived from the box as given. Callers hold the NORMALISED
/// box -- core::boundingBox of the annotation -- because a stored box may be
/// inverted, and handles computed from an inverted box would each sit on the
/// wrong side.
[[nodiscard]] std::array<PointF, 8> boxHandlePositions(const RectF& box);

/// The handle within `toleranceSlideUnits` of `point`; absent when none is in
/// range.
///
/// Nearest wins, and a tie goes to the earlier enumerator. On a shape only a
/// few handle-widths across, several handles are inside the tolerance at once
/// and the answer must not depend on iteration order changing later.
[[nodiscard]] std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point,
                                                        double toleranceSlideUnits);

} // namespace slideio::viewer::core
