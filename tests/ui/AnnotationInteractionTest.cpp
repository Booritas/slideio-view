#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationInteraction.h"

#include <chrono>

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
    REQUIRE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{6.0, 0.0}, 1.0));
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
    REQUIRE_FALSE(closesOnFirstVertex(core::PointF{0.0, 0.0}, core::PointF{0.0, 0.0}, -1.0));
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

namespace
{

core::Annotation annotationAt(double x, double y, std::chrono::system_clock::time_point modified)
{
    core::AnnotationMetadata metadata;
    metadata.author = "tester";
    metadata.createdAt = std::chrono::system_clock::time_point{} + std::chrono::seconds(100);
    metadata.modifiedAt = modified;
    return core::Annotation("abc", core::RectangleGeometry{{x, y}, {x + 10.0, y + 10.0}},
                            core::AnnotationProperties{}, metadata);
}

} // namespace

TEST_CASE("a one-pixel twitch does not begin a move", "[ui][AnnotationInteraction]")
{
    // A click meant only to select the shape. Allowing this through moves the
    // annotation by a distance the user cannot see and records an undo step
    // for it, so their next Ctrl+Z appears to do nothing.
    REQUIRE_FALSE(moveExceedsDragThreshold({100.0, 100.0}, {101.0, 100.0}, 1.0));
}

TEST_CASE("a drag past the click-target radius begins a move",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(moveExceedsDragThreshold({100.0, 100.0}, {110.0, 100.0}, 1.0));
    // Exactly at the radius counts, matching closesOnFirstVertex's boundary.
    REQUIRE(moveExceedsDragThreshold({100.0, 100.0}, {100.0 + kHitToleranceScreenPixels, 100.0},
                                     1.0));
}

TEST_CASE("the move threshold is screen-space, not slide-space",
          "[ui][AnnotationInteraction]")
{
    // Zoomed far out, a large slide-space distance is a small screen one and
    // must not count; zoomed in, a small slide-space distance does.
    REQUIRE_FALSE(moveExceedsDragThreshold({0.0, 0.0}, {50.0, 0.0}, 0.1));
    REQUIRE(moveExceedsDragThreshold({0.0, 0.0}, {1.0, 0.0}, 10.0));
}

TEST_CASE("no move is begun without a usable scale", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(moveExceedsDragThreshold({0.0, 0.0}, {1000.0, 1000.0}, 0.0));
    REQUIRE_FALSE(moveExceedsDragThreshold({0.0, 0.0}, {1000.0, 1000.0}, -1.0));
}

TEST_CASE("a recorded move keeps the timestamp the annotation was picked up with",
          "[ui][AnnotationInteraction]")
{
    // The before-state must be the annotation as it was, timestamp included.
    // Rebuilding it by putting the geometry back on a copy of the moved one
    // restamps modifiedAt, and undoing the move would then save the moment of
    // the drag's release as when the mark was last touched.
    const auto pickedUpAt = std::chrono::system_clock::time_point{} + std::chrono::seconds(500);
    const auto droppedAt = std::chrono::system_clock::time_point{} + std::chrono::seconds(900);

    const core::Annotation original = annotationAt(0.0, 0.0, pickedUpAt);
    const core::Annotation moved = annotationAt(40.0, 0.0, droppedAt);

    const std::optional<MoveEdit> edit = moveEdit(original, moved);

    REQUIRE(edit.has_value());
    REQUIRE(edit->before.metadata().modifiedAt == pickedUpAt);
    REQUIRE(edit->before.metadata().createdAt == original.metadata().createdAt);
    REQUIRE(core::boundingBox(edit->before.geometry()).x == 0.0);
}

TEST_CASE("a recorded move carries the annotation as it was put down",
          "[ui][AnnotationInteraction]")
{
    const auto droppedAt = std::chrono::system_clock::time_point{} + std::chrono::seconds(900);
    const core::Annotation original = annotationAt(0.0, 0.0,
        std::chrono::system_clock::time_point{} + std::chrono::seconds(500));
    const core::Annotation moved = annotationAt(40.0, 0.0, droppedAt);

    const std::optional<MoveEdit> edit = moveEdit(original, moved);

    REQUIRE(edit.has_value());
    REQUIRE(edit->after.metadata().modifiedAt == droppedAt);
    REQUIRE(core::boundingBox(edit->after.geometry()).x == 40.0);
    REQUIRE(edit->after.id() == original.id());
}

TEST_CASE("a shape put back where it was is not a recorded move",
          "[ui][AnnotationInteraction]")
{
    // Escape-cancelling restores the original itself, and a drag that ends at
    // its start is no edit. Either way the stack must not gain a step that
    // does nothing when the user presses Ctrl+Z.
    const core::Annotation original = annotationAt(0.0, 0.0,
        std::chrono::system_clock::time_point{} + std::chrono::seconds(500));
    const core::Annotation unmoved = annotationAt(0.0, 0.0,
        std::chrono::system_clock::time_point{} + std::chrono::seconds(900));

    REQUIRE_FALSE(moveEdit(original, unmoved).has_value());
}

