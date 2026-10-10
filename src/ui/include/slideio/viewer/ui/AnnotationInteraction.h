#pragma once

#include <Qt>

namespace slideio::viewer::ui
{

enum class AnnotationTool
{
    Pan,
    Rectangle,
};

enum class DragOwner
{
    None,
    Pan,
    Tool,
    MoveAnnotation,
};

/// Screen-space radius within which a click counts as hitting an annotation.
/// Screen-space so the target stays the same physical size at 1x and at 40x.
constexpr double kHitToleranceScreenPixels = 6.0;

/// Decides who owns a drag beginning with `button`.
///
/// Pure -- no widget state and no side effects -- so the routing rules can be
/// tested without a QApplication, which no test binary in this repository
/// creates. The rules, in order:
///
///   * middle button pans, whatever the tool (02-user-interface-design.md
///     3.2: pan stays available without leaving annotation mode);
///   * held space pans, whatever the tool;
///   * left button with the pan tool moves an annotation if the press landed
///     on one, and otherwise pans;
///   * left button with a drawing tool belongs to the tool;
///   * anything else owns nothing.
///
/// `modifiers` is unused today and named in the signature for FR-ANN-16's
/// shift-click multi-select, which is the next thing to touch these rules.
[[nodiscard]] DragOwner resolveDragOwner(Qt::MouseButton button,
                                         Qt::KeyboardModifiers modifiers,
                                         bool spaceHeld,
                                         AnnotationTool active,
                                         bool pressHitAnnotation);

/// Converts a screen-space tolerance to slide units at `viewportScale`.
/// Returns 0 for a non-positive scale rather than dividing by it: an infinite
/// tolerance would make every hit test succeed.
[[nodiscard]] double screenToleranceToSlide(double toleranceScreenPixels,
                                            double viewportScale);

} // namespace slideio::viewer::ui
