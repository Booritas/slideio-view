#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationInteraction.h"

using namespace slideio::viewer::ui;
namespace core = slideio::viewer::core;

TEST_CASE("middle button always pans, whatever the tool", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, true) == DragOwner::Pan);
}

TEST_CASE("held space pans, whatever the tool", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Rectangle, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Pan, true) == DragOwner::Pan);
    // The off-diagonal cell: a drawing tool pressed ON an annotation with
    // space held still pans. Without this, "don't pan if a drawing tool is
    // over an annotation" could be added and every test would still pass.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Rectangle, true) == DragOwner::Pan);
}

TEST_CASE("left drag on empty space with the pan tool pans",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, false) == DragOwner::Pan);
}

TEST_CASE("left drag on an annotation with the pan tool moves it",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, true) == DragOwner::MoveAnnotation);
}

TEST_CASE("left drag with a drawing tool belongs to the tool",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, false) == DragOwner::Tool);
    // Pressing on an existing annotation does not steal the drag from a
    // drawing tool: the user asked to draw.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, true) == DragOwner::Tool);
}

TEST_CASE("the right button owns no drag", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::RightButton, Qt::NoModifier, false,
                             AnnotationTool::Pan, false) == DragOwner::None);
    REQUIRE(resolveDragOwner(Qt::RightButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, true) == DragOwner::None);
    // Space must not promote a non-left button into a pan: the button
    // rejection has to come first.
    REQUIRE(resolveDragOwner(Qt::RightButton, Qt::NoModifier, true,
                             AnnotationTool::Pan, false) == DragOwner::None);
    // Right is not the only non-left button. Narrowing the rejection to
    // `button == Qt::RightButton` would let Back/Forward/NoButton fall
    // through into the Pan or Tool branches.
    REQUIRE(resolveDragOwner(Qt::BackButton, Qt::NoModifier, false,
                             AnnotationTool::Rectangle, false) == DragOwner::None);
}

TEST_CASE("tolerance converts from screen pixels to slide units",
          "[ui][AnnotationInteraction]")
{
    // At 2x, 6 screen pixels is 3 slide pixels.
    REQUIRE(screenToleranceToSlide(6.0, 2.0) == 3.0);
    // At 0.5x, the same 6 screen pixels covers 12 slide pixels.
    REQUIRE(screenToleranceToSlide(6.0, 0.5) == 12.0);
}

// Dividing by a non-positive scale yields an infinite tolerance, which makes
// every hit test succeed -- one click would select an annotation anywhere on
// the slide.
TEST_CASE("a non-positive scale yields zero tolerance, never infinity",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(screenToleranceToSlide(6.0, 0.0) == 0.0);
    REQUIRE(screenToleranceToSlide(6.0, -1.0) == 0.0);
}

TEST_CASE("the hit tolerance constant is a usable click target",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(kHitToleranceScreenPixels > 0.0);
    REQUIRE(kHitToleranceScreenPixels <= 12.0);
}

TEST_CASE("a tiny drag is rejected as a misclick", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 2, 2}, 1.0, 6.0));
    // The same slide-space drag at high zoom is large on screen and is accepted.
    REQUIRE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 2, 2}, 4.0, 6.0));
}

TEST_CASE("a long thin drag is accepted", "[ui][AnnotationInteraction]")
{
    REQUIRE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 300, 2}, 1.0, 6.0));
    REQUIRE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 2, 300}, 1.0, 6.0));
}

TEST_CASE("a long drag with zero extent on one axis is still accepted",
          "[ui][AnnotationInteraction]")
{
    // Mouse deltas are integers, so a deliberate sideways drag often has dy == 0.
    // Rejecting it would leave the user with no shape AND no selection change --
    // the dead-UI case this gate exists to prevent. withMinimumExtent widens the
    // degenerate axis afterwards so the stored geometry is never zero-area.
    REQUIRE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 300, 0}, 1.0, 6.0));
    REQUIRE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 0, 300}, 1.0, 6.0));
}

TEST_CASE("a click with no extent on either axis is rejected", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 0, 0}, 1.0, 6.0));
}

