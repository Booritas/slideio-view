#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeometry.h"

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
