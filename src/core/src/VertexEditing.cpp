#include "slideio/viewer/core/VertexEditing.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

std::optional<std::size_t> nearestVertex(const PolygonGeometry& poly, PointF point,
                                         double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    std::optional<std::size_t> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < poly.vertices.size(); ++i) {
        const PointF vertex = poly.vertices[i];
        const double distance = std::hypot(point.x - vertex.x, point.y - vertex.y);
        if (distance > tolerance) {
            continue;
        }
        // Strictly nearer, so a tie keeps the lower index and the answer does
        // not depend on iteration order changing later.
        if (!best.has_value() || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

std::optional<EdgeHit> nearestEdge(const PolygonGeometry& poly, PointF point,
                                   double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    const std::size_t count = poly.vertices.size();
    // Below two vertices there is no segment. An open path's last vertex
    // starts no segment, so it has one fewer than a closed one.
    if (count < 2) {
        return std::nullopt;
    }
    const std::size_t segmentCount = poly.closed ? count : count - 1;

    std::optional<EdgeHit> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const PointF a = poly.vertices[i];
        const PointF b = poly.vertices[(i + 1) % count];
        const PointF projected = closestPointOnSegment(a, b, point);
        const double distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance > tolerance) {
            continue;
        }
        if (!best.has_value() || distance < bestDistance) {
            best = EdgeHit{i, projected};
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace slideio::viewer::core
