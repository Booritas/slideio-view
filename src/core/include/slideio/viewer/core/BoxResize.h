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

/// `box` with `handle` moved to `position`. A corner moves two edges, an edge
/// midpoint moves one, and the opposite edges never move.
///
/// The result may carry a NEGATIVE extent, and deliberately is not clamped:
/// dragging a handle past its opposite edge flips the box and the resize
/// continues, which is the same freedom a creation drag has. The geometry
/// built from it is an inverted pair, and core::boundingBox normalises that.
/// Clamping or taking an absolute value here would make the shape stick
/// instead of flipping. Design spec 6.4.
[[nodiscard]] RectF withBoxHandleAt(const RectF& box, BoxHandle handle, PointF position);

/// `geom` rebuilt around `box`, preserving its type; absent for a shape that
/// is not box-defined.
///
/// Dispatches with std::visit onto an overload set, NOT a std::get_if chain.
/// A get_if chain would let a future box-shaped type fall silently through to
/// "not resizable" -- the shape of the readGeometry trap CLAUDE.md warns
/// about. With std::visit the compiler names the site until every shape
/// answers. This is the only place this work touches the variant.
[[nodiscard]] std::optional<AnnotationGeometry> withBoxReplaced(const AnnotationGeometry& geom,
                                                                const RectF& box);

} // namespace slideio::viewer::core
