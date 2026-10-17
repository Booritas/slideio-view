#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <optional>

namespace slideio::viewer::core
{

/// Where an insertion on an edge belongs: the index of the segment's FIRST
/// vertex, and the point on that segment nearest the press. The new vertex
/// goes at afterIndex + 1.
struct EdgeHit
{
    std::size_t afterIndex;
    PointF position;
};

/// Index of the vertex within `toleranceSlideUnits` of `point`; absent when
/// none is in range.
///
/// When several are in range the NEAREST wins, and a tie goes to the lower
/// index. That is what makes a dense trace editable at any zoom without
/// hiding handles the user cannot then grab -- design spec 5.1.
///
/// Indexes `poly.vertices`, which is NOT core::outline(): outline() drops
/// consecutive duplicate vertices, so its indices do not address stored ones.
/// Every polygon saved before 2026-10-17 carries such a duplicate.
[[nodiscard]] std::optional<std::size_t> nearestVertex(const PolygonGeometry& poly, PointF point,
                                                       double toleranceSlideUnits);

/// The segment within `toleranceSlideUnits` of `point`, nearest first; absent
/// when the press is off the outline.
///
/// A closed shape's closing segment -- last vertex back to first -- is
/// included; an open one's is not, because it does not exist.
[[nodiscard]] std::optional<EdgeHit> nearestEdge(const PolygonGeometry& poly, PointF point,
                                                 double toleranceSlideUnits);

} // namespace slideio::viewer::core
