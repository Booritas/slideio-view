#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeometry.h"

#include <cmath>
#include <utility>
#include <variant>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return RectangleGeometry{PointF{x0, y0}, PointF{x1, y1}};
}

AnnotationGeometry ellipseGeom(double x0, double y0, double x1, double y1)
{
    return EllipseGeometry{PointF{x0, y0}, PointF{x1, y1}};
}

AnnotationGeometry pointGeom(double x, double y)
{
    return PointGeometry{PointF{x, y}};
}

AnnotationGeometry arrowGeom(double tailX, double tailY, double headX, double headY)
{
    return ArrowGeometry{PointF{tailX, tailY}, PointF{headX, headY}};
}

} // namespace

TEST_CASE("boundingBox returns the rectangle itself", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(rect(10.0, 20.0, 110.0, 70.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

// A drag that runs right-to-left and bottom-to-top must still produce a
// positive-extent box, or the shape fails its own hit test and can never be
// selected again.
TEST_CASE("boundingBox normalises an inverted rectangle", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(rect(110.0, 70.0, 10.0, 20.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("boundingBox of a degenerate rectangle has zero extent", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(rect(10.0, 20.0, 10.0, 20.0));
    REQUIRE(box.width == 0.0);
    REQUIRE(box.height == 0.0);
}

TEST_CASE("hitTest succeeds inside the shape", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{50.0, 40.0}, 0.0));
}

TEST_CASE("hitTest succeeds on the outline with zero tolerance", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{10.0, 45.0}, 0.0));
}

TEST_CASE("hitTest fails outside the shape", "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{5.0, 45.0}, 0.0));
    // Each of the four edge comparisons must be exercised as failing, or a
    // sign typo in any one of them passes unnoticed.
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{115.0, 45.0}, 0.0));
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{50.0, 15.0}, 0.0));
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{50.0, 75.0}, 0.0));
}

TEST_CASE("hitTest succeeds just outside when tolerance covers the gap",
          "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{5.0, 45.0}, 5.0));
}

TEST_CASE("hitTest fails just beyond the tolerance boundary", "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{4.0, 45.0}, 5.0));
}

TEST_CASE("hitTest treats a negative tolerance as zero", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{50.0, 40.0}, -10.0));
    REQUIRE_FALSE(hitTest(rect(10.0, 20.0, 110.0, 70.0), PointF{5.0, 45.0}, -10.0));
}

TEST_CASE("hitTest works on an inverted rectangle", "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(rect(110.0, 70.0, 10.0, 20.0), PointF{50.0, 40.0}, 0.0));
}

// Foundation follow-up item 12. A zero-extent rectangle is real data, not a
// malformed one: withMinimumExtent exists precisely because integer mouse
// deltas make a short horizontal or vertical drag land with no extent on one
// axis. Such a shape must still be selectable within the click tolerance, or
// the user draws something they can never select again.
TEST_CASE("hitTest respects tolerance on a degenerate rectangle", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry point = rect(10.0, 20.0, 10.0, 20.0);
    REQUIRE(hitTest(point, PointF{10.0, 20.0}, 0.0));
    REQUIRE(hitTest(point, PointF{13.0, 20.0}, 5.0));
    REQUIRE(hitTest(point, PointF{10.0, 24.0}, 5.0));
    REQUIRE_FALSE(hitTest(point, PointF{20.0, 20.0}, 5.0));

    // Zero extent on one axis only -- the common case, from a short drag.
    const AnnotationGeometry line = rect(10.0, 20.0, 110.0, 20.0);
    REQUIRE(hitTest(line, PointF{50.0, 23.0}, 5.0));
    REQUIRE_FALSE(hitTest(line, PointF{50.0, 27.0}, 5.0));
}

TEST_CASE("typeOf reports a rectangle", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(rect(0.0, 0.0, 10.0, 10.0)) == AnnotationType::Rectangle);
}

