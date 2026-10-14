#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <optional>
#include <string>

namespace slideio::viewer::core
{

/// Physical size of one pixel, in metres along each axis. The default measures
/// in pixel units, which is what most tests use because the arithmetic stays
/// checkable by hand.
struct PixelSize
{
    double x = 1.0;
    double y = 1.0;
};

struct ShapeMetrics
{
    /// Absent for an open path, and for a closed path that intersects itself.
    /// Never a number the application cannot stand behind.
    std::optional<double> area;
    /// Perimeter for a closed shape, total length for an open one. `closed`
    /// says which word a caller should use.
    double outlineLength = 0.0;
    bool closed = false;
    /// Why `area` is missing, when it is. NOT a general statement that the
    /// shape is simple: an open path is never tested, because its area is
    /// absent regardless and open traces carry the most vertices.
    bool selfIntersecting = false;
};

/// Area and outline length of `geometry`, with each vertex scaled by
/// `pixelSize` BEFORE measuring.
///
/// The scaling order matters. An area scales by x*y, but a perimeter does not
/// scale by any single factor -- each segment scales differently with its
/// direction -- so measuring in pixels and multiplying afterwards is wrong on
/// any slide whose pixels are not square. Scaling the vertices first is the
/// only order that is correct for both.
///
/// A non-positive pixelSize component collapses that axis to zero rather than
/// producing NaN: resolutionX is 0 on an uncalibrated slide.
[[nodiscard]] ShapeMetrics measure(const AnnotationGeometry& geometry, PixelSize pixelSize);

/// As measure(), but with the self-intersection answer supplied rather than
/// computed.
///
/// Exists so a caller that already knows the answer -- because the shape has
/// only been translated since it last asked, and self-intersection is
/// translation-invariant -- can skip the O(n^2) test. The properties panel
/// needs this: it repopulates on every mouse-move of a drag.
///
/// The supplied answer is TRUSTED. Passing a wrong one produces an area that
/// disagrees with the painted region, so only a caller that can prove the
/// outline is unchanged may use this.
[[nodiscard]] ShapeMetrics measureKnown(const AnnotationGeometry& geometry, PixelSize pixelSize,
                                        bool selfIntersecting);

/// True when any two non-adjacent segments of `outline` cross, or when two
/// segments overlap along a line.
///
/// Takes an Outline rather than a geometry because outline() is already the one
/// place that turns any shape into points-plus-closed, and a rectangle's
/// outline is its *normalised* corners -- the same box hitTest uses. Deriving
/// the measurement, the painted region and the hit-tested region from one
/// source is what keeps them from disagreeing.
///
/// Segments meeting at a shared endpoint are NOT an intersection: consecutive
/// edges of every polygon touch, so counting that would make nothing simple.
/// Collinear overlap IS: a trace doubling back along itself has no
/// well-defined interior.
///
/// When `outline.closed` is true the closing edge is included in the test.
///
/// O(n^2) in the point count. Callers measuring long freehand traces should
/// note that an open path's area is absent regardless, so there is no reason to
/// ask this question about one.
[[nodiscard]] bool isSelfIntersecting(const Outline& outline);

/// Which unit system a formatter's input is in. The enum exists so a caller
/// cannot pass pixels where metres are expected and get a plausible wrong
/// answer.
enum class MeasurementUnits
{
    Physical, ///< metres, and square metres
    Pixels,   ///< pixels, and square pixels
};

/// `value` as a human-readable length. Physical input reads in microns below
/// one millimetre and millimetres at or above it; pixel input reads in px.
///
/// Three significant figures throughout, so the mantissa always reads 1-999 and
/// two people measuring the same shape write down the same number. The unit is
/// chosen AFTER rounding, so 999.6 um becomes "1.00 mm" rather than "1000 um".
///
/// Locale-free UTF-8: no thousands separators, and the micro sign spelled as
/// the two bytes the rest of this codebase already uses. Grouping and
/// translation are presentation decisions; this layer produces numbers, not
/// prose.
[[nodiscard]] std::string formatLength(double value, MeasurementUnits units);

/// `value` as a human-readable area, on the same rules: square microns below
/// one square millimetre, square millimetres at or above.
[[nodiscard]] std::string formatArea(double value, MeasurementUnits units);

} // namespace slideio::viewer::core
