#include "slideio/viewer/core/AnnotationDecoration.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

// An overload set visited by std::visit, the same shape as every other
// dispatch over this variant, so a shape added later has to answer here too
// rather than falling silently through to "no glyph".
std::optional<ScreenDecoration> decorationFor(const RectangleGeometry&, double, double)
{
    return std::nullopt;
}

std::optional<ScreenDecoration> decorationFor(const EllipseGeometry&, double, double)
{
    return std::nullopt;
}

std::optional<ScreenDecoration> decorationFor(const PolygonGeometry&, double, double)
{
    return std::nullopt;
}

std::optional<ScreenDecoration> decorationFor(const PointGeometry& marker, double,
                                              double discRadiusScreenPixels)
{
    // The radius is the caller's, returned as given. That is the whole point
    // of it being a parameter: ViewportWidget passes the same value it passes
    // to the hit test, so the marker and its grab target cannot drift apart.
    return ScreenDecoration{ScreenDecoration::Kind::Disc, marker.position,
                            std::max(0.0, discRadiusScreenPixels)};
}

std::optional<ScreenDecoration> decorationFor(const ArrowGeometry& arrow, double viewportScale,
                                              double)
{
    const double shaftScreenPixels =
        std::hypot(arrow.head.x - arrow.tail.x, arrow.head.y - arrow.tail.y) * viewportScale;
    // The smaller of the two rules. Fixed alone outgrows its shaft on a short
    // arrow or at low zoom; proportional alone disappears. A zero-length arrow
    // -- which the tool refuses but the parser accepts from disk -- gives 0
    // here rather than a direction that cannot be normalised.
    const double size =
        std::min(kArrowHeadScreenPixels, shaftScreenPixels * kArrowHeadMaxShaftFraction);
    return ScreenDecoration{ScreenDecoration::Kind::ArrowHead, arrow.head, std::max(0.0, size)};
}

} // namespace

std::optional<ScreenDecoration> screenDecoration(const AnnotationGeometry& geom,
                                                 double viewportScale,
                                                 double discRadiusScreenPixels)
{
    // Written as the negation of "is positive" rather than as "<= 0" so that a
    // NaN scale is refused too: NaN fails every comparison, and `NaN <= 0.0`
    // is false, which would let it through to multiply the shaft length into a
    // NaN size the painter cannot use.
    if (!(viewportScale > 0.0)) {
        return std::nullopt;
    }
    return std::visit(
        [&](const auto& shape) {
            return decorationFor(shape, viewportScale, discRadiusScreenPixels);
        },
        geom);
}

} // namespace slideio::viewer::core
