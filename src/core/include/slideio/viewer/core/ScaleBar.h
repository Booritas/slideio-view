#pragma once

namespace slideio::viewer::core
{

struct ScaleBarTick
{
    double lengthMicrons = 0.0;  // 0.0 when input is invalid (caller hides bar)
    int    pixelWidth    = 0;    // matching screen pixels, rounded
};

/// Pick the largest "nice" length (1/2/5 series) whose screen width does not
/// exceed maxBarPixels. Returns {0, 0} only when micronsPerPixel <= 0.
/// If even the smallest candidate (0.1 um) overflows maxBarPixels, that
/// smallest candidate is still returned and pixelWidth may exceed maxBarPixels —
/// callers may clamp visually as they see fit.
ScaleBarTick pickScaleBarLength(double micronsPerPixel, int maxBarPixels);

} // namespace slideio::viewer::core
