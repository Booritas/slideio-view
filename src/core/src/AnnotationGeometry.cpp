#include "slideio/viewer/core/AnnotationGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

/// The box two opposite corners span, with a non-negative width and height.
///
/// A creation drag may run in any direction and a resize handle may be dragged
/// past its opposite edge, so neither corner is reliably the top-left one.
/// Rectangle and ellipse share this because they are the same box to everything
/// that reads it -- the same reason core::withBoxReplaced is one visited
/// overload set rather than two.
RectF normalizedCorners(PointF a, PointF b)
{
    const double x0 = std::min(a.x, b.x);
    const double y0 = std::min(a.y, b.y);
    const double x1 = std::max(a.x, b.x);
    const double y1 = std::max(a.y, b.y);
    return RectF{x0, y0, x1 - x0, y1 - y0};
}

RectF normalized(const RectangleGeometry& rect)
{
    return normalizedCorners(rect.topLeft, rect.bottomRight);
}

RectF normalized(const EllipseGeometry& ellipse)
{
    return normalizedCorners(ellipse.topLeft, ellipse.bottomRight);
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
        // A sentinel, not a box at the origin: an empty polygon has no extent
        // and no position, and RectF cannot say so. No reachable path produces
        // one -- the serializer refuses below kMinPolygonVertices on both read
        // and write, and a pending trace is not an annotation -- so the caller
        // that would have to tell the two apart does not exist. Returning
        // std::optional here would make every caller handle a case none of
        // them can meet.
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
    const PointF nearest = closestPointOnSegment(a, b, point);
    return std::hypot(point.x - nearest.x, point.y - nearest.y);
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

/// Exact equality, deliberately. A zero-length segment is the degeneracy that
/// matters here; two points merely close together make a short real segment
/// that nothing is confused by.
bool samePoint(PointF a, PointF b)
{
    return a.x == b.x && a.y == b.y;
}

Outline outlineShape(const PolygonGeometry& poly)
{
    // Consecutive duplicates are dropped. They describe a zero-length segment,
    // which paints nothing, encloses nothing and adds nothing to the shoelace
    // area -- but it makes the segments on either side of it share an endpoint,
    // and isSelfIntersecting only excludes segments adjacent by INDEX. The
    // result was a hand-traced polygon reported as self-intersecting, which
    // withholds its area. Observed on a real trace, 2026-10-17: closing by
    // double-click appends a vertex on top of the last trailed one whenever the
    // cursor has not moved since, which is about half the time.
    //
    // Done here rather than on the stored vertices because this is the one
    // place painting, hit-testing and measurement all read, so it repairs
    // annotations already on disk without rewriting a single file.
    Outline result;
    result.closed = poly.closed;
    result.points.reserve(poly.vertices.size());
    for (const PointF& vertex : poly.vertices) {
        if (!result.points.empty() && samePoint(result.points.back(), vertex)) {
            continue;
        }
        result.points.push_back(vertex);
    }
    // On a closed shape the last point joins back to the first, so a final
    // point equal to the first is the same degeneracy by the other route.
    if (result.closed && result.points.size() > 1
        && samePoint(result.points.front(), result.points.back())) {
        result.points.pop_back();
    }
    return result;
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

Outline outlineShape(const EllipseGeometry& ellipse)
{
    const RectF box = normalized(ellipse);
    const double cx = box.x + box.width * 0.5;
    const double cy = box.y + box.height * 0.5;
    const double a = box.width * 0.5;
    const double b = box.height * 0.5;

    Outline result;
    result.closed = true;
    result.points.reserve(kEllipseOutlineVertices);
    for (std::size_t i = 0; i < kEllipseOutlineVertices; ++i) {
        const double theta = 2.0 * kPi * static_cast<double>(i)
                           / static_cast<double>(kEllipseOutlineVertices);
        result.points.push_back(PointF{cx + a * std::cos(theta), cy + b * std::sin(theta)});
    }
    return result;
}

// The outline IS the shape (design spec 3), so the hit test runs over exactly
// the points the painter strokes. Deriving both from one source is what stops
// the painted interior and the hit-tested interior from disagreeing.
bool hitTestShape(const EllipseGeometry& ellipse, PointF point, double tolerance)
{
    Outline shape = outlineShape(ellipse);
    return hitTestShape(PolygonGeometry{std::move(shape.points), true}, point, tolerance);
}

AnnotationType typeOfShape(const EllipseGeometry&)
{
    return AnnotationType::Ellipse;
}

AnnotationGeometry translatedShape(const EllipseGeometry& ellipse, double dx, double dy)
{
    return EllipseGeometry{PointF{ellipse.topLeft.x + dx, ellipse.topLeft.y + dy},
                           PointF{ellipse.bottomRight.x + dx, ellipse.bottomRight.y + dy}};
}

bool wellDefinedArea(const RectangleGeometry&)
{
    return true;
}

bool wellDefinedArea(const EllipseGeometry&)
{
    return true;
}

bool wellDefinedArea(const PolygonGeometry&)
{
    return false;
}

RectF normalized(const PointGeometry& marker)
{
    return RectF{marker.position.x, marker.position.y, 0.0, 0.0};
}

/// The bounding box, which is NOT the arrow: tail and head are ordered, and
/// only this box-shaped view of them may reorder their coordinates.
RectF normalized(const ArrowGeometry& arrow)
{
    return normalizedCorners(arrow.tail, arrow.head);
}

bool hitTestShape(const PointGeometry& marker, PointF point, double tolerance)
{
    return std::hypot(point.x - marker.position.x, point.y - marker.position.y) <= tolerance;
}

// The shaft, and only the shaft. The head is decoration: it sits at the
// shaft's end and is therefore already within tolerance of it, so hit-testing
// it separately would duplicate an answer this already gives.
//
// distanceToSegment clamps to the segment rather than its infinite line, and
// treats a zero-length segment as its own endpoint -- which is what makes a
// degenerate arrow from disk hit at its point instead of dividing by zero.
bool hitTestShape(const ArrowGeometry& arrow, PointF point, double tolerance)
{
    return distanceToSegment(arrow.tail, arrow.head, point) <= tolerance;
}

AnnotationType typeOfShape(const PointGeometry&)
{
    return AnnotationType::Point;
}

AnnotationType typeOfShape(const ArrowGeometry&)
{
    return AnnotationType::Arrow;
}

// One point, open. measure() reads this and reports length 0 with no area,
// which is the truth about a marker and needs no code of its own.
Outline outlineShape(const PointGeometry& marker)
{
    return Outline{{marker.position}, false};
}

// Tail first, head second: the painter derives the head's direction from these
// two points once it has converted them to screen space, so their order is
// load-bearing.
Outline outlineShape(const ArrowGeometry& arrow)
{
    return Outline{{arrow.tail, arrow.head}, false};
}

AnnotationGeometry translatedShape(const PointGeometry& marker, double dx, double dy)
{
    return PointGeometry{PointF{marker.position.x + dx, marker.position.y + dy}};
}

AnnotationGeometry translatedShape(const ArrowGeometry& arrow, double dx, double dy)
{
    return ArrowGeometry{PointF{arrow.tail.x + dx, arrow.tail.y + dy},
                         PointF{arrow.head.x + dx, arrow.head.y + dy}};
}

// Neither has an area at all. The predicate promises that a TYPE always has a
// well-defined area, and "none" is not a value measure() can stand behind.
bool wellDefinedArea(const PointGeometry&)
{
    return false;
}

bool wellDefinedArea(const ArrowGeometry&)
{
    return false;
}

} // namespace

PointF closestPointOnSegment(PointF a, PointF b, PointF point)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0) {
        return a;
    }
    double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq;
    t = std::max(0.0, std::min(1.0, t));
    return PointF{a.x + t * dx, a.y + t * dy};
}

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

bool hasWellDefinedArea(const AnnotationGeometry& geom)
{
    return std::visit([](const auto& shape) { return wellDefinedArea(shape); }, geom);
}

} // namespace slideio::viewer::core
