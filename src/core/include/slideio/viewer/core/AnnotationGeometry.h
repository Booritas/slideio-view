#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <variant>
#include <vector>

namespace slideio::viewer::core
{

struct RectangleGeometry
{
    PointF topLeft;
    PointF bottomRight;
};

/// An axis-aligned ellipse, stored as the bounding box of the drag that made
/// it (FR-ANN-02).
///
/// The box rather than a centre and radii, so RectangleGeometry's
/// normalisation of an inverted drag, ui::dragExceedsMinimumSize and
/// ui::withMinimumExtent all apply unchanged rather than through conversions --
/// and so the written file reads the same way a rectangle's does.
struct EllipseGeometry
{
    PointF topLeft;
    PointF bottomRight;
};

/// Named kind of a shape. Derived from the geometry rather than stored
/// alongside it, so the two cannot disagree -- see typeOf() below.
enum class AnnotationType
{
    Rectangle,
    Ellipse,
    Polygon,
    Polyline,
};

/// A sequence of vertices, closed into a polygon (FR-ANN-03) or left open as a
/// freehand line (FR-ANN-04).
///
/// One type for both, because they differ only in whether the path closes:
/// the vertices, the bounding box, the translation and the edge hit-test are
/// identical, and only the closing segment, the fill and the interior test
/// differ. Two variant alternatives would duplicate all of that to express
/// one boolean.
///
/// Self-intersecting outlines are valid. Pathologists produce them by accident
/// when tracing quickly, and silently rejecting or repairing someone's outline
/// is worse than rendering what they drew; the even-odd rule handles them.
struct PolygonGeometry
{
    std::vector<PointF> vertices;
    bool closed = true;
};

/// Fewer vertices than this cannot describe the shape: a closed polygon needs
/// three, an open path two. Enforced by the drawing tool at commit and by the
/// serializer on both read and write, so the application cannot produce a file
/// it would refuse to open.
constexpr std::size_t kMinPolygonVertices = 3;
constexpr std::size_t kMinPolylineVertices = 2;

/// Vertices in an ellipse's outline. Fixed, never adaptive and never derived
/// from the shape's size or the current zoom: an adaptive count would make a
/// stored annotation's measured area depend on the zoom it happened to be
/// measured at, and sub-project G exists to make measurements reproducible.
///
/// At 512 the area reads about 0.0025% under the true ellipse and the facet
/// bulge is about 0.19 px on a shape 20000 screen pixels wide. See the design
/// spec 3.2 before changing it -- both numbers move with it.
constexpr std::size_t kEllipseOutlineVertices = 512;

using AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry, EllipseGeometry>;

/// Always positive-extent: a shape built from an inverted drag is normalised.
[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);

/// True when `point` lies inside the shape or within `toleranceSlideUnits` of
/// its outline. Filled annotations (FR-ANN-11 gives them a fill opacity) must
/// be clickable in their interior.
///
/// The tolerance is in SLIDE units. Callers hold a screen-pixel tolerance and
/// convert with ui::screenToleranceToSlide, which keeps the only
/// zoom-dependent arithmetic at one call site and this function pure.
/// A negative tolerance is treated as zero.
[[nodiscard]] bool hitTest(const AnnotationGeometry& geom, PointF point,
                           double toleranceSlideUnits);

/// The shape as a sequence of points to stroke, in order, plus whether the
/// last point joins back to the first.
///
/// This is the whole of what the painter needs to know about a geometry, and
/// it is plain data: the Qt-side conversion to a QPainterPath is a loop with
/// no knowledge of shape types. Keeping the per-type decision here is also
/// what makes it testable -- no test binary in this repository creates a
/// QApplication, so nothing inside a widget can be covered.
struct Outline
{
    std::vector<PointF> points;
    bool closed = true;
};

/// The kind of shape `geom` holds. The single source of truth for an
/// annotation's type; Annotation::type() delegates here.
[[nodiscard]] AnnotationType typeOf(const AnnotationGeometry& geom);

/// `geom` as points to stroke. A rectangle yields its four corners from the
/// normalised box, so the painted outline matches the box hitTest uses.
[[nodiscard]] Outline outline(const AnnotationGeometry& geom);

/// `geom` moved by a slide-space delta, shape and vertex count preserved.
///
/// Returns a whole geometry rather than a rectangle: the move handler used to
/// rebuild every annotation from its bounding box, which would have turned a
/// polygon into a rectangle the first time a user dragged it.
[[nodiscard]] AnnotationGeometry translated(const AnnotationGeometry& geom, double dx, double dy);

/// True when the shape cannot self-intersect whatever its values -- a property
/// of the TYPE, not of the instance.
///
/// core::measure() uses it to skip the O(n^2) self-intersection test, which at
/// kEllipseOutlineVertices costs about 131000 segment-pair comparisons to
/// answer a question that is structurally always false. A polygon's vertices
/// are the user's, so nothing can promise anything about them.
[[nodiscard]] bool isConvexByConstruction(const AnnotationGeometry& geom);

} // namespace slideio::viewer::core
