#pragma once

#include "slideio/viewer/core/Types.h"

#include <variant>

namespace slideio::viewer::core
{

struct RectangleGeometry
{
    PointF topLeft;
    PointF bottomRight;
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

} // namespace slideio::viewer::core
