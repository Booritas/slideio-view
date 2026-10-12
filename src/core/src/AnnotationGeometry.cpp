#include "slideio/viewer/core/AnnotationGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

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

RectF normalized(const PolygonGeometry& poly)
{
    if (poly.vertices.empty()) {
        return RectF{0.0, 0.0, 0.0, 0.0};
    }
    double x0 = poly.vertices.front().x;
    double y0 = poly.vertices.front().y;
    double x1 = x0;
    double y1 = y0;
    for (const PointF& vertex : poly.vertices) {
        x0 = std::min(x0, vertex.x);
        y0 = std::min(y0, vertex.y);
        x1 = std::max(x1, vertex.x);
        y1 = std::max(y1, vertex.y);
    }
    return RectF{x0, y0, x1 - x0, y1 - y0};
}

/// Even-odd ray casting (Franklin's algorithm). The half-open vertical span
/// test -- strictly greater on one end, not on the other -- is what makes a
/// ray passing exactly through a vertex cross once rather than twice or zero
/// times. A horizontal tissue edge produces whole rows of vertices at the same
/// y, so this is the common case, not the corner case. `straddles` also
/// guarantees the two y values differ, so the division cannot be by zero.
bool pointInPolygon(const std::vector<PointF>& vertices, PointF point)
{
    bool inside = false;
    const std::size_t count = vertices.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const bool straddles = (vertices[i].y > point.y) != (vertices[j].y > point.y);
        if (!straddles) {
            continue;
        }
        const double t = (point.y - vertices[i].y) / (vertices[j].y - vertices[i].y);
        if (point.x < vertices[i].x + t * (vertices[j].x - vertices[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

/// Distance from `point` to the segment a-b, with a zero-length segment
/// treated as the point a rather than divided by.
double distanceToSegment(PointF a, PointF b, PointF point)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0) {
        return std::hypot(point.x - a.x, point.y - a.y);
    }
    double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

bool hitTestShape(const PolygonGeometry& poly, PointF point, double tolerance)
{
    const std::size_t count = poly.vertices.size();
    if (count == 0) {
        return false;
    }
    if (count == 1) {
        return std::hypot(point.x - poly.vertices[0].x, point.y - poly.vertices[0].y)
               <= tolerance;
    }
    // An open path has no interior, however enclosing its shape looks.
    if (poly.closed && count >= kMinPolygonVertices && pointInPolygon(poly.vertices, point)) {
        return true;
    }
    // A closed shape tests its closing segment too; an open one stops one short.
    const std::size_t segments = poly.closed ? count : count - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        if (distanceToSegment(poly.vertices[i], poly.vertices[(i + 1) % count], point)
            <= tolerance) {
            return true;
        }
    }
    return false;
}

AnnotationType typeOfShape(const PolygonGeometry& poly)
{
    return poly.closed ? AnnotationType::Polygon : AnnotationType::Polyline;
}

Outline outlineShape(const PolygonGeometry& poly)
{
    return Outline{poly.vertices, poly.closed};
}

AnnotationGeometry translatedShape(const PolygonGeometry& poly, double dx, double dy)
{
    PolygonGeometry moved = poly;
    for (PointF& vertex : moved.vertices) {
        vertex.x += dx;
        vertex.y += dy;
    }
    return moved;
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