TEST_CASE("outline of a rectangle is four corners, closed", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(rect(10.0, 20.0, 110.0, 70.0));
    REQUIRE(shape.closed);
    REQUIRE(shape.points.size() == 4);
    REQUIRE(shape.points[0].x == 10.0);
    REQUIRE(shape.points[0].y == 20.0);
    REQUIRE(shape.points[1].x == 110.0);
    REQUIRE(shape.points[1].y == 20.0);
    REQUIRE(shape.points[2].x == 110.0);
    REQUIRE(shape.points[2].y == 70.0);
    REQUIRE(shape.points[3].x == 10.0);
    REQUIRE(shape.points[3].y == 70.0);
}

// An inverted drag is normalised by boundingBox; the outline must agree with
// it, or a shape would be painted somewhere other than where it hit-tests.
TEST_CASE("outline normalises an inverted rectangle", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(rect(110.0, 70.0, 10.0, 20.0));
    REQUIRE(shape.points[0].x == 10.0);
    REQUIRE(shape.points[0].y == 20.0);
    REQUIRE(shape.points[2].x == 110.0);
    REQUIRE(shape.points[2].y == 70.0);
}

TEST_CASE("translated moves a rectangle without resizing it", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(translated(rect(10.0, 20.0, 110.0, 70.0), 5.0, -3.0));
    REQUIRE(box.x == 15.0);
    REQUIRE(box.y == 17.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("translated by zero is the identity", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(translated(rect(10.0, 20.0, 110.0, 70.0), 0.0, 0.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    // The extent too: asserting the origin alone would pass for a translate
    // that moved one corner and left the other, which is the shape of the
    // bug an identity test exists to catch.
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

namespace
{

AnnotationGeometry polygon(std::vector<PointF> vertices)
{
    return PolygonGeometry{std::move(vertices), true};
}

AnnotationGeometry polyline(std::vector<PointF> vertices)
{
    return PolygonGeometry{std::move(vertices), false};
}

// A square with a deep notch cut into its right edge. The notch lies inside
// the bounding box and outside the shape.
AnnotationGeometry notchedSquare()
{
    return polygon({PointF{0.0, 0.0}, PointF{100.0, 0.0}, PointF{50.0, 50.0},
                    PointF{100.0, 100.0}, PointF{0.0, 100.0}});
}

} // namespace

TEST_CASE("typeOf distinguishes a polygon from a polyline", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(polygon({PointF{0, 0}, PointF{10, 0}, PointF{0, 10}}))
            == AnnotationType::Polygon);
    REQUIRE(typeOf(polyline({PointF{0, 0}, PointF{10, 10}})) == AnnotationType::Polyline);
}

TEST_CASE("boundingBox spans every vertex", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(polygon({PointF{30.0, 10.0}, PointF{-5.0, 70.0},
                                           PointF{90.0, 40.0}}));
    REQUIRE(box.x == -5.0);
    REQUIRE(box.y == 10.0);
    REQUIRE(box.width == 95.0);
    REQUIRE(box.height == 60.0);
}

// Review Focus 2. This is what distinguishes real point-in-polygon from the
// bounding-box approximation the whole increment exists to remove: (75, 50)
// is squarely inside the bounding box and squarely outside the shape.
TEST_CASE("hitTest misses a point in the notch of a concave polygon",
          "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hitTest(notchedSquare(), PointF{75.0, 50.0}, 0.0));
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 50.0}, 0.0));
}

// Review Focus 1. A ray cast through a vertex is double-counted by a naive
// implementation, which then reports "outside" for a point plainly inside.
// Every vertex of the notched square sits on y = 0, 50 or 100, so these probe
// the three vertex rows directly.
TEST_CASE("hitTest is correct for a ray passing exactly through vertices",
          "[core][AnnotationGeometry]")
{
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 50.0}, 0.0));
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 0.0}, 0.0));
    REQUIRE(hitTest(notchedSquare(), PointF{20.0, 100.0}, 0.0));
    REQUIRE_FALSE(hitTest(notchedSquare(), PointF{120.0, 50.0}, 0.0));
    REQUIRE_FALSE(hitTest(notchedSquare(), PointF{120.0, 0.0}, 0.0));
}

