#pragma once

#include "slideio/viewer/core/Types.h"

#include <variant>
#include <vector>

namespace slideio::viewer::core
{

struct RectangleGeometry
{
    PointF topLeft;
    PointF bottomRight;
};

/// Named kind of a shape. Derived from the geometry rather than stored
/// alongside it, so the two cannot disagree -- see typeOf() below.
enum class AnnotationType
{
    Rectangle,
};

/// The variant starts with one alternative and widens as drawing tools land.
/// std::visit dispatch in the free functions below is identical at one
/// alternative or six, so the structure is established now without writing
/// nine geometry types against a transform not yet proven on a real slide.
using AnnotationGeometry = std::variant<RectangleGeometry>;

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

} // namespace slideio::viewer::core
