#include "slideio/viewer/ui/CalibrationPolicy.h"

namespace slideio::viewer::ui
{

std::string calibrationBadgeText(core::CalibrationSource source)
{
    switch (source) {
    case core::CalibrationSource::Manual:
        return "manual calibration";
    case core::CalibrationSource::None:
        return "uncalibrated";
    case core::CalibrationSource::Metadata:
        break;
    }
    return {};
}

std::string scaleBarSuffix(core::CalibrationSource source)
{
    return source == core::CalibrationSource::Manual ? " (manual)" : "";
}

CalibrationPrefill calibrationPrefill(std::optional<core::PixelSize> currentOverride, double metadataXMetres,
                                      double metadataYMetres)
{
    const core::Calibration calibration =
        core::effectiveCalibration(metadataXMetres, metadataYMetres, currentOverride);
    if (calibration.source == core::CalibrationSource::None) {
        return {};
    }
    CalibrationPrefill prefill;
    prefill.value = calibration.micronsPerPixel;
    prefill.squarePixels = calibration.micronsPerPixel.x == calibration.micronsPerPixel.y;
    return prefill;
}

std::string calibrationInputError(double x, double y)
{
    if (core::isValidCalibrationOverride(core::PixelSize{x, y})) {
        return {};
    }
    // \xC2\xB5 is the micro sign: MSVC compiles this file without /utf-8.
    return "Enter a pixel size between 0.01 and 100 \xC2\xB5m per pixel.";
}

} // namespace slideio::viewer::ui