// Self-intersection is valid data -- pathologists produce it by accident when
// tracing quickly. Even-odd means the centre of a bowtie is outside.
TEST_CASE("hitTest applies the even-odd rule to a self-intersecting polygon",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry bowtie = polygon({PointF{0.0, 0.0}, PointF{100.0, 100.0},
                                               PointF{100.0, 0.0}, PointF{0.0, 100.0}});
    REQUIRE(hitTest(bowtie, PointF{10.0, 50.0}, 0.0));
    REQUIRE_FALSE(hitTest(bowtie, PointF{50.0, 20.0}, 0.0));
}

TEST_CASE("hitTest finds a point near an edge within tolerance",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry line = polyline({PointF{0.0, 0.0}, PointF{100.0, 0.0}});
    REQUIRE(hitTest(line, PointF{50.0, 3.0}, 5.0));
    REQUIRE_FALSE(hitTest(line, PointF{50.0, 9.0}, 5.0));
}

// A polyline has no interior, however tempting its shape looks.
TEST_CASE("hitTest never reports an interior hit for an open path",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry open = polyline({PointF{0.0, 0.0}, PointF{100.0, 0.0},
                                              PointF{100.0, 100.0}, PointF{0.0, 100.0}});
    REQUIRE_FALSE(hitTest(open, PointF{50.0, 50.0}, 1.0));
    REQUIRE(hitTest(open, PointF{50.0, 0.0}, 1.0));
}

// An open path must not be hit along the segment that would close it.
TEST_CASE("hitTest ignores the closing segment of an open path",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry open = polyline({PointF{0.0, 0.0}, PointF{100.0, 0.0},
                                              PointF{100.0, 100.0}});
    REQUIRE_FALSE(hitTest(open, PointF{50.0, 50.0}, 1.0));
}

// Review Focus 3. Degenerate shapes must not crash and must stay selectable
// by edge proximity -- a zero-length segment divides by zero if the distance
// maths is careless.
TEST_CASE("hitTest handles a polygon whose vertices are all identical",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry dot = polygon({PointF{10.0, 10.0}, PointF{10.0, 10.0},
                                            PointF{10.0, 10.0}});
    REQUIRE(hitTest(dot, PointF{12.0, 10.0}, 5.0));
    REQUIRE_FALSE(hitTest(dot, PointF{40.0, 10.0}, 5.0));
    const RectF box = boundingBox(dot);
    REQUIRE(box.width == 0.0);
    REQUIRE(box.height == 0.0);
}

TEST_CASE("hitTest handles three collinear vertices", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry flat = polygon({PointF{0.0, 0.0}, PointF{50.0, 0.0},
                                             PointF{100.0, 0.0}});
    REQUIRE(hitTest(flat, PointF{50.0, 2.0}, 5.0));
    REQUIRE_FALSE(hitTest(flat, PointF{50.0, 20.0}, 5.0));
}

TEST_CASE("outline of a polygon is its vertices", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(polygon({PointF{1.0, 2.0}, PointF{3.0, 4.0},
                                           PointF{5.0, 6.0}}));
    REQUIRE(shape.closed);
    REQUIRE(shape.points.size() == 3);
    REQUIRE(shape.points[1].x == 3.0);
    REQUIRE(shape.points[1].y == 4.0);
}

TEST_CASE("outline of a polyline is open", "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(outline(polyline({PointF{1.0, 2.0}, PointF{3.0, 4.0}})).closed);
}

// The regression foundation item 1 was filed to prevent: a dragged polygon
// must stay a polygon with the same vertex count, not become its bounding box.
TEST_CASE("translated moves every vertex and preserves the shape",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved =
        translated(polygon({PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{0.0, 10.0}}), 5.0, -2.0);
    REQUIRE(typeOf(moved) == AnnotationType::Polygon);
    const PolygonGeometry& shape = std::get<PolygonGeometry>(moved);
    REQUIRE(shape.vertices.size() == 3);
    REQUIRE(shape.closed);
    REQUIRE(shape.vertices[0].x == 5.0);
    REQUIRE(shape.vertices[0].y == -2.0);
    REQUIRE(shape.vertices[2].x == 5.0);
    REQUIRE(shape.vertices[2].y == 8.0);
}

