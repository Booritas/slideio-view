#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <algorithm>
#include <cmath>
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

/// `shape` with every vertex scaled into physical units.
///
/// A negative scale would mirror the shape and a NaN would poison every sum.
/// Neither is a calibration, so both collapse to zero -- resolutionX is 0 on an
/// uncalibrated slide and a caller may pass it straight through.
Outline scaledOutline(const Outline& shape, PixelSize pixelSize)
{
    const double sx = pixelSize.x > 0.0 ? pixelSize.x : 0.0;
    const double sy = pixelSize.y > 0.0 ? pixelSize.y : 0.0;

    Outline scaled;
    scaled.closed = shape.closed;
    scaled.points.reserve(shape.points.size());
    for (const PointF& p : shape.points) {
        scaled.points.push_back(PointF{p.x * sx, p.y * sy});
    }
    return scaled;
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

ShapeMetrics measure(const AnnotationGeometry& geometry, PixelSize pixelSize)
{
    // The expensive question, asked once, then delegated. An open path never
    // needs it: its area is absent either way, and open freehand traces carry
    // the most vertices by far.
    const Outline shape = outline(geometry);
    const bool crosses = shape.closed && isSelfIntersecting(scaledOutline(shape, pixelSize));
    return measureKnown(geometry, pixelSize, crosses);
}

ShapeMetrics measureKnown(const AnnotationGeometry& geometry, PixelSize pixelSize,
                          bool selfIntersecting)
{
    // outline() is the single place that turns any shape into points plus a
    // closed flag, and a rectangle's outline is its normalised corners -- the
    // same box hitTest uses and paintAnnotations strokes. Measuring from it is
    // what keeps the number, the painted region and the hit-tested region in
    // agreement, and means a new geometry type needs no change here.
    const Outline scaled = scaledOutline(outline(geometry), pixelSize);

    ShapeMetrics metrics;
    metrics.closed = scaled.closed;

    const std::size_t n = scaled.points.size();
    if (n < 2) {
        return metrics;
    }

    const std::size_t segmentCount = scaled.closed ? n : n - 1;
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const PointF a = scaled.points[i];
        const PointF b = scaled.points[(i + 1) % n];
        metrics.outlineLength += std::hypot(b.x - a.x, b.y - a.y);
    }

    if (!scaled.closed) {
        // No area for an open path, so the supplied flag is irrelevant and
        // deliberately not echoed: selfIntersecting means "this is why the area
        // is missing", and for an open path the reason is simply that it is open.
        return metrics;
    }

    if (metrics.outlineLength == 0.0) {
        // Every vertex collapsed onto one point: a stray click, or a caller
        // passing an uncalibrated slide's zero resolution straight through. Its
        // zero-length segments all overlap, so a self-intersection test says
        // yes -- but there is no missing area here to explain. A shape with no
        // perimeter encloses nothing, unambiguously, and reporting that as
        // "self-intersecting" would name a reason that is not the real one.
        metrics.area = 0.0;
        return metrics;
    }

    metrics.selfIntersecting = selfIntersecting;
    if (metrics.selfIntersecting) {
        return metrics;
    }

    // Shoelace, absolute so winding direction does not matter.
    double twiceSigned = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const PointF a = scaled.points[i];
        const PointF b = scaled.points[(i + 1) % n];
        twiceSigned += a.x * b.y - b.x * a.y;
    }
    metrics.area = std::abs(twiceSigned) / 2.0;
    return metrics;
}

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
