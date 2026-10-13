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