TEST_CASE("withMinimumExtent widens only the degenerate axis", "[ui][AnnotationInteraction]")
{
    const auto flat = withMinimumExtent(slideio::viewer::core::RectF{10, 20, 300, 0}, 0.5);
    REQUIRE(flat.x == 10.0);
    REQUIRE(flat.y == 20.0);
    REQUIRE(flat.width == 300.0);
    REQUIRE(flat.height == 0.5);

    const auto upright = withMinimumExtent(slideio::viewer::core::RectF{10, 20, 0, 300}, 0.5);
    REQUIRE(upright.width == 0.5);
    REQUIRE(upright.height == 300.0);
}

TEST_CASE("withMinimumExtent leaves a healthy box alone", "[ui][AnnotationInteraction]")
{
    const auto box = withMinimumExtent(slideio::viewer::core::RectF{10, 20, 300, 40}, 0.5);
    REQUIRE(box.width == 300.0);
    REQUIRE(box.height == 40.0);
}

TEST_CASE("withMinimumExtent treats a negative minimum as zero",
          "[ui][AnnotationInteraction]")
{
    const auto box = withMinimumExtent(slideio::viewer::core::RectF{0, 0, 300, 0}, -5.0);
    REQUIRE(box.height == 0.0);
}

TEST_CASE("a non-positive scale rejects every drag", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 300, 300}, 0.0, 6.0));
    REQUIRE_FALSE(dragExceedsMinimumSize(slideio::viewer::core::RectF{0, 0, 300, 300}, -1.0, 6.0));
}

TEST_CASE("isNPointTool identifies the multi-click tools", "[ui][AnnotationInteraction]")
{
    REQUIRE(isNPointTool(AnnotationTool::Polygon));
    REQUIRE(isNPointTool(AnnotationTool::Freehand));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Rectangle));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Pan));
}

TEST_CASE("the new tools own their drags", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Polygon, false) == DragOwner::Tool);
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Freehand, true) == DragOwner::Tool);
}

// Held space and the middle button pan whatever the tool -- the rule that lets
// a user reposition the slide without abandoning a half-traced outline.
TEST_CASE("space and the middle button still pan with an N-point tool active",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Polygon, false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false,
                             AnnotationTool::Polygon, false) == DragOwner::Pan);
}

// Screen-space, so a trace has the same vertex density at 1x and at 40x.
TEST_CASE("shouldAppendTrailPoint measures the gap in screen pixels",
          "[ui][AnnotationInteraction]")
{
    // scale 1.0: 1 slide unit is 1 screen pixel, so 2 units clears the 2 px bar.
    REQUIRE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{2.0, 0.0}, 1.0));
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1.0, 0.0}, 1.0));

    // scale 0.1 (zoomed out): the same 2 slide units is 0.2 screen pixels.
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{2.0, 0.0}, 0.1));
    REQUIRE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{20.0, 0.0}, 0.1));
}

TEST_CASE("shouldAppendTrailPoint measures diagonally", "[ui][AnnotationInteraction]")
{
    REQUIRE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1.5, 1.5}, 1.0));
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}, 1.0));
}

// A non-positive scale means the viewport has no usable transform; appending
// on every event would bury the shape in duplicate vertices.
TEST_CASE("shouldAppendTrailPoint refuses a non-positive scale",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1e6, 0.0}, 0.0));
    REQUIRE_FALSE(shouldAppendTrailPoint(core::PointF{0.0, 0.0}, core::PointF{1e6, 0.0}, -1.0));
}

TEST_CASE("closesOnFirstVertex uses the click target radius",
          "[ui][AnnotationInteraction]")
{
    // kHitToleranceScreenPixels is 6.0.
    REQUIRE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{5.0, 0.0}, 1.0));
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{7.0, 0.0}, 1.0));
    // At 40x the same physical target is a fraction of a slide unit.
    REQUIRE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{0.1, 0.0}, 40.0));
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{1.0, 0.0}, 40.0));
}

TEST_CASE("closesOnFirstVertex refuses a non-positive scale",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{0.0, 0.0}, 0.0));
}

TEST_CASE("pendingShapeCommits enforces the geometry minimums",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(pendingShapeCommits(0, true));
    REQUIRE_FALSE(pendingShapeCommits(2, true));
    REQUIRE(pendingShapeCommits(3, true));
    REQUIRE_FALSE(pendingShapeCommits(1, false));
    REQUIRE(pendingShapeCommits(2, false));
}
