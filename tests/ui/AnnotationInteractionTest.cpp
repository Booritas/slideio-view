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

// Closing a trace by double-click sends the first click to mousePressEvent,
// which places a vertex at the cursor. If the cursor has not moved since the
// last trailed point -- which is most of the time, because the user stops to
// double-click -- that vertex lands exactly on the previous one. The resulting
// zero-length segment makes its neighbours share an endpoint and the polygon
// reads as self-intersecting, which withholds its area. Observed on a real
// 160-vertex trace, 2026-10-17.
TEST_CASE("a click does not place a vertex on top of the previous one",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(shouldPlaceClickVertex(core::PointF{10.0, 20.0}, core::PointF{10.0, 20.0}));
}

// Exact equality only. A deliberate click a fraction of a slide pixel away is
// a real point the user asked for, and the short segment it makes confuses
// nothing -- unlike the trail rule, this is not a density control.
TEST_CASE("a click just off the previous vertex still places one",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(shouldPlaceClickVertex(core::PointF{10.0, 20.0}, core::PointF{10.0, 20.5}));
    REQUIRE(shouldPlaceClickVertex(core::PointF{10.0, 20.0}, core::PointF{10.5, 20.0}));
    REQUIRE(shouldPlaceClickVertex(core::PointF{10.0, 20.0}, core::PointF{-10.0, -20.0}));
}

TEST_CASE("a left press on a handle drags that vertex", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, true, false)
            == VertexEditAction::DragVertex);
}

// Design spec 3.4: a press on a handle is in range of the vertex AND of the
// two segments meeting there. The vertex wins, or pressing a handle would
// insert a new vertex on top of the one the user meant to grab.
TEST_CASE("a vertex hit beats an edge hit", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, true, true)
            == VertexEditAction::DragVertex);
}

TEST_CASE("ctrl and a handle removes that vertex", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ControlModifier, true, false)
            == VertexEditAction::RemoveVertex);
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ControlModifier, true, true)
            == VertexEditAction::RemoveVertex);
}

TEST_CASE("a left press on the outline inserts and drags",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, false, true)
            == VertexEditAction::InsertAndDrag);
    // Ctrl means "remove" only on a handle; on an edge there is nothing to
    // remove, so it does not suppress the insert.
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ControlModifier, false, true)
            == VertexEditAction::InsertAndDrag);
}

TEST_CASE("a left press on neither leaves the mode", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::NoModifier, false, false)
            == VertexEditAction::Exit);
}

// A middle or right press must not exit the mode: the middle button pans, and
// right is reserved for a future context menu.
TEST_CASE("a non-left press does nothing while editing",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::MiddleButton, Qt::NoModifier, true, false)
            == VertexEditAction::None);
    REQUIRE(resolveVertexEditPress(Qt::RightButton, Qt::NoModifier, false, false)
            == VertexEditAction::None);
}

// Shift and Alt have meanings while drawing; neither means anything here and
// neither may divert the gesture.
TEST_CASE("unrelated modifiers do not change the action",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::ShiftModifier, true, false)
            == VertexEditAction::DragVertex);
    REQUIRE(resolveVertexEditPress(Qt::LeftButton, Qt::AltModifier, false, true)
            == VertexEditAction::InsertAndDrag);
}

TEST_CASE("a left press on a resize handle resizes", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::LeftButton, false, true) == BoxResizeAction::Resize);
}

TEST_CASE("a left press off the handles passes through",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::LeftButton, false, false)
            == BoxResizeAction::PassThrough);
}

// Space pans whatever else is happening. That promise predates every mode in
// this widget and was broken once already during vertex editing.
TEST_CASE("held space passes through even over a handle",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::LeftButton, true, true) == BoxResizeAction::PassThrough);
}

// Middle pans and right is reserved for a future context menu; neither is a
// resize, and neither may be swallowed.
TEST_CASE("a non-left press is not ours", "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveBoxResizePress(Qt::MiddleButton, false, true) == BoxResizeAction::None);
    REQUIRE(resolveBoxResizePress(Qt::RightButton, false, true) == BoxResizeAction::None);
    REQUIRE(resolveBoxResizePress(Qt::MiddleButton, true, false) == BoxResizeAction::None);
}

// A box minimum is the wrong test for an arrow. dragExceedsMinimumSize asks
// whether EITHER dimension of a bounding box reaches the threshold, so a
// diagonal drag of exactly 10 screen pixels presents a 7.07 x 7.07 box and is
// rejected -- a gesture the user plainly made.
TEST_CASE("a diagonal drag at the threshold is accepted as an arrow",
          "[ui][AnnotationInteraction]")
{
    // 3-4-5 at scale 2 is a 10-pixel shaft, in a 6 x 8 box.
    REQUIRE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{3.0, 4.0},
                                      2.0, 10.0));
    // The box test, for contrast: neither dimension reaches 10.
    REQUIRE_FALSE(dragExceedsMinimumSize(
        core::boundingBox(core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                  core::PointF{3.0, 4.0}}),
        2.0, 10.0));
}

