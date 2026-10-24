#include "slideio/viewer/core/DisplayRangePolicy.h"

namespace slideio::viewer::core
{

std::pair<double, double> dataTypeFullRange(DataType dt)
{
    switch (dt) {
        case DataType::Byte:    return {0.0, 255.0};
        case DataType::Int8:    return {-128.0, 127.0};
        case DataType::UInt16:  return {0.0, 65535.0};
        case DataType::Int16:   return {0.0, 32767.0};
        case DataType::UInt32:  return {0.0, 4294967295.0};
        case DataType::Int32:   return {0.0, 2147483647.0};
        case DataType::Int64:   return {-9223372036854775808.0, 9223372036854775807.0};
        case DataType::UInt64:  return {0.0, 18446744073709551615.0};
        case DataType::Float16: return {0.0, 1.0};
        case DataType::Float32: return {0.0, 1.0};
        case DataType::Float64: return {0.0, 1.0};
        default:                return {0.0, 255.0};
    }
}

bool rendersNativeRange(bool isBrightfield, int numChannels, DataType dt)
{
    return isBrightfield && numChannels == 3 && dt == DataType::Byte;
}

void applyNativeDisplayRange(SlideInfo& info)
{
    if (!rendersNativeRange(info.isBrightfield, info.numChannels, info.channelDataType)) {
        return;
    }
    const auto [fullMin, fullMax] = dataTypeFullRange(info.channelDataType);
    info.displayRange = DisplayRange{fullMin, fullMax, false};
    for (auto& channel : info.channels) {
        channel.displayRange = DisplayRange{fullMin, fullMax, false};
    }
}

} // namespace slideio::viewer::core
