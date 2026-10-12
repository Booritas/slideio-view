#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeometry.h"

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