TEST_CASE("no modifier leaves the drag box untouched", "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result = applyDragConstraints(box, core::PointF{10.0, 20.0}, Qt::NoModifier);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == 20.0);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 40.0);
}

// Review Focus 3: a modifier this function does not implement must not fall
// into a constraint branch.
TEST_CASE("an unrelated modifier leaves the drag box untouched",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    for (const Qt::KeyboardModifiers mods :
         {Qt::KeyboardModifiers(Qt::ControlModifier), Qt::KeyboardModifiers(Qt::MetaModifier)}) {
        const core::RectF result = applyDragConstraints(box, core::PointF{10.0, 20.0}, mods);
        REQUIRE(result.width == 100.0);
        REQUIRE(result.height == 40.0);
    }
}

// The anchor is the top-left corner, so the square grows right and down.
TEST_CASE("shift squares the box about an anchor dragged down-right",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{10.0, 20.0}, Qt::ShiftModifier);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == 20.0);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
}

// The anchor is the bottom-right corner, so the square grows left and up and
// the box origin moves.
TEST_CASE("shift squares the box about an anchor dragged up-left",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{110.0, 60.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == -40.0);
}

// The anchor is the bottom-left corner: the square grows right and up.
TEST_CASE("shift squares the box about an anchor dragged up-right",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{10.0, 60.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == -40.0);
}

// The anchor is the top-right corner: the square grows left and down.
TEST_CASE("shift squares the box about an anchor dragged down-left",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{110.0, 20.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 100.0);
    REQUIRE(result.height == 100.0);
    REQUIRE(result.x == 10.0);
    REQUIRE(result.y == 20.0);
}

TEST_CASE("shift takes the larger extent so the shape never shrinks",
          "[ui][AnnotationInteraction]")
{
    const core::RectF tall{0.0, 0.0, 10.0, 90.0};
    const core::RectF result =
        applyDragConstraints(tall, core::PointF{0.0, 0.0}, Qt::ShiftModifier);
    REQUIRE(result.width == 90.0);
    REQUIRE(result.height == 90.0);
}

// Alt makes the press point the centre and the drag a radius, so both extents
// double.
TEST_CASE("alt re-centres the box on the anchor and doubles it",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result =
        applyDragConstraints(box, core::PointF{10.0, 20.0}, Qt::AltModifier);
    REQUIRE(result.width == 200.0);
    REQUIRE(result.height == 80.0);
    REQUIRE(result.x == -90.0);
    REQUIRE(result.y == -20.0);
    REQUIRE(result.x + result.width * 0.5 == 10.0);
    REQUIRE(result.y + result.height * 0.5 == 20.0);
}

TEST_CASE("shift and alt together give a square centred on the anchor",
          "[ui][AnnotationInteraction]")
{
    const core::RectF box{10.0, 20.0, 100.0, 40.0};
    const core::RectF result = applyDragConstraints(
        box, core::PointF{10.0, 20.0}, Qt::ShiftModifier | Qt::AltModifier);
    REQUIRE(result.width == 200.0);
    REQUIRE(result.height == 200.0);
    REQUIRE(result.x + result.width * 0.5 == 10.0);
    REQUIRE(result.y + result.height * 0.5 == 20.0);
}

// Review Focus 4: a press and release without moving. No NaN, no inverted box.
TEST_CASE("a zero-length drag stays zero under either modifier",
          "[ui][AnnotationInteraction]")
{
    const core::RectF empty{50.0, 50.0, 0.0, 0.0};
    for (const Qt::KeyboardModifiers mods :
         {Qt::KeyboardModifiers(Qt::ShiftModifier), Qt::KeyboardModifiers(Qt::AltModifier),
          Qt::KeyboardModifiers(Qt::ShiftModifier | Qt::AltModifier)}) {
        const core::RectF result = applyDragConstraints(empty, core::PointF{50.0, 50.0}, mods);
        REQUIRE(result.width == 0.0);
        REQUIRE(result.height == 0.0);
        REQUIRE(result.x == 50.0);
        REQUIRE(result.y == 50.0);
    }
}

// The ellipse is a single-drag tool, like the rectangle. isNPointTool
// returning false is the whole of what routes it down that path rather than
// the multi-click one.
TEST_CASE("the ellipse tool is not an n-point tool", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Ellipse));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Rectangle));
    REQUIRE(isNPointTool(AnnotationTool::Polygon));
    REQUIRE(isNPointTool(AnnotationTool::Freehand));
}

TEST_CASE("a left drag with the ellipse tool belongs to the tool",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false, AnnotationTool::Ellipse,
                             false) == DragOwner::Tool);
    // Shift is the circle constraint, not a selection modifier, so it must not
    // divert the drag away from the tool.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::ShiftModifier, false, AnnotationTool::Ellipse,
                             true) == DragOwner::Tool);
    // Space and the middle button still pan, whatever the tool.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true, AnnotationTool::Ellipse,
                             false) == DragOwner::Pan);
    REQUIRE(resolveDragOwner(Qt::MiddleButton, Qt::NoModifier, false, AnnotationTool::Ellipse,
                             false) == DragOwner::Pan);
}
