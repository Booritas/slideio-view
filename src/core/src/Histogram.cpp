#include "slideio/viewer/core/Histogram.h"

#include <cstdint>

namespace slideio::viewer::core
{

namespace
{

template <typename T>
void scanHistogramStrided(const void* dataPtr, size_t pixelCount,
                          int numChannels, int channelIndex,
                          double rangeMin, double rangeMax,
                          uint32_t* bins, int numBins)
{
    const T* typed = static_cast<const T*>(dataPtr);
    const double span = rangeMax - rangeMin;
    const double scale = static_cast<double>(numBins) / span;
    const size_t stride = static_cast<size_t>(numChannels);
    const size_t offset = static_cast<size_t>(channelIndex);

    for (size_t i = 0; i < pixelCount; ++i) {
        const T raw = typed[i * stride + offset];
        const double v = static_cast<double>(raw);
        int idx = static_cast<int>((v - rangeMin) * scale);
        if (idx < 0) idx = 0;
        else if (idx >= numBins) idx = numBins - 1;
        bins[idx]++;
    }
}

} // namespace

void computeHistogramStrided(const uint8_t* data, size_t pixelCount,
                             int numChannels, int channelIndex,
                             DataType dataType,
                             double rangeMin, double rangeMax,
                             uint32_t* bins, int numBins)
{
    if (pixelCount == 0 || numBins <= 0 || rangeMax <= rangeMin) {
        return;
    }
    if (bins == nullptr || data == nullptr) {
        return;
    }
    if (channelIndex < 0 || channelIndex >= numChannels) {
        return;
    }

    switch (dataType) {
        case DataType::Byte:
            scanHistogramStrided<uint8_t>(data, pixelCount, numChannels, channelIndex,
                                          rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Int8:
            scanHistogramStrided<int8_t>(data, pixelCount, numChannels, channelIndex,
                                         rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::UInt16:
            scanHistogramStrided<uint16_t>(data, pixelCount, numChannels, channelIndex,
                                           rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Int16:
            scanHistogramStrided<int16_t>(data, pixelCount, numChannels, channelIndex,
                                          rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::UInt32:
            scanHistogramStrided<uint32_t>(data, pixelCount, numChannels, channelIndex,
                                           rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Int32:
            scanHistogramStrided<int32_t>(data, pixelCount, numChannels, channelIndex,
                                          rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Float32:
            scanHistogramStrided<float>(data, pixelCount, numChannels, channelIndex,
                                        rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Float64:
            scanHistogramStrided<double>(data, pixelCount, numChannels, channelIndex,
                                         rangeMin, rangeMax, bins, numBins);
            break;
        default:
            break;
    }
}

} // namespace slideio::viewer::core
