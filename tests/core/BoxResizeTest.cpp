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

TEST_CASE("a corner handle moves two edges and leaves the opposite two",
          "[core][BoxResize]")
{
    const RectF moved = withBoxHandleAt(box(), BoxHandle::TopLeft, PointF{0.0, 0.0});
    REQUIRE(moved.x == 0.0);
    REQUIRE(moved.y == 0.0);
    // Right stayed at 110, bottom at 60.
    REQUIRE(moved.x + moved.width == 110.0);
    REQUIRE(moved.y + moved.height == 60.0);
}

TEST_CASE("every corner handle moves its own two edges", "[core][BoxResize]")
{
    const RectF topRight = withBoxHandleAt(box(), BoxHandle::TopRight, PointF{200.0, 0.0});
    REQUIRE(topRight.x == 10.0);
    REQUIRE(topRight.x + topRight.width == 200.0);
    REQUIRE(topRight.y == 0.0);
    REQUIRE(topRight.y + topRight.height == 60.0);

    const RectF bottomRight =
        withBoxHandleAt(box(), BoxHandle::BottomRight, PointF{200.0, 200.0});
    REQUIRE(bottomRight.x == 10.0);
    REQUIRE(bottomRight.y == 20.0);
    REQUIRE(bottomRight.x + bottomRight.width == 200.0);
    REQUIRE(bottomRight.y + bottomRight.height == 200.0);

    const RectF bottomLeft = withBoxHandleAt(box(), BoxHandle::BottomLeft, PointF{0.0, 200.0});
    REQUIRE(bottomLeft.x == 0.0);
    REQUIRE(bottomLeft.y == 20.0);
    REQUIRE(bottomLeft.x + bottomLeft.width == 110.0);
    REQUIRE(bottomLeft.y + bottomLeft.height == 200.0);
}

TEST_CASE("an edge handle moves one edge and leaves the other three",
          "[core][BoxResize]")
{
    const RectF top = withBoxHandleAt(box(), BoxHandle::Top, PointF{999.0, 0.0});
    // The x of the press is ignored: a top handle owns the top edge only.
    REQUIRE(top.x == 10.0);
    REQUIRE(top.x + top.width == 110.0);
    REQUIRE(top.y == 0.0);
    REQUIRE(top.y + top.height == 60.0);

    const RectF right = withBoxHandleAt(box(), BoxHandle::Right, PointF{200.0, 999.0});
    REQUIRE(right.y == 20.0);
    REQUIRE(right.y + right.height == 60.0);
    REQUIRE(right.x == 10.0);
    REQUIRE(right.x + right.width == 200.0);

    const RectF bottom = withBoxHandleAt(box(), BoxHandle::Bottom, PointF{999.0, 200.0});
    REQUIRE(bottom.y == 20.0);
    REQUIRE(bottom.y + bottom.height == 200.0);

    const RectF left = withBoxHandleAt(box(), BoxHandle::Left, PointF{0.0, 999.0});
    REQUIRE(left.x == 0.0);
    REQUIRE(left.x + left.width == 110.0);
}

// Review Focus 3, and spec 6.4. Dragging past the opposite edge is allowed and
// the intermediate RectF carries a NEGATIVE extent. It must not be clamped or
// absolute-valued here: the geometry built from it is an inverted pair, which
// core::boundingBox normalises.
TEST_CASE("a handle dragged past its opposite edge gives a negative extent",
          "[core][BoxResize]")
{
    const RectF flipped = withBoxHandleAt(box(), BoxHandle::Left, PointF{200.0, 0.0});
    REQUIRE(flipped.x == 200.0);
    REQUIRE(flipped.width == -90.0);
    // The right edge did not move.
    REQUIRE(flipped.x + flipped.width == 110.0);
}

