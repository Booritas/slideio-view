#pragma once

#include <cstddef>
#include <cstdint>

#include "slideio/viewer/core/Types.h"

namespace slideio::viewer::core
{

/// Bin a single channel of an interleaved pixel buffer into the supplied bin
/// array. The buffer is assumed to have `pixelCount` pixels, each storing
/// `numChannels` interleaved elements of `dataType`. Only the element at
/// `channelIndex` of every pixel is read. Values are mapped linearly from
/// `[rangeMin, rangeMax]` to `[0, numBins)` and clamped at the edges.
///
/// `bins` MUST point to `numBins` uint32_t cells; the function increments the
/// cells in place — it does NOT zero them, so callers should zero-initialize.
///
/// No-op if `pixelCount == 0`, `numBins <= 0`, or `rangeMax <= rangeMin`.
void computeHistogramStrided(const uint8_t* data, size_t pixelCount,
                             int numChannels, int channelIndex,
                             DataType dataType,
                             double rangeMin, double rangeMax,
                             uint32_t* bins, int numBins);

} // namespace slideio::viewer::core
