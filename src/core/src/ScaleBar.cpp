#include "slideio/viewer/core/ScaleBar.h"

#include <cmath>
#include <iterator>

namespace slideio::viewer::core
{

namespace
{

// 1/2/5 series in micrometers, monotonically increasing.
constexpr double kCandidateLengthsMicrons[] = {
    0.1, 0.2, 0.5,
    1.0, 2.0, 5.0,
    10.0, 20.0, 50.0,
    100.0, 200.0, 500.0,
    1000.0, 2000.0, 5000.0,
    10000.0, 20000.0, 50000.0,
    100000.0,
};

} // namespace

ScaleBarTick pickScaleBarLength(double micronsPerPixel, int maxBarPixels)
{
    if (micronsPerPixel <= 0.0) {
        return {0.0, 0};
    }

    // Walk candidates from largest to smallest; pick the first whose width fits.
    for (auto it = std::rbegin(kCandidateLengthsMicrons);
         it != std::rend(kCandidateLengthsMicrons); ++it) {
        const double widthPx = *it / micronsPerPixel;
        if (widthPx <= static_cast<double>(maxBarPixels)) {
            return { *it, static_cast<int>(std::round(widthPx)) };
        }
    }

    // Even the smallest candidate overflows maxBarPixels — return it anyway,
    // with its actual (overflowing) pixel width. Per spec, the caller decides
    // what to do; in practice this branch is unreachable for the zoom range
    // the magnification slider exposes.
    const double smallest = kCandidateLengthsMicrons[0];
    return { smallest, static_cast<int>(std::round(smallest / micronsPerPixel)) };
}

} // namespace slideio::viewer::core
