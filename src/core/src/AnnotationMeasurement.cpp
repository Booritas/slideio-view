#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <algorithm>
#include <cstddef>

namespace slideio::viewer::core
{

namespace
{

/// Cross product of (b - a) and (c - a). Sign gives which side of ab c lies on;
/// zero means the three are collinear.
double cross(PointF a, PointF b, PointF c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/// True when c lies within the axis-aligned box spanned by a and b. Only
/// meaningful once the three are known to be collinear.
bool withinSpan(PointF a, PointF b, PointF c)
{
    return std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x)
           && std::min(a.y, b.y) <= c.y && c.y <= std::max(a.y, b.y);
}

/// True when segments ab and cd share any point. Endpoints count, so callers
/// must exclude adjacent pairs themselves.
bool segmentsIntersect(PointF a, PointF b, PointF c, PointF d)
{
    const double d1 = cross(c, d, a);
    const double d2 = cross(c, d, b);
    const double d3 = cross(a, b, c);
    const double d4 = cross(a, b, d);

    // Strict straddle on both sides: a proper crossing.
    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0))
        && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
        return true;
    }

    // Collinear touching or overlap. This is the case that makes a trace
    // doubling back along itself count as self-intersecting.
    if (d1 == 0.0 && withinSpan(c, d, a)) {
        return true;
    }
    if (d2 == 0.0 && withinSpan(c, d, b)) {
        return true;
    }
    if (d3 == 0.0 && withinSpan(a, b, c)) {
        return true;
    }
    if (d4 == 0.0 && withinSpan(a, b, d)) {
        return true;
    }
    return false;
}

} // namespace

bool isSelfIntersecting(const Outline& outline)
{
    const std::vector<PointF>& p = outline.points;
    const std::size_t n = p.size();

    // Below three points there is no pair of non-adjacent segments to cross.
    if (n < 3) {
        return false;
    }

    const std::size_t segmentCount = outline.closed ? n : n - 1;

    for (std::size_t i = 0; i < segmentCount; ++i) {
        const PointF a = p[i];
        const PointF b = p[(i + 1) % n];
        for (std::size_t j = i + 1; j < segmentCount; ++j) {
            // Adjacent segments share an endpoint by construction. For a closed
            // shape the last segment is also adjacent to the first.
            const bool adjacent =
                (j == i + 1) || (outline.closed && i == 0 && j == segmentCount - 1);
            if (adjacent) {
                continue;
            }
            const PointF c = p[j];
            const PointF d = p[(j + 1) % n];
            if (segmentsIntersect(a, b, c, d)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace slideio::viewer::core
