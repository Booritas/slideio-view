#include "slideio/viewer/core/Calibration.h"

#include <cmath>

namespace slideio::viewer::core
{

namespace
{

bool inRange(double micronsPerPixel)
{
    return std::isfinite(micronsPerPixel) && micronsPerPixel >= kMinCalibrationMicronsPerPixel
           && micronsPerPixel <= kMaxCalibrationMicronsPerPixel;
}

bool knownAxis(double metres)
{
    return std::isfinite(metres) && metres > 0.0;
}

} // namespace

bool isValidCalibrationOverride(PixelSize micronsPerPixel)
{
    return inRange(micronsPerPixel.x) && inRange(micronsPerPixel.y);
}

Calibration effectiveCalibration(double metadataXMetres, double metadataYMetres,
                                 std::optional<PixelSize> overrideMicrons)
{
    if (overrideMicrons.has_value() && isValidCalibrationOverride(*overrideMicrons)) {
        return Calibration{*overrideMicrons, CalibrationSource::Manual};
    }
    if (knownAxis(metadataXMetres) && knownAxis(metadataYMetres)) {
        return Calibration{PixelSize{metadataXMetres * 1e6, metadataYMetres * 1e6}, CalibrationSource::Metadata};
    }
    return Calibration{};
}

const char* calibrationSourceName(CalibrationSource source)
{
    switch (source) {
    case CalibrationSource::Metadata:
        return "metadata";
    case CalibrationSource::Manual:
        return "manual";
    case CalibrationSource::None:
        break;
    }
    return "none";
}

} // namespace slideio::viewer::core