TEST_CASE("an arrow shorter than the minimum is refused",
          "[ui][AnnotationInteraction]")
{
    // 3-4-5 at scale 1 is a 5-pixel shaft.
    REQUIRE_FALSE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{3.0, 4.0},
                                            1.0, 10.0));
}

TEST_CASE("an arrow is measured the same in every direction",
          "[ui][AnnotationInteraction]")
{
    const double scale = 1.0;
    const double minimum = 10.0;
    // Right, left, down, up and a diagonal, each 20 slide units long or
    // longer, so direction cannot change the answer.
    REQUIRE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{20.0, 0.0},
                                      scale, minimum));
    REQUIRE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{-20.0, 0.0},
                                      scale, minimum));
    REQUIRE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{0.0, 20.0},
                                      scale, minimum));
    REQUIRE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{0.0, -20.0},
                                      scale, minimum));
    REQUIRE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0}, core::PointF{-20.0, -20.0},
                                      scale, minimum));
}

TEST_CASE("a zero-length arrow is refused", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(arrowExceedsMinimumLength(core::PointF{40.0, 40.0},
                                            core::PointF{40.0, 40.0}, 1.0, 10.0));
}

// Review Focus 4: with no usable transform there is no screen distance to
// compare, and inventing one would let a focus loss before the first frame
// commit an arrow out of nothing. False, like every other predicate here.
TEST_CASE("a non-positive scale refuses an arrow", "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0},
                                            core::PointF{1000.0, 1000.0}, 0.0, 10.0));
    REQUIRE_FALSE(arrowExceedsMinimumLength(core::PointF{0.0, 0.0},
                                            core::PointF{1000.0, 1000.0}, -2.0, 10.0));
}

// The two new tools are single-gesture tools, not multi-click ones: they
// commit on the release of the press that started them, so no pending-shape
// state outlives a drag.
TEST_CASE("the point and arrow tools are not n-point tools",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Point));
    REQUIRE_FALSE(isNPointTool(AnnotationTool::Arrow));
}

// Both are drawing tools, so a left press belongs to the tool and never to a
// move -- exactly as it does for the rectangle.
TEST_CASE("a left press under the point or arrow tool belongs to the tool",
          "[ui][AnnotationInteraction]")
{
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Point, true) == DragOwner::Tool);
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, false,
                             AnnotationTool::Arrow, false) == DragOwner::Tool);
    // Held space still pans, whatever the tool.
    REQUIRE(resolveDragOwner(Qt::LeftButton, Qt::NoModifier, true,
                             AnnotationTool::Point, false) == DragOwner::Pan);
}

// Review finding: a marker's bounding box is degenerate on both axes, so the
// stroke-derived margin alone is 2 px at the default line width -- while its
// disc is 6 px in radius and its selection ring reaches 10.5 px. A marker
// whose centre sat just outside the widget was culled with part of its glyph
// on screen, and because the selection pass does not cull at all, a SELECTED
// marker just off-screen painted a ring with no disc inside it.
TEST_CASE("a shape with no glyph is margined by its stroke alone",
          "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry rect =
        core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{10.0, 10.0}};
    REQUIRE(annotationPaintMarginScreenPixels(rect, 2.0, 1.0) == 2.0);
    REQUIRE(annotationPaintMarginScreenPixels(rect, 8.0, 1.0) == 5.0);
}

// The disc reaches its radius; the ring is included because the selection pass
// does not cull, so the disc must not be culled while the ring would paint.
TEST_CASE("a marker is margined past its disc and its selection ring",
          "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry marker = core::PointGeometry{core::PointF{5.0, 5.0}};
    const double expected = kHitToleranceScreenPixels + kSelectionRingGapScreenPixels
                          + kSelectionRingPenScreenPixels / 2.0;
    REQUIRE(annotationPaintMarginScreenPixels(marker, 2.0, 1.0) == expected);
    // Screen-space, so it does not shrink as the user zooms out.
    REQUIRE(annotationPaintMarginScreenPixels(marker, 2.0, 0.01) == expected);
}

// A fat stroke can out-reach the glyph, and then it is the margin.
TEST_CASE("the wider of the stroke and the glyph wins",
          "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry marker = core::PointGeometry{core::PointF{5.0, 5.0}};
    REQUIRE(annotationPaintMarginScreenPixels(marker, 100.0, 1.0) == 51.0);
}

// An arrowhead's barbs reach half the head's size perpendicular to the shaft,
// which is outside the shaft's own bounding box.
TEST_CASE("an arrow is margined past its head", "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry arrow =
        core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{100.0, 0.0}};
    const double expected = core::kArrowHeadScreenPixels + kSelectionRingGapScreenPixels
                          + kSelectionRingPenScreenPixels / 2.0;
    REQUIRE(annotationPaintMarginScreenPixels(arrow, 2.0, 1.0) == expected);
}

// No usable transform means no glyph, so the stroke is all that is left. This
// must not divide by or multiply through a scale it cannot use.
TEST_CASE("a non-positive scale falls back to the stroke margin",
          "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry marker = core::PointGeometry{core::PointF{5.0, 5.0}};
    REQUIRE(annotationPaintMarginScreenPixels(marker, 2.0, 0.0) == 2.0);
    REQUIRE(annotationPaintMarginScreenPixels(marker, 2.0, -1.0) == 2.0);
}