TEST_CASE("translated preserves an open path's openness", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved =
        translated(polyline({PointF{0.0, 0.0}, PointF{10.0, 0.0}}), 1.0, 1.0);
    REQUIRE_FALSE(std::get<PolygonGeometry>(moved).closed);
    REQUIRE(typeOf(moved) == AnnotationType::Polyline);
}

TEST_CASE("an ellipse outline has the fixed vertex count and is closed",
          "[core][AnnotationGeometry]")
{
    const Outline shape = outline(ellipseGeom(0.0, 0.0, 100.0, 50.0));
    REQUIRE(shape.points.size() == kEllipseOutlineVertices);
    REQUIRE(shape.closed);
}

// The first vertex is the rightmost point of the curve -- theta = 0. Fixing
// this pins the parameterisation, so a later change to the loop that happened
// to preserve the vertex count would still be caught.
TEST_CASE("an ellipse outline starts at the rightmost point", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(ellipseGeom(0.0, 0.0, 100.0, 50.0));
    REQUIRE(std::abs(shape.points.front().x - 100.0) < 1e-9);
    REQUIRE(std::abs(shape.points.front().y - 25.0) < 1e-9);
}

// Every vertex lies ON the curve. The polygon is inscribed, which is what makes
// the measured area slightly low in a known, documented direction (spec 3.2).
TEST_CASE("every ellipse outline vertex satisfies the ellipse equation",
          "[core][AnnotationGeometry]")
{
    const Outline shape = outline(ellipseGeom(10.0, 20.0, 110.0, 70.0));
    const double cx = 60.0;
    const double cy = 45.0;
    const double a = 50.0;
    const double b = 25.0;
    for (const PointF& point : shape.points) {
        const double nx = (point.x - cx) / a;
        const double ny = (point.y - cy) / b;
        REQUIRE(std::abs(nx * nx + ny * ny - 1.0) < 1e-9);
    }
}

TEST_CASE("boundingBox normalises an inverted ellipse drag", "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(ellipseGeom(110.0, 70.0, 10.0, 20.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

// Review Focus 5: a file may hold either winding. Both must describe one shape.
TEST_CASE("an inverted ellipse has the same outline as the normalised one",
          "[core][AnnotationGeometry]")
{
    const Outline normal = outline(ellipseGeom(10.0, 20.0, 110.0, 70.0));
    const Outline inverted = outline(ellipseGeom(110.0, 70.0, 10.0, 20.0));
    REQUIRE(normal.points.size() == inverted.points.size());
    for (std::size_t i = 0; i < normal.points.size(); ++i) {
        REQUIRE(std::abs(normal.points[i].x - inverted.points[i].x) < 1e-9);
        REQUIRE(std::abs(normal.points[i].y - inverted.points[i].y) < 1e-9);
    }
}

TEST_CASE("typeOf reports an ellipse", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(ellipseGeom(0.0, 0.0, 10.0, 20.0)) == AnnotationType::Ellipse);
}

