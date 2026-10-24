#pragma once

#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationDecoration.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <Qt>

#include <cstddef>
#include <optional>

namespace slideio::viewer::ui
{

enum class AnnotationTool
{
    Pan,
    Rectangle,
    Ellipse,
    Polygon,
    Freehand,
    Point,
    Arrow,
    Text,
    Ruler,
    Angle,
};

enum class DragOwner
{
    None,
    Pan,
    Tool,
    MoveAnnotation,
    /// Dragging one vertex of the annotation in vertex-editing mode.
    MoveVertex,
    /// Dragging one handle of a selected box-shaped annotation.
    ResizeBox,
};

/// What a press does while the viewport is in vertex-editing mode. Separate
/// from DragOwner: that routes drags in the ordinary viewport, and widening it
/// would make one set of rules serve two unrelated situations.
enum class VertexEditAction
{
    None,
    DragVertex,
    InsertAndDrag,
    RemoveVertex,
    Exit,
};

/// What a press means when a box-shaped annotation is selected.
///
/// `None` and `PassThrough` are both handled by falling through to
/// resolveDragOwner today, and are kept distinct because they answer different
/// questions: None means the press was never ours -- a middle-button pan, or a
/// right-click a future context menu will want -- while PassThrough means ours
/// to consider and declined.
enum class BoxResizeAction
{
    None,
    Resize,
    PassThrough,
};

/// Screen-space radius within which a click counts as hitting an annotation.
/// Screen-space so the target stays the same physical size at 1x and at 40x.
constexpr double kHitToleranceScreenPixels = 6.0;

/// Side length of a vertex handle, in screen pixels, so handles stay the same
/// size to grab at 1x and at 40x. Smaller than the grab diameter on purpose:
/// the target is easier to hit than to see precisely, which suits a gesture
/// aimed with a mouse.
constexpr double kVertexHandleScreenPixels = 8.0;

/// Clear gap between a marker's disc and the selection ring drawn around it,
/// measured from the disc's edge to the ring's centreline, in screen pixels.
///
/// A point's outline() is a single vertex, so the ordinary selection strokes
/// paint nothing at all and a selected marker would be pixel-identical to an
/// unselected one. The ring is what the selection looks like instead.
constexpr double kSelectionRingGapScreenPixels = 3.0;

/// Width of that ring's black underlay pen, in screen pixels. Half of it
/// reaches outside the ring's centreline, which is why
/// annotationPaintMarginScreenPixels adds half and not the whole.
constexpr double kSelectionRingPenScreenPixels = 3.0;

/// Minimum screen-pixel gap between consecutive points of a trailed trace.
/// Screen-space, like every other tolerance here, so a trace has the same
/// density at 1x and at 40x. This is the only control on how many vertices a
/// freehand outline accumulates -- the serializer deliberately imposes no
/// maximum, because a dense trace is legitimate data.
constexpr double kTrailMinScreenPixels = 2.0;

/// How far beyond its slide-space bounding box an annotation actually paints,
/// in screen pixels. The viewport culls against the box widened by this.
///
/// The stroke alone is not enough once a shape carries a screen-space glyph.
/// A marker's bounding box is degenerate on BOTH axes, so the stroke-derived
/// margin is about 2 px at the default line width, while its disc reaches
/// kHitToleranceScreenPixels and its selection ring further still. Culling on
/// the stroke alone made markers pop in and out at the viewport edge during a
/// pan.
///
/// The ring is counted even though only a selected annotation draws one,
/// because the selection pass does not cull at all: if the disc were culled
/// while the ring was painted, a selected marker just off-screen would show a
/// ring with nothing inside it. One margin that covers both is cheaper than
/// teaching two passes to agree.
///
/// Pure, so the rule is covered without a QApplication -- which no test binary
/// in this repository creates, and which is why this does not live inside
/// paintAnnotations where it is used.
[[nodiscard]] double annotationPaintMarginScreenPixels(const core::AnnotationGeometry& geom,
                                                       double lineWidth, double viewportScale);

/// True for the tools that build a shape across several press/release pairs.
/// Those tools need pending-shape state that outlives a drag, which the
/// Rectangle tool does not.
[[nodiscard]] bool isNPointTool(AnnotationTool tool);

/// True when the cursor has moved far enough from the last placed vertex to
/// deserve another one. False for a non-positive scale: with no usable
/// transform, appending on every event would bury the shape in duplicates.
[[nodiscard]] bool shouldAppendTrailPoint(core::PointF lastSlide, core::PointF cursorSlide,
                                          double viewportScale);

/// True when a click-placed vertex is a distinct point from the previous one.
///
/// Exact inequality, and deliberately not the trail rule: `shouldAppendTrailPoint`
/// is a density control with a screen-space minimum, while a click is something
/// the user asked for explicitly and must not be thinned. The one vertex that
/// must be refused is the one that coincides exactly, because it describes a
/// zero-length segment -- which paints nothing and measures nothing, but makes
/// the segments on either side of it share an endpoint, and
/// core::isSelfIntersecting only excludes segments adjacent by index. A
/// hand-traced polygon then reads as self-intersecting and its area is
/// withheld.
///
/// Reached by closing a trace with a double-click: Qt sends the first click to
/// mousePressEvent, which places a vertex at a cursor that has usually not
/// moved since the last trailed point.
[[nodiscard]] bool shouldPlaceClickVertex(core::PointF lastSlide, core::PointF cursorSlide);

/// What a press means while vertex editing, given what it landed on.
///
/// `hitVertex` and `hitEdge` come from core::nearestVertex and core::nearestEdge
/// against the annotation being edited. Both can be true at once -- a press on
/// a handle is in range of the two segments meeting there -- and the vertex
/// wins, because the user aimed at something they can see.
///
/// Pure, like every other routing predicate here, so the rules are covered
/// without a QApplication.
[[nodiscard]] VertexEditAction resolveVertexEditPress(Qt::MouseButton button,
                                                      Qt::KeyboardModifiers modifiers,
                                                      bool hitVertex, bool hitEdge);

/// Whether a press on a selected box-shaped annotation starts a resize.
///
/// `hitHandle` comes from core::nearestBoxHandle against the selected shape's
/// normalised bounding box. Tested BEFORE resolveDragOwner, because a corner
/// handle sits on the outline and would otherwise be taken as a move.
///
/// Pure, like every other routing predicate here, so the rules are covered
/// without a QApplication.
[[nodiscard]] BoxResizeAction resolveBoxResizePress(Qt::MouseButton button, bool spaceHeld,
                                                    bool hitHandle);

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

/// True when a tail-to-head drag is long enough on screen to be a deliberate
/// arrow rather than a misclick.
///
/// Not dragExceedsMinimumSize, and the difference is the whole reason this
/// exists: that one tests either DIMENSION of a bounding box, so a diagonal
/// drag of exactly 10 screen pixels presents a 7.07 x 7.07 box and is
/// rejected. An arrow's gesture is a length, so the test is the hypotenuse,
/// and the answer is then the same in every direction.
///
/// False for a non-positive scale, like every other predicate here: with no
/// usable transform there is no screen distance to compare.
[[nodiscard]] bool arrowExceedsMinimumLength(core::PointF tailSlide, core::PointF headSlide,
                                             double viewportScale, double minimumScreenPixels);

/// `box` with any zero-extent dimension widened to `minExtentSlideUnits`.
///
/// Mouse deltas are integers, so a short deliberate horizontal or vertical
/// drag routinely has zero extent on one axis. Rejecting those drags outright
/// gave the user no shape, no selection change and no feedback at all; the
/// degenerate axis is widened instead, so the gesture still produces a real
/// annotation rather than a zero-area one.
[[nodiscard]] core::RectF withMinimumExtent(const core::RectF& box, double minExtentSlideUnits);

/// `box` constrained by the modifiers held during the drag that produced it.
///
/// `box` is the normalised bounding box of the drag, so `anchor` -- the press
/// point -- is one of its four corners, and which corner is what tells this
/// function which way the drag went.
///
///   * Shift squares the box about `anchor`, taking the LARGER of the two
///     extents so the shape never shrinks away from the cursor.
///   * Alt re-centres the box on `anchor` and doubles both extents, making the
///     press point the centre and the drag a radius rather than a diameter.
///   * Both compose, Shift first, so the pair gives a circle centred on the
///     press.
///   * Any other modifier is ignored.
///
/// Shape-agnostic on purpose: it constrains a box and knows nothing about what
/// will be built from it, which is what lets the rectangle tool use it too
/// (FR-ANN-01's "Shift constrains to square").
[[nodiscard]] core::RectF applyDragConstraints(core::RectF box, core::PointF anchor,
                                               Qt::KeyboardModifiers mods);


/// True when the cursor has travelled far enough from the press for a move
/// drag to be a deliberate one.
///
/// Below the threshold the annotation does not move at all. Mouse deltas are
/// integers and a hand resting on a button is not still, so a click meant only
/// to select a shape routinely carries a pixel or two of travel; without this
/// the shape creeps and a "Move" step lands on the undo stack for a move the
/// user never made -- and Ctrl+Z then appears to do nothing, because it undoes
/// a displacement too small to see.
///
/// The threshold is the click-target radius rather than a fourth tolerance of
/// its own: both answer the same question about whether the pointer stayed
/// where it was put. Being screen-space, it costs no precision -- it spans
/// fewer slide units the further in the user has zoomed, so a fine adjustment
/// is always available one zoom step closer.
///
/// False for a non-positive scale, where no screen distance can be computed.
[[nodiscard]] bool moveExceedsDragThreshold(core::PointF pressSlide, core::PointF cursorSlide,
                                            double viewportScale);


/// The geometry a box-shaped drawing tool makes from `box`.
///
/// One function, because the preview and the commit were each deciding it from
/// `activeTool` on their own: the painter chose between an ellipse outline and
/// a rectangle, and commitDrawDrag chose between EllipseGeometry and
/// RectangleGeometry sixty lines away. Two places deciding what shape a drag
/// is can disagree, and the way the user would find out is a dashed ellipse
/// that releases into a rectangle.
///
/// Any tool that is not Ellipse gets a rectangle. That is the existing
/// behaviour made explicit rather than a new rule -- the box-drag path is only
/// reached for Rectangle and Ellipse, because Point has no drag, Arrow has its
/// own preview, and the n-point tools do not come through here at all.
[[nodiscard]] core::AnnotationGeometry boxToolGeometry(AnnotationTool tool,
                                                       const core::RectF& box);

/// Whether a mouse release should be read as a click that changes the
/// selection, rather than as the end of a drag.
///
/// `travelPixels` is the press-to-release screen distance, not whether a move
/// event arrived: Qt delivers one for a single pixel, which made deselecting
/// by clicking empty tissue work only if the mouse never twitched.
///
/// `panningFromSpace` is the one that is easy to miss. Holding Space is the
/// user saying "I am navigating, leave my work alone", and a press under it
/// becomes a pan. Releasing it without moving then ran the selection test and
/// cleared whatever was selected -- in a mode entered precisely to avoid
/// touching anything. A pan the user did not ask for by holding Space, such as
/// the one a read-only slide converts an annotation press into, is NOT
/// excluded: clicking a mark to read its properties is the only way a reader
/// has to select one.
[[nodiscard]] bool releaseSelectsAnnotation(bool leftButton, bool movingAnnotation,
                                            bool panningFromSpace, double travelPixels,
                                            double tolerancePixels);

/// The annotation as it was picked up and as it was put down.
struct MoveEdit
{
    core::Annotation before;
    core::Annotation after;
};

/// The pair a completed move should record on the undo stack, or nothing when
/// the shape ended where it started.
///
/// `original` must be the annotation captured at the press, not the moved one
/// with its geometry put back: core::Annotation::setGeometry restamps
/// modifiedAt, so a before-state rebuilt that way carries the moment of the
/// drag's release as the annotation's last modification. Undoing the move
/// would then write that wrong timestamp to the slide's file -- the one record
/// of when the mark was actually last touched.
///
/// A shape put back where it was is not an edit, and must not occupy a step
/// the user has to press Ctrl+Z through to reach a real one. That comparison
/// is made on the bounding box, which is exact for the translation a move
/// performs.
[[nodiscard]] std::optional<MoveEdit> moveEdit(const core::Annotation& original,
                                               const core::Annotation& moved);

} // namespace slideio::viewer::ui
