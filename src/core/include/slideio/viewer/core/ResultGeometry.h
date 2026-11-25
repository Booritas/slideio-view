#pragma once

#include "slideio/viewer/core/ResultsSource.h"

#include <vector>

namespace slideio::viewer::core
{

/// True when `point` is inside `object`: even-odd over all rings, so a hole
/// ring is a miss and a second outer ring is a hit.
[[nodiscard]] bool resultContains(const ResultObject& object, PointF point);

/// `ring` reduced for drawing: a point is kept when its distance to the last
/// KEPT point is at least the tolerance; the first and last points are always
/// kept. A ring of 3 or more points never comes back with fewer than 3 (the
/// first, a middle and the last input point).
[[nodiscard]] std::vector<PointF> thinForDrawing(const std::vector<PointF>& ring, double toleranceSlideUnits);

} // namespace slideio::viewer::core
