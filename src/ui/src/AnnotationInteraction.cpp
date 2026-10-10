#include "slideio/viewer/ui/AnnotationInteraction.h"

#include <algorithm>

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

core::RectangleGeometry translatedBox(const core::RectF& box, double dx, double dy)
{
    return core::RectangleGeometry{core::PointF{box.x + dx, box.y + dy},
                                   core::PointF{box.x + box.width + dx, box.y + box.height + dy}};
}

} // namespace slideio::viewer::ui