TEST_CASE("translating an ellipse moves it and preserves its extents",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved = translated(ellipseGeom(10.0, 20.0, 110.0, 70.0), 5.0, -7.0);
    const RectF box = boundingBox(moved);
    REQUIRE(box.x == 15.0);
    REQUIRE(box.y == 13.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
    REQUIRE(typeOf(moved) == AnnotationType::Ellipse);
}

// The whole point of the shape: the corner of the bounding box is outside the
// curve. A rectangle of the same box would be hit here.
TEST_CASE("an ellipse is not hit in the corner of its bounding box",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry shape = ellipseGeom(0.0, 0.0, 100.0, 50.0);
    REQUIRE(hitTest(shape, PointF{50.0, 25.0}, 0.0));
    REQUIRE_FALSE(hitTest(shape, PointF{0.0, 0.0}, 0.0));
    REQUIRE(hitTest(shape, PointF{100.0, 25.0}, 0.0));
}

TEST_CASE("a point just outside an ellipse is hit within the tolerance band",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry shape = ellipseGeom(0.0, 0.0, 100.0, 50.0);
    REQUIRE_FALSE(hitTest(shape, PointF{103.0, 25.0}, 0.0));
    REQUIRE(hitTest(shape, PointF{103.0, 25.0}, 4.0));
}

// The header promises a negative tolerance is treated as zero. An unclamped
// one would shrink the shape instead of growing it.
TEST_CASE("a negative tolerance on an ellipse behaves as zero",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry shape = ellipseGeom(0.0, 0.0, 100.0, 50.0);
    REQUIRE(hitTest(shape, PointF{50.0, 25.0}, -10.0));
    REQUIRE_FALSE(hitTest(shape, PointF{0.0, 0.0}, -10.0));
}

// Review Focus 1: a hand-edited file may hold one. It must still be clickable
// rather than becoming an invisible, unselectable annotation.
TEST_CASE("a degenerate ellipse is still hit along its line", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry flat = ellipseGeom(0.0, 25.0, 100.0, 25.0);
    REQUIRE(hitTest(flat, PointF{50.0, 25.0}, 0.5));
}

TEST_CASE("a well-defined area is known per shape type", "[core][AnnotationGeometry]")
{
    REQUIRE(hasWellDefinedArea(ellipseGeom(0.0, 0.0, 100.0, 50.0)));
    REQUIRE(hasWellDefinedArea(rect(0.0, 0.0, 100.0, 50.0)));
    // A polygon's vertices are the user's; a trace that crosses itself has no
    // area anyone can stand behind, and nothing can promise it does not.
    REQUIRE_FALSE(hasWellDefinedArea(
        PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{10.0, 10.0}}, true}));
}

// The degenerate cases are the whole reason this is not called "convex" or
// "cannot self-intersect": both would be FALSE here, while the area -- zero --
// is perfectly well defined. The parser accepts these from disk on purpose.
TEST_CASE("a degenerate shape still has a well-defined area",
          "[core][AnnotationGeometry]")
{
    REQUIRE(hasWellDefinedArea(ellipseGeom(0.0, 25.0, 100.0, 25.0)));
    REQUIRE(hasWellDefinedArea(rect(0.0, 25.0, 100.0, 25.0)));
}

TEST_CASE("a point's bounding box has zero extent at its position",
          "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(pointGeom(120.0, 340.0));
    REQUIRE(box.x == 120.0);
    REQUIRE(box.y == 340.0);
    REQUIRE(box.width == 0.0);
    REQUIRE(box.height == 0.0);
}

