#include "slideio/viewer/core/ResultGeometry.h"

#include <cmath>
#include <cstddef>

namespace slideio::viewer::core
{

bool resultContains(const ResultObject& object, PointF point)
{
    bool inside = false;
    for (const ResultRing& ring : object.rings) {
        const auto& pts = ring.points;
        const size_t n = pts.size();
        if (n < 3) {
            continue;
        }
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const PointF& a = pts[i];
            const PointF& b = pts[j];
            if ((a.y > point.y) != (b.y > point.y)) {
                const double xCross = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
                if (point.x < xCross) {
                    inside = !inside;
                }
            }
        }
    }
    return inside;
}

std::vector<PointF> thinForDrawing(const std::vector<PointF>& ring, double toleranceSlideUnits)
{
    if (ring.size() < 3) {
        return ring;
    }
    std::vector<PointF> out;
    out.push_back(ring.front());
    for (size_t i = 1; i + 1 < ring.size(); ++i) {
        const PointF& last = out.back();
        if (std::hypot(ring[i].x - last.x, ring[i].y - last.y) >= toleranceSlideUnits) {
            out.push_back(ring[i]);
        }
    }
    out.push_back(ring.back());
    if (out.size() < 3) {
        out = {ring.front(), ring[ring.size() / 2], ring.back()};
    }
    return out;
}

} // namespace slideio::viewer::core
