#include "slideio/viewer/core/AnnotationImport.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace slideio::viewer::core
{

AnnotationFileFormat detectAnnotationFileFormat(std::string_view text)
{
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (text.substr(0, kBom.size()) == kBom) {
        text.remove_prefix(kBom.size());
    }
    // ASCII whitespace compared by value: std::isspace reads the C locale.
    const std::size_t first = text.find_first_not_of(" \t\r\n\f\v");
    return first != std::string_view::npos && text[first] == '<' ? AnnotationFileFormat::AsapXml
                                                                 : AnnotationFileFormat::GeoJson;
}

std::pair<PointF, PointF> cornersOf(const std::vector<PointF>& points)
{
    PointF low = points.front();
    PointF high = low;
    for (const PointF& p : points) {
        low.x = std::min(low.x, p.x);
        low.y = std::min(low.y, p.y);
        high.x = std::max(high.x, p.x);
        high.y = std::max(high.y, p.y);
    }
    return {low, high};
}

bool hasFiniteExtent(const std::vector<PointF>& points)
{
    if (points.empty()) {
        return true;
    }
    const auto [low, high] = cornersOf(points);
    return std::isfinite(high.x - low.x) && std::isfinite(high.y - low.y);
}

bool isAxisAlignedBox(const std::vector<PointF>& ring)
{
    if (ring.size() != 4) {
        return false;
    }
    std::set<double> xs;
    std::set<double> ys;
    for (std::size_t i = 0; i < 4; ++i) {
        const PointF& a = ring[i];
        const PointF& b = ring[(i + 1) % 4];
        if (a.x != b.x && a.y != b.y) {
            return false;
        }
        for (std::size_t j = i + 1; j < 4; ++j) {
            if (ring[i].x == ring[j].x && ring[i].y == ring[j].y) {
                return false;
            }
        }
        xs.insert(a.x);
        ys.insert(a.y);
    }
    return xs.size() == 2 && ys.size() == 2;
}

AnnotationGeometry closedAreaFrom(std::vector<PointF> ring)
{
    if (isAxisAlignedBox(ring)) {
        const auto [low, high] = cornersOf(ring);
        return RectangleGeometry{low, high};
    }
    return PolygonGeometry{std::move(ring), true};
}

} // namespace slideio::viewer::core
