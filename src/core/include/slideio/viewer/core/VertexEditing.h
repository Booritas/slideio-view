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

/// `poly` with vertex `index` at `position`. An index past the end returns the
/// polygon unchanged: the only caller passes an index nearestVertex produced,
/// so out of range means a bug upstream, and corrupting the shape would be a
/// worse answer than doing nothing.
[[nodiscard]] PolygonGeometry withVertexMoved(const PolygonGeometry& poly, std::size_t index,
                                              PointF position);

/// `poly` with a new vertex at `position`, placed at afterIndex + 1 so it lies
/// between the segment's endpoints. On the closing segment of a closed shape
/// afterIndex is the last index and the vertex is appended, which is the same
/// place.
[[nodiscard]] PolygonGeometry withVertexInserted(const PolygonGeometry& poly,
                                                 std::size_t afterIndex, PointF position);

/// `poly` without vertex `index`; absent when that would take the shape below
/// kMinPolygonVertices for a closed shape or kMinPolylineVertices for an open
/// one, or when the index is past the end.
///
/// Those are the same constants the serializer enforces on read and write, so
/// this cannot produce a shape the application would refuse to save or reopen
/// -- the rule ui::pendingShapeCommits already follows for the drawing tools.
[[nodiscard]] std::optional<PolygonGeometry> withVertexRemoved(const PolygonGeometry& poly,
                                                               std::size_t index);

/// True when `a` and `b` have the same vertices in the same order and the same
/// closed flag.
///
/// Exists because a completed gesture that changed nothing must record no undo
/// step, and ui::moveEdit cannot answer that here: it compares bounding boxes,
/// which is exact for a translation but blind to an interior vertex moving
/// inside an unchanged extent. PointF is an aggregate with no operator==, so
/// the coordinates are compared directly.
[[nodiscard]] bool sameVertices(const PolygonGeometry& a, const PolygonGeometry& b);

} // namespace slideio::viewer::core
