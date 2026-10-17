#include "slideio/viewer/ui/AnnotationInteraction.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::ui
{

DragOwner resolveDragOwner(Qt::MouseButton button,
                           Qt::KeyboardModifiers,
                           bool spaceHeld,
                           AnnotationTool active,
                           bool pressHitAnnotation)
{
    if (button == Qt::MiddleButton) {
        return DragOwner::Pan;
    }
    if (button != Qt::LeftButton) {
        return DragOwner::None;
    }
    if (spaceHeld) {
        return DragOwner::Pan;
    }
    if (active == AnnotationTool::Pan) {
        return pressHitAnnotation ? DragOwner::MoveAnnotation : DragOwner::Pan;
    }
    return DragOwner::Tool;
}

double screenToleranceToSlide(double toleranceScreenPixels, double viewportScale)
{
    if (viewportScale <= 0.0) {
        return 0.0;
    }
    return toleranceScreenPixels / viewportScale;
}

bool dragExceedsMinimumSize(const core::RectF& boxSlideUnits,
                            double viewportScale,
                            double minimumScreenPixels)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double widthScreen = boxSlideUnits.width * viewportScale;
    const double heightScreen = boxSlideUnits.height * viewportScale;
    return widthScreen >= minimumScreenPixels || heightScreen >= minimumScreenPixels;
}

core::RectF withMinimumExtent(const core::RectF& box, double minExtentSlideUnits)
{
    const double minimum = std::max(0.0, minExtentSlideUnits);
    core::RectF result = box;
    if (result.width <= 0.0) {
        result.width = minimum;
    }
    if (result.height <= 0.0) {
        result.height = minimum;
    }
    return result;
}

VertexEditAction resolveVertexEditPress(Qt::MouseButton button, Qt::KeyboardModifiers modifiers,
                                        bool hitVertex, bool hitEdge)
{
    // Middle still pans and right is reserved for a context menu; neither may
    // end the mode by accident.
    if (button != Qt::LeftButton) {
        return VertexEditAction::None;
    }
    if (hitVertex) {
        return modifiers.testFlag(Qt::ControlModifier) ? VertexEditAction::RemoveVertex
                                                       : VertexEditAction::DragVertex;
    }
    if (hitEdge) {
        return VertexEditAction::InsertAndDrag;
    }
    return VertexEditAction::Exit;
}

core::RectF applyDragConstraints(core::RectF box, core::PointF anchor,
                                 Qt::KeyboardModifiers mods)
{
    if (mods.testFlag(Qt::ShiftModifier)) {
        // Which corner the anchor sits on says which way the drag went. The
        // midpoint comparison rather than an equality test on doubles: the box
        // came from the anchor by arithmetic, so exact equality is not
        // guaranteed, and a zero-extent box answers "extends positive" either
        // way, which is harmless because there is nothing to extend.
        const bool extendsRight = anchor.x <= box.x + box.width * 0.5;
        const bool extendsDown = anchor.y <= box.y + box.height * 0.5;
        const double side = std::max(box.width, box.height);
        box.x = extendsRight ? anchor.x : anchor.x - side;
        box.y = extendsDown ? anchor.y : anchor.y - side;
        box.width = side;
        box.height = side;
    }
    if (mods.testFlag(Qt::AltModifier)) {
        box.x = anchor.x - box.width;
        box.y = anchor.y - box.height;
        box.width *= 2.0;
        box.height *= 2.0;
    }
    return box;
}

bool shouldPlaceClickVertex(core::PointF lastSlide, core::PointF cursorSlide)
{
    return lastSlide.x != cursorSlide.x || lastSlide.y != cursorSlide.y;
}

bool isNPointTool(AnnotationTool tool)
{
    return tool == AnnotationTool::Polygon || tool == AnnotationTool::Freehand;
}

bool shouldAppendTrailPoint(core::PointF lastSlide, core::PointF cursorSlide,
                            double viewportScale)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double dxScreen = (cursorSlide.x - lastSlide.x) * viewportScale;
    const double dyScreen = (cursorSlide.y - lastSlide.y) * viewportScale;
    return std::hypot(dxScreen, dyScreen) >= kTrailMinScreenPixels;
}

bool closesOnFirstVertex(core::PointF firstVertexSlide, core::PointF cursorSlide,
                         double viewportScale)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double dxScreen = (cursorSlide.x - firstVertexSlide.x) * viewportScale;
    const double dyScreen = (cursorSlide.y - firstVertexSlide.y) * viewportScale;
    return std::hypot(dxScreen, dyScreen) <= kHitToleranceScreenPixels;
}

bool pendingShapeCommits(std::size_t vertexCount, bool closed)
{
    return vertexCount >= (closed ? core::kMinPolygonVertices : core::kMinPolylineVertices);
}

bool moveExceedsDragThreshold(core::PointF pressSlide, core::PointF cursorSlide,
                              double viewportScale)
{
    if (viewportScale <= 0.0) {
        return false;
    }
    const double dxScreen = (cursorSlide.x - pressSlide.x) * viewportScale;
    const double dyScreen = (cursorSlide.y - pressSlide.y) * viewportScale;
    return std::hypot(dxScreen, dyScreen) >= kHitToleranceScreenPixels;
}

std::optional<MoveEdit> moveEdit(const core::Annotation& original,
                                 const core::Annotation& moved)
{
    const core::RectF boxBefore = core::boundingBox(original.geometry());
    const core::RectF boxAfter = core::boundingBox(moved.geometry());

    // core::Rect is an aggregate of x/y/width/height with no operator==, so the
    // four fields are compared directly.
    if (boxBefore.x == boxAfter.x && boxBefore.y == boxAfter.y
        && boxBefore.width == boxAfter.width && boxBefore.height == boxAfter.height) {
        return std::nullopt;
    }

    return MoveEdit{original, moved};
}

} // namespace slideio::viewer::ui
