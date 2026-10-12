#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <Qt>

#include <cstddef>

namespace slideio::viewer::ui
{

enum class AnnotationTool
{
    Pan,
    Rectangle,
    Polygon,
    Freehand,
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

/// Minimum screen-pixel gap between consecutive points of a trailed trace.
/// Screen-space, like every other tolerance here, so a trace has the same
/// density at 1x and at 40x. This is the only control on how many vertices a
/// freehand outline accumulates -- the serializer deliberately imposes no
/// maximum, because a dense trace is legitimate data.
constexpr double kTrailMinScreenPixels = 2.0;

/// True for the tools that build a shape across several press/release pairs.
/// Those tools need pending-shape state that outlives a drag, which the
/// Rectangle tool does not.
[[nodiscard]] bool isNPointTool(AnnotationTool tool);

/// True when the cursor has moved far enough from the last placed vertex to
/// deserve another one. False for a non-positive scale: with no usable
/// transform, appending on every event would bury the shape in duplicates.
[[nodiscard]] bool shouldAppendTrailPoint(core::PointF lastSlide, core::PointF cursorSlide,
                                          double viewportScale);

/// True when a click at `cursorSlide` lands on the shape's first vertex and so
/// closes it. Reuses the click-target radius rather than introducing a second
/// screen-space constant for the same kind of question.
[[nodiscard]] bool closesOnFirstVertex(core::PointF firstVertexSlide, core::PointF cursorSlide,
                                       double viewportScale);

/// True when a pending shape has enough vertices to become an annotation.
/// Mirrors core::kMinPolygonVertices / kMinPolylineVertices: a commit below
/// them is discarded, the way an undersized rectangle drag already is.
[[nodiscard]] bool pendingShapeCommits(std::size_t vertexCount, bool closed);

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

/// True when a drag is big enough on screen to be a deliberate shape rather
/// than a misclick. Either dimension reaching the threshold is enough, so a
/// long thin rectangle stays drawable -- but both must be non-zero, or a
/// perfectly horizontal drag would produce a zero-height annotation.
[[nodiscard]] bool dragExceedsMinimumSize(const core::RectF& boxSlideUnits,
                                          double viewportScale,
                                          double minimumScreenPixels);

/// `box` with any zero-extent dimension widened to `minExtentSlideUnits`.
///
/// Mouse deltas are integers, so a short deliberate horizontal or vertical
/// drag routinely has zero extent on one axis. Rejecting those drags outright
/// gave the user no shape, no selection change and no feedback at all; the
/// degenerate axis is widened instead, so the gesture still produces a real
/// annotation rather than a zero-area one.
[[nodiscard]] core::RectF withMinimumExtent(const core::RectF& box, double minExtentSlideUnits);

} // namespace slideio::viewer::ui
