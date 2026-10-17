#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <optional>

namespace slideio::viewer::core
{

/// Target size of an arrowhead along its shaft, in screen pixels.
///
/// In core rather than ui, unlike the screen-space tolerances in
/// AnnotationInteraction.h, because the function that reads it is here: core
/// cannot name a ui constant, and the arithmetic belongs where a test can
/// reach it. The disc radius goes the other way for the mirror-image reason --
/// its constant already exists in ui with other callers, so it is passed in.
constexpr double kArrowHeadScreenPixels = 12.0;

/// The most of a shaft's length an arrowhead may occupy.
///
/// The head's size is the SMALLER of the fixed size above and this fraction of
/// the shaft. Fixed alone means the head outgrows its shaft on a short arrow
/// or at low zoom; proportional alone means it disappears.
constexpr double kArrowHeadMaxShaftFraction = 0.5;

/// A glyph that must keep its size on SCREEN, for a shape whose slide-space
/// outline cannot express it.
///
/// Sizes only, never screen coordinates: `anchor` stays in slide units and the
/// painter converts it, because it already converts every other point. The
/// arrowhead's direction is likewise the painter's to derive, from the two
/// shaft points it has converted -- correct because the viewport only pans and
/// zooms, so it preserves direction.
struct ScreenDecoration
{
    enum class Kind
    {
        Disc,
        ArrowHead,
    };

    Kind kind;
    PointF anchor;      ///< slide units
    double sizePixels;  ///< screen pixels; see below for what it measures
};

/// The glyph `geom` needs, or nothing when it needs none.
///
/// `std::nullopt` for a rectangle, an ellipse and a polygon -- their outlines
/// say everything about them -- and for a `viewportScale` that is not
/// positive, consistent with every other screen-space function in this
/// codebase, which returns a safe value rather than dividing by a scale it
/// cannot use.
///
/// What `sizePixels` measures, stated once so the painter is determined:
///
///   * `Disc`: the RADIUS, and `anchor` is the centre -- the marker's own
///     position. `discRadiusScreenPixels` is returned as given (clamped at
///     zero), so the caller decides it; ViewportWidget passes the same value
///     it passes to the hit test, which is what makes the marker you see the
///     target you click, by construction rather than by coincidence.
///   * `ArrowHead`: the head's LENGTH ALONG THE SHAFT, and `anchor` is the
///     arrow's head point. The barbs are half that length to either side,
///     giving a head as wide as it is long.
///
/// outline() is untouched and still yields slide-space geometry: nothing here
/// makes a measurement depend on zoom.
[[nodiscard]] std::optional<ScreenDecoration> screenDecoration(const AnnotationGeometry& geom,
                                                               double viewportScale,
                                                               double discRadiusScreenPixels);

} // namespace slideio::viewer::core
