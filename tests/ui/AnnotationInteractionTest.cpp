#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationInteraction.h"

using namespace slideio::viewer::ui;

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
}

TEST_CASE("tolerance converts from screen pixels to slide units",
          "[ui][AnnotationInteraction]")
{
    // At 2x, 6 screen pixels is 3 slide pixels.
    REQUIRE(screenToleranceToSlide(6.0, 2.0) == 3.0);
    // At 0.5x, the same 6 screen pixels covers 12 slide pixels.
    REQUIRE(screenToleranceToSlide(6.0, 0.5) == 12.0);
}

// Review Focus 1: dividing by a non-positive scale yields an infinite
// tolerance, which makes every hit test succeed -- one click would select an
// annotation anywhere on the slide.
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
