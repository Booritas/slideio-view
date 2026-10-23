#include "slideio/viewer/core/AnnotationImport.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace slideio::viewer::core
{

AnnotationFileFormat detectAnnotationFileFormat(std::string_view text)
{
    // UTF-16, by its byte-order mark or by '<' beside a zero byte. The BOM is
    // checked before the '<': a UTF-16 GeoJSON file is not XML, and the GeoJSON
    // reader is the one to say it cannot read it.
    const auto startsWith = [&text](std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; };
    if (startsWith("\xFF\xFE") || startsWith("\xFE\xFF")) {
        const std::string_view rest = text.substr(2);
        const bool less =
            rest.size() >= 2 && ((rest[0] == '<' && rest[1] == '\0') || (rest[0] == '\0' && rest[1] == '<'));
        return less ? AnnotationFileFormat::AsapXml : AnnotationFileFormat::GeoJson;
    }
    if (startsWith(std::string_view("<\0", 2)) || startsWith(std::string_view("\0<", 2))) {
        return AnnotationFileFormat::AsapXml;
    }
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (startsWith(kBom)) {
        text.remove_prefix(kBom.size());
    }
    // ASCII whitespace compared by value: std::isspace reads the C locale.
    const std::size_t first = text.find_first_not_of(" \t\r\n\f\v");
    return first != std::string_view::npos && text[first] == '<' ? AnnotationFileFormat::AsapXml
                                                                 : AnnotationFileFormat::GeoJson;
}

AnnotationImportResult importFailure(std::string message)
{
    AnnotationImportResult result;
    result.error = std::move(message);
    return result;
}

AnnotationImportResult cancelledImport()
{
    AnnotationImportResult result;
    result.cancelled = true;
    return result;
}

bool importCancelRequested(const std::atomic<bool>* cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
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