TEST_CASE("withBoxReplaced keeps a rectangle a rectangle", "[core][BoxResize]")
{
    const AnnotationGeometry original =
        RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}};
    const auto replaced = withBoxReplaced(original, RectF{10.0, 20.0, 100.0, 40.0});

    REQUIRE(replaced.has_value());
    const auto* rectangle = std::get_if<RectangleGeometry>(&*replaced);
    REQUIRE(rectangle != nullptr);
    REQUIRE(rectangle->topLeft.x == 10.0);
    REQUIRE(rectangle->topLeft.y == 20.0);
    REQUIRE(rectangle->bottomRight.x == 110.0);
    REQUIRE(rectangle->bottomRight.y == 60.0);
}

TEST_CASE("withBoxReplaced keeps an ellipse an ellipse", "[core][BoxResize]")
{
    const AnnotationGeometry original = EllipseGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}};
    const auto replaced = withBoxReplaced(original, RectF{10.0, 20.0, 100.0, 40.0});

    REQUIRE(replaced.has_value());
    REQUIRE(std::get_if<EllipseGeometry>(&*replaced) != nullptr);
    REQUIRE(typeOf(*replaced) == AnnotationType::Ellipse);
}

// Review Focus 5. A traced shape is not box-defined, and scaling 400 vertices
// by a box drag is a different feature (spec 8).
TEST_CASE("withBoxReplaced refuses a polygon", "[core][BoxResize]")
{
    const AnnotationGeometry polygon = PolygonGeometry{
        {PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 10.0}}, true};
    REQUIRE_FALSE(withBoxReplaced(polygon, RectF{0.0, 0.0, 1.0, 1.0}).has_value());
}

// Review Focus 3, the other half: the negative extent must survive into the
// stored geometry as an inverted pair, and boundingBox must normalise it back.
TEST_CASE("a flipped box becomes an inverted pair that boundingBox normalises",
          "[core][BoxResize]")
{
    const AnnotationGeometry original =
        RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}};
    const RectF flipped = withBoxHandleAt(box(), BoxHandle::Left, PointF{200.0, 0.0});
    const auto replaced = withBoxReplaced(original, flipped);

    REQUIRE(replaced.has_value());
    const auto* rectangle = std::get_if<RectangleGeometry>(&*replaced);
    REQUIRE(rectangle != nullptr);
    REQUIRE(rectangle->topLeft.x == 200.0);
    REQUIRE(rectangle->bottomRight.x == 110.0);

    const RectF normalised = boundingBox(*replaced);
    REQUIRE(normalised.x == 110.0);
    REQUIRE(normalised.width == 90.0);
}

// Spec 7.1: "A resized rectangle and a resized ellipse each round-trip through
// the serializer unchanged." The resize can only build shapes the format
// accepts, and this is what holds it to that. Both types, because they are
// separate branches of withBoxReplaced and a wrong one would silently change
// an ellipse into a rectangle on disk.
TEST_CASE("a resized rectangle and ellipse round-trip through the serializer",
          "[core][BoxResize]")
{
    const RectF resized = withBoxHandleAt(box(), BoxHandle::BottomRight, PointF{200.0, 200.0});

    const std::vector<AnnotationGeometry> starts{
        RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
        EllipseGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}},
    };

    for (const AnnotationGeometry& start : starts) {
        const auto replaced = withBoxReplaced(start, resized);
        REQUIRE(replaced.has_value());

        AnnotationDocument document;
        document.slideId = "a3f8c2e1b4d50697";
        document.sceneIndex = 0;
        document.annotations.emplace_back("b1", *replaced);

        const ParseResult parsed = parseAnnotationDocument(serializeAnnotationDocument(document));
        REQUIRE(parsed.error == ParseError::None);
        REQUIRE(parsed.document.annotations.size() == 1);

        const AnnotationGeometry& readBack = parsed.document.annotations.front().geometry();
        REQUIRE(typeOf(readBack) == typeOf(*replaced));

        const RectF before = boundingBox(*replaced);
        const RectF after = boundingBox(readBack);
        REQUIRE(before.x == after.x);
        REQUIRE(before.y == after.y);
        REQUIRE(before.width == after.width);
        REQUIRE(before.height == after.height);
    }
}
