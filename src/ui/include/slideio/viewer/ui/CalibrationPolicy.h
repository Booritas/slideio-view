#pragma once

#include "slideio/viewer/core/Calibration.h"

#include <optional>
#include <string>

namespace slideio::viewer::ui
{

/// The badge shown beside a measurement: empty for the scanner's own
/// calibration, "manual calibration" or "uncalibrated" otherwise. Text, not
/// colour alone, so it survives a screenshot pasted into a report.
[[nodiscard]] std::string calibrationBadgeText(core::CalibrationSource source);

/// What the scale bar appends to its length: " (manual)" for an override,
/// nothing otherwise. With no calibration the bar draws "Uncalibrated" instead
/// of a length, so it has nothing to append.
[[nodiscard]] std::string scaleBarSuffix(core::CalibrationSource source);

/// Left edge, in widget pixels, of a scale-bar label `textWidth` wide: centred
/// on the bar's `barCentre`, but moved to stay inside a widget `widgetWidth`
/// wide, and pinned to 0 when it cannot fit. A label that starts off the left
/// edge loses its leading digit -- "20000 um" reads as "0000 um", a scale wrong
/// by an order of magnitude -- and " (manual)" makes the label long enough to.
[[nodiscard]] int scaleBarLabelLeft(int barCentre, int textWidth, int widgetWidth);

/// What the Calibration dialog opens with.
struct CalibrationPrefill
{
    /// Microns per pixel, or nothing when there is neither an override nor
    /// metadata to start from.
    std::optional<core::PixelSize> value;
    /// Whether Y follows X. On unless the prefilled value has two different
    /// axes -- turning it on there would silently change Y on the first edit.
    bool squarePixels = true;
};

/// The current override, else the metadata (SlideInfo::resolutionX/Y, metres),
/// else nothing. The metadata is offered only when both axes are known, the
/// same rule core::effectiveCalibration applies.
[[nodiscard]] CalibrationPrefill calibrationPrefill(std::optional<core::PixelSize> currentOverride,
                                                    double metadataXMetres, double metadataYMetres);

/// Empty when (x, y) is a valid override; otherwise the message the dialog
/// shows, naming the range.
[[nodiscard]] std::string calibrationInputError(double x, double y);

} // namespace slideio::viewer::ui
