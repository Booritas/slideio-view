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

// Review Focus 2: a drag that runs right-to-left and bottom-to-top must still
// produce a positive-extent box, or the shape fails its own hit test and can
// never be selected again.
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
