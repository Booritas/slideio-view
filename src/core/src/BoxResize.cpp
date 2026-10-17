#include "slideio/viewer/core/BoxResize.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace slideio::viewer::core
{

std::array<PointF, 8> boxHandlePositions(const RectF& box)
{
    const double left = box.x;
    const double right = box.x + box.width;
    const double top = box.y;
    const double bottom = box.y + box.height;
    const double midX = box.x + box.width * 0.5;
    const double midY = box.y + box.height * 0.5;

    // Enumerator order: clockwise from the top-left corner.
    return {PointF{left, top},    PointF{midX, top},     PointF{right, top},
            PointF{right, midY},  PointF{right, bottom}, PointF{midX, bottom},
            PointF{left, bottom}, PointF{left, midY}};
}

std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point,
                                          double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    const std::array<PointF, 8> handles = boxHandlePositions(box);

    std::optional<BoxHandle> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const double distance = std::hypot(point.x - handles[i].x, point.y - handles[i].y);
        if (distance > tolerance) {
            continue;
        }
        // Strictly nearer, so a tie keeps the earlier enumerator and a
        // degenerate box -- where all eight coincide -- answers TopLeft rather
        // than whichever the loop happened to reach last.
        if (!best.has_value() || distance < bestDistance) {
            best = static_cast<BoxHandle>(i);
            bestDistance = distance;
        }
    }
    return best;
}

namespace
{

std::optional<AnnotationGeometry> replaceBox(const RectangleGeometry&, const RectF& box)
{
    return RectangleGeometry{PointF{box.x, box.y},
                             PointF{box.x + box.width, box.y + box.height}};
}

std::optional<AnnotationGeometry> replaceBox(const EllipseGeometry&, const RectF& box)
{
    return EllipseGeometry{PointF{box.x, box.y},
                           PointF{box.x + box.width, box.y + box.height}};
}

// A traced shape is not defined by a box. Scaling its vertices is a different
// feature with different arithmetic -- design spec 8.
std::optional<AnnotationGeometry> replaceBox(const PolygonGeometry&, const RectF&)
{
    return std::nullopt;
}

} // namespace

RectF withBoxHandleAt(const RectF& box, BoxHandle handle, PointF position)
{
    double left = box.x;
    double right = box.x + box.width;
    double top = box.y;
    double bottom = box.y + box.height;

    switch (handle) {
    case BoxHandle::TopLeft:
        left = position.x;
        top = position.y;
        break;
    case BoxHandle::Top:
        top = position.y;
        break;
    case BoxHandle::TopRight:
        right = position.x;
        top = position.y;
        break;
    case BoxHandle::Right:
        right = position.x;
        break;
    case BoxHandle::BottomRight:
        right = position.x;
        bottom = position.y;
        break;
    case BoxHandle::Bottom:
        bottom = position.y;
        break;
    case BoxHandle::BottomLeft:
        left = position.x;
        bottom = position.y;
        break;
    case BoxHandle::Left:
        left = position.x;
        break;
    }

    // Extents may come out negative. See the header: that is the flip, not a
    // bug, and normalising here would stop the shape flipping at all.
    return RectF{left, top, right - left, bottom - top};
}

std::optional<AnnotationGeometry> withBoxReplaced(const AnnotationGeometry& geom, const RectF& box)
{
    return std::visit([&](const auto& shape) { return replaceBox(shape, box); }, geom);
}

} // namespace slideio::viewer::core