// The pair is ORDERED -- head and tail are not corners -- but the bounding box
// is a box, so it normalises like any other.
TEST_CASE("an arrow's bounding box normalises its endpoints",
          "[core][AnnotationGeometry]")
{
    const RectF box = boundingBox(arrowGeom(110.0, 70.0, 10.0, 20.0));
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("a point is hit within the tolerance of its position and not beyond",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry marker = pointGeom(100.0, 100.0);
    REQUIRE(hitTest(marker, PointF{100.0, 100.0}, 0.0));
    REQUIRE(hitTest(marker, PointF{103.0, 104.0}, 5.0));
    REQUIRE_FALSE(hitTest(marker, PointF{103.0, 104.0}, 4.9));
}

TEST_CASE("an arrow is hit along its shaft and not off it",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry arrow = arrowGeom(0.0, 0.0, 100.0, 0.0);
    REQUIRE(hitTest(arrow, PointF{50.0, 0.0}, 0.0));
    REQUIRE(hitTest(arrow, PointF{50.0, 2.0}, 2.0));
    REQUIRE_FALSE(hitTest(arrow, PointF{50.0, 10.0}, 2.0));
    // Past the head, not on the infinite line through it.
    REQUIRE_FALSE(hitTest(arrow, PointF{140.0, 0.0}, 2.0));
}

// Review Focus 1: the tool refuses to create this, but the parser accepts it
// from disk, so every dispatch has to answer for it.
TEST_CASE("a zero-length arrow is hit at its own point",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry arrow = arrowGeom(40.0, 40.0, 40.0, 40.0);
    REQUIRE(hitTest(arrow, PointF{40.0, 40.0}, 0.0));
    REQUIRE_FALSE(hitTest(arrow, PointF{60.0, 40.0}, 1.0));
}

TEST_CASE("typeOf names the two new shapes", "[core][AnnotationGeometry]")
{
    REQUIRE(typeOf(pointGeom(1.0, 2.0)) == AnnotationType::Point);
    REQUIRE(typeOf(arrowGeom(1.0, 2.0, 3.0, 4.0)) == AnnotationType::Arrow);
}

TEST_CASE("a point's outline is one open point", "[core][AnnotationGeometry]")
{
    const Outline shape = outline(pointGeom(7.0, 9.0));
    REQUIRE(shape.points.size() == 1);
    REQUIRE(shape.points[0].x == 7.0);
    REQUIRE(shape.points[0].y == 9.0);
    REQUIRE_FALSE(shape.closed);
}

// Tail first, head second, in the stored order. The painter reads the shaft
// direction from these two points, so reversing them would reverse the head.
TEST_CASE("an arrow's outline is tail then head, open",
          "[core][AnnotationGeometry]")
{
    const Outline shape = outline(arrowGeom(10.0, 20.0, 90.0, 60.0));
    REQUIRE(shape.points.size() == 2);
    REQUIRE(shape.points[0].x == 10.0);
    REQUIRE(shape.points[0].y == 20.0);
    REQUIRE(shape.points[1].x == 90.0);
    REQUIRE(shape.points[1].y == 60.0);
    REQUIRE_FALSE(shape.closed);
}

TEST_CASE("translating a point moves its position", "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved = translated(pointGeom(10.0, 20.0), 5.0, -3.0);
    REQUIRE(typeOf(moved) == AnnotationType::Point);
    const PointGeometry& marker = std::get<PointGeometry>(moved);
    REQUIRE(marker.position.x == 15.0);
    REQUIRE(marker.position.y == 17.0);
}

// Both endpoints move by the same delta, and the ORDER survives: a translated
// arrow that came back reversed would point the wrong way after every drag.
TEST_CASE("translating an arrow moves both endpoints and keeps their order",
          "[core][AnnotationGeometry]")
{
    const AnnotationGeometry moved = translated(arrowGeom(10.0, 20.0, 90.0, 60.0), 5.0, -3.0);
    REQUIRE(typeOf(moved) == AnnotationType::Arrow);
    const ArrowGeometry& arrow = std::get<ArrowGeometry>(moved);
    REQUIRE(arrow.tail.x == 15.0);
    REQUIRE(arrow.tail.y == 17.0);
    REQUIRE(arrow.head.x == 95.0);
    REQUIRE(arrow.head.y == 57.0);
}

// Neither has an area at all, so neither type can promise one. The predicate
// is about the TYPE, not the instance -- see the header.
TEST_CASE("neither a point nor an arrow has a well-defined area",
          "[core][AnnotationGeometry]")
{
    REQUIRE_FALSE(hasWellDefinedArea(pointGeom(1.0, 2.0)));
    REQUIRE_FALSE(hasWellDefinedArea(arrowGeom(1.0, 2.0, 3.0, 4.0)));
}

TEST_CASE("a text is a point of anchor with no extent", "[core][AnnotationGeometry][Text]")
{
    const AnnotationGeometry text = TextGeometry{PointF{5.0, 6.0}};
    REQUIRE(typeOf(text) == AnnotationType::Text);

    const Outline shape = outline(text);
    REQUIRE(shape.points.size() == 1);
    CHECK(shape.points[0].x == 5.0);
    CHECK(shape.points[0].y == 6.0);
    CHECK_FALSE(shape.closed);

    const RectF box = boundingBox(text);
    CHECK(box.x == 5.0);
    CHECK(box.y == 6.0);
    CHECK(box.width == 0.0);
    CHECK(box.height == 0.0);

    const AnnotationGeometry moved = translated(text, 2.0, 3.0);
    REQUIRE(std::holds_alternative<TextGeometry>(moved));
    CHECK(std::get<TextGeometry>(moved).anchor.x == 7.0);
    CHECK(std::get<TextGeometry>(moved).anchor.y == 9.0);

    CHECK_FALSE(hasWellDefinedArea(text));
    CHECK(hitTest(text, PointF{5.5, 6.0}, 1.0));
    CHECK_FALSE(hitTest(text, PointF{8.0, 6.0}, 1.0));
}
