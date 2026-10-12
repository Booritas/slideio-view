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

AnnotationType typeOfShape(const RectangleGeometry&)
{
    return AnnotationType::Rectangle;
}

Outline outlineShape(const RectangleGeometry& rect)
{
    const RectF box = normalized(rect);
    return Outline{{PointF{box.x, box.y},
                    PointF{box.x + box.width, box.y},
                    PointF{box.x + box.width, box.y + box.height},
                    PointF{box.x, box.y + box.height}},
                   true};
}

AnnotationGeometry translatedShape(const RectangleGeometry& rect, double dx, double dy)
{
    return RectangleGeometry{PointF{rect.topLeft.x + dx, rect.topLeft.y + dy},
                             PointF{rect.bottomRight.x + dx, rect.bottomRight.y + dy}};
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

AnnotationType typeOf(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return typeOfShape(shape); }, geom);
}

Outline outline(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return outlineShape(shape); }, geom);
}

AnnotationGeometry translated(const AnnotationGeometry& geom, double dx, double dy)
{
    return std::visit([&](const auto& shape) { return translatedShape(shape, dx, dy); }, geom);
}

} // namespace slideio::viewer::core
