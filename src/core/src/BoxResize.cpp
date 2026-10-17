#include "slideio/viewer/core/BoxResize.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::core
{

std::array<PointF, 8> boxHandlePositions(const RectF& box)
{
    const double left = box.x;
    const double right = box.x + box.width;
    const double top = box.y;
    const double bottom = box.y + box.height;
    const double midX = box.x + box.width * 0.5;
    const double midY = box.y + box.height * 0.5;

    // Enumerator order: clockwise from the top-left corner.
    return {PointF{left, top},    PointF{midX, top},     PointF{right, top},
            PointF{right, midY},  PointF{right, bottom}, PointF{midX, bottom},
            PointF{left, bottom}, PointF{left, midY}};
}

std::optional<BoxHandle> nearestBoxHandle(const RectF& box, PointF point,
                                          double toleranceSlideUnits)
{
    const double tolerance = std::max(0.0, toleranceSlideUnits);
    const std::array<PointF, 8> handles = boxHandlePositions(box);

    std::optional<BoxHandle> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const double distance = std::hypot(point.x - handles[i].x, point.y - handles[i].y);
        if (distance > tolerance) {
            continue;
        }
        // Strictly nearer, so a tie keeps the earlier enumerator and a
        // degenerate box -- where all eight coincide -- answers TopLeft rather
        // than whichever the loop happened to reach last.
        if (!best.has_value() || distance < bestDistance) {
            best = static_cast<BoxHandle>(i);
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace slideio::viewer::core
