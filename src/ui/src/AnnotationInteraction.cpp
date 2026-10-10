#include "slideio/viewer/ui/AnnotationInteraction.h"

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

} // namespace slideio::viewer::ui
