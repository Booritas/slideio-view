#include "slideio/viewer/core/AnnotationGeometry.h"

#include <algorithm>

namespace slideio::viewer::core
{

namespace
{

RectF normalized(const RectangleGeometry& rect)
{
    const double x0 = std::min(rect.topLeft.x, rect.bottomRight.x);
    const double y0 = std::min(rect.topLeft.y, rect.bottomRight.y);
    const double x1 = std::max(rect.topLeft.x, rect.bottomRight.x);
    const double y1 = std::max(rect.topLeft.y, rect.bottomRight.y);
    return RectF{x0, y0, x1 - x0, y1 - y0};
}

bool hitTestShape(const RectangleGeometry& rect, PointF point, double tolerance)
{
    const RectF box = normalized(rect);
    return point.x >= box.x - tolerance
        && point.x <= box.x + box.width + tolerance
        && point.y >= box.y - tolerance
        && point.y <= box.y + box.height + tolerance;
}

} // namespace

RectF boundingBox(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return normalized(shape); }, geom);
}

bool hitTest(const AnnotationGeometry& geom, PointF point, double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    return std::visit([&](const auto& shape) { return hitTestShape(shape, point, tolerance); },
                      geom);
}

} // namespace slideio::viewer::core