// A degenerate arrow from disk has a zero-size head, but the ring still
// reaches past it, so the margin is the ring's and not the stroke's.
TEST_CASE("a zero-length arrow is still margined by its ring",
          "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry arrow =
        core::ArrowGeometry{core::PointF{40.0, 40.0}, core::PointF{40.0, 40.0}};
    REQUIRE(annotationPaintMarginScreenPixels(arrow, 2.0, 1.0)
            == kSelectionRingGapScreenPixels + kSelectionRingPenScreenPixels / 2.0);
}

// --- boxToolGeometry -------------------------------------------------------

TEST_CASE("boxToolGeometry makes an ellipse for the ellipse tool", "[ui][AnnotationInteraction]")
{
    const core::AnnotationGeometry geometry =
        boxToolGeometry(AnnotationTool::Ellipse, core::RectF{10.0, 20.0, 30.0, 40.0});

    REQUIRE(core::typeOf(geometry) == core::AnnotationType::Ellipse);
    REQUIRE(core::boundingBox(geometry).x == 10.0);
    REQUIRE(core::boundingBox(geometry).width == 30.0);
}

TEST_CASE("boxToolGeometry makes a rectangle for every other box tool", "[ui][AnnotationInteraction]")
{
    // The preview and the commit both ask this, so the two cannot disagree
    // about what shape the drag in progress is going to become.
    REQUIRE(core::typeOf(boxToolGeometry(AnnotationTool::Rectangle,
                                             core::RectF{0.0, 0.0, 5.0, 5.0}))
            == core::AnnotationType::Rectangle);
    REQUIRE(core::typeOf(boxToolGeometry(AnnotationTool::Pan,
                                             core::RectF{0.0, 0.0, 5.0, 5.0}))
            == core::AnnotationType::Rectangle);
}

TEST_CASE("boxToolGeometry keeps a collapsed axis collapsed", "[ui][AnnotationInteraction]")
{
    // withMinimumExtent is the commit's job, not this function's: the preview
    // asks the same question and must paint the gesture as it stands.
    const core::RectF box = core::boundingBox(
        boxToolGeometry(AnnotationTool::Rectangle, core::RectF{3.0, 7.0, 12.0, 0.0}));

    REQUIRE(box.height == 0.0);
}

// --- releaseSelectsAnnotation ----------------------------------------------

TEST_CASE("releaseSelectsAnnotation accepts a still left click", "[ui][AnnotationInteraction]")
{
    REQUIRE(releaseSelectsAnnotation(true, false, false, 0.0, 6.0));
    REQUIRE(releaseSelectsAnnotation(true, false, false, 5.9, 6.0));
}

TEST_CASE("releaseSelectsAnnotation refuses a release that travelled", "[ui][AnnotationInteraction]")
{
    // The press-to-release distance, not "did a move event arrive": Qt
    // delivers one for a single pixel.
    REQUIRE_FALSE(releaseSelectsAnnotation(true, false, false, 6.0, 6.0));
    REQUIRE_FALSE(releaseSelectsAnnotation(true, false, false, 40.0, 6.0));
}

TEST_CASE("releaseSelectsAnnotation refuses anything but a left release",
          "[ui][AnnotationInteraction]")
{
    REQUIRE_FALSE(releaseSelectsAnnotation(false, false, false, 0.0, 6.0));
}

TEST_CASE("releaseSelectsAnnotation refuses the end of an annotation move",
          "[ui][AnnotationInteraction]")
{
    // A move that ended where it began is still a move, not a click on
    // whatever happens to be under the cursor.
    REQUIRE_FALSE(releaseSelectsAnnotation(true, true, false, 0.0, 6.0));
}

TEST_CASE("releaseSelectsAnnotation refuses a click made under held Space",
          "[ui][AnnotationInteraction]")
{
    // Holding Space is the user saying they are navigating. A still click in
    // that mode used to clear the selection -- in the one mode entered
    // specifically to avoid touching the work.
    REQUIRE_FALSE(releaseSelectsAnnotation(true, false, true, 0.0, 6.0));
}

TEST_CASE("releaseSelectsAnnotation still accepts a pan the user did not ask for",
          "[ui][AnnotationInteraction]")
{
    // A read-only slide converts a press on an annotation into a pan. Space is
    // not held, so this is still a selection click -- clicking a mark to read
    // its properties is the only way a reader has to select one.
    REQUIRE(releaseSelectsAnnotation(true, false, false, 1.0, 6.0));
}

TEST_CASE("the overlay paints whenever anything is being built", "[ui][AnnotationInteraction]")
{
    CHECK_FALSE(overlayNeedsPainting(false, false, false, false));
    CHECK(overlayNeedsPainting(true, false, false, false));
    CHECK(overlayNeedsPainting(false, true, false, false));
    CHECK(overlayNeedsPainting(false, false, true, false));
    // A pending angle on a scene with nothing else on it: its rays and live
    // angle must still be drawn, or the user commits an angle they never saw.
    CHECK(overlayNeedsPainting(false, false, false, true));
}
