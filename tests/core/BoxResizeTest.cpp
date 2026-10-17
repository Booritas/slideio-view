#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/BoxResize.h"

#include <cmath>
#include <variant>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

// x=10, y=20, width=100, height=40. Left 10, right 110, top 20, bottom 60,
// midX 60, midY 40.
RectF box()
{
    return RectF{10.0, 20.0, 100.0, 40.0};
}

std::size_t indexOf(BoxHandle handle)
{
    return static_cast<std::size_t>(handle);
}

} // namespace

TEST_CASE("boxHandlePositions puts all eight handles on the box", "[core][BoxResize]")
{
    const std::array<PointF, 8> handles = boxHandlePositions(box());

    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::Top)].x == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Top)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::TopRight)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::TopRight)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::Right)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::Right)].y == 40.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Bottom)].x == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Bottom)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomLeft)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomLeft)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Left)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::Left)].y == 40.0);
}

// Review Focus 1. A stored box can be inverted -- a file can hold one, and
// 6.4 produces one on every mid-drag flip. The caller is required to pass the
// NORMALISED box, so this pins that core::boundingBox and boxHandlePositions
// compose correctly for an inverted stored pair: the handles must land on the
// shape, not mirrored off it.
//
// Note this asserts against the SAME expected coordinates as the normalised
// case above. Comparing the two results to each other would pass whatever
// either produced.
TEST_CASE("an inverted stored box normalises to the same handles",
          "[core][BoxResize]")
{
    // The same rectangle as box(), stored bottom-right to top-left.
    const AnnotationGeometry inverted =
        RectangleGeometry{PointF{110.0, 60.0}, PointF{10.0, 20.0}};
    const std::array<PointF, 8> handles = boxHandlePositions(boundingBox(inverted));

    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].x == 10.0);
    REQUIRE(handles[indexOf(BoxHandle::TopLeft)].y == 20.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].x == 110.0);
    REQUIRE(handles[indexOf(BoxHandle::BottomRight)].y == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Top)].x == 60.0);
    REQUIRE(handles[indexOf(BoxHandle::Left)].y == 40.0);
}

TEST_CASE("nearestBoxHandle finds a handle in tolerance", "[core][BoxResize]")
{
    const auto handle = nearestBoxHandle(box(), PointF{110.4, 60.0}, 1.0);
    REQUIRE(handle.has_value());
    REQUIRE(*handle == BoxHandle::BottomRight);
}

TEST_CASE("nearestBoxHandle is empty away from every handle", "[core][BoxResize]")
{
    REQUIRE_FALSE(nearestBoxHandle(box(), PointF{60.0, 40.0}, 1.0).has_value());
}

// Review Focus 2. On a small shape several handles are in range at once.
TEST_CASE("nearestBoxHandle returns the nearest of several in range",
          "[core][BoxResize]")
{
    const RectF small{0.0, 0.0, 4.0, 4.0};
    // Nearer TopRight (4,0) than TopLeft (0,0) or Top (2,0).
    const auto handle = nearestBoxHandle(small, PointF{3.6, 0.0}, 100.0);
    REQUIRE(handle.has_value());
    REQUIRE(*handle == BoxHandle::TopRight);
}

TEST_CASE("a negative tolerance behaves as zero", "[core][BoxResize]")
{
    REQUIRE(nearestBoxHandle(box(), PointF{10.0, 20.0}, -5.0).has_value());
    REQUIRE_FALSE(nearestBoxHandle(box(), PointF{10.5, 20.0}, -5.0).has_value());
}

// Review Focus 4. A hand-edited file can hold one. Several handles coincide
// and nothing may produce NaN or index out of range.
TEST_CASE("a degenerate box still answers", "[core][BoxResize]")
{
    const RectF flat{10.0, 20.0, 100.0, 0.0};
    const std::array<PointF, 8> handles = boxHandlePositions(flat);
    for (const PointF& handle : handles) {
        REQUIRE(std::isfinite(handle.x));
        REQUIRE(std::isfinite(handle.y));
        REQUIRE(handle.y == 20.0);
    }
    REQUIRE(nearestBoxHandle(flat, PointF{10.0, 20.0}, 1.0).has_value());

    const RectF point{5.0, 5.0, 0.0, 0.0};
    const auto onPoint = nearestBoxHandle(point, PointF{5.0, 5.0}, 1.0);
    REQUIRE(onPoint.has_value());
    // All eight coincide; the tie goes to the earliest enumerator.
    REQUIRE(*onPoint == BoxHandle::TopLeft);
}
