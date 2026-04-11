#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace slideio::viewer::core
{

enum class DataType
{
    Byte,
    Int8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    Int64,
    UInt64,
    Float16,
    Float32,
    Float64,
    Unknown,
    None
};

inline size_t dataTypeSize(DataType dt)
{
    switch (dt) {
        case DataType::Byte:    return 1;
        case DataType::Int8:    return 1;
        case DataType::UInt16:  return 2;
        case DataType::Int16:   return 2;
        case DataType::UInt32:  return 4;
        case DataType::Int32:   return 4;
        case DataType::Int64:   return 8;
        case DataType::UInt64:  return 8;
        case DataType::Float16: return 2;
        case DataType::Float32: return 4;
        case DataType::Float64: return 8;
        case DataType::Unknown: return 0;
        case DataType::None:    return 0;
    }
    return 0;
}

struct DisplayRange
{
    double displayMin = 0.0;
    double displayMax = 255.0;
    bool autoDetected = false;
};

struct SlideInfo
{
    std::string filePath;
    int width = 0;
    int height = 0;
    int numChannels = 0;
    DataType channelDataType = DataType::None;
    int numZoomLevels = 0;
    double magnification = 0.0;
    double resolutionX = 0.0;
    double resolutionY = 0.0;
    std::string driverName;
    DisplayRange displayRange;
};

struct LevelInfo
{
    int level = 0;
    int width = 0;
    int height = 0;
    double scale = 1.0;
    double magnification = 0.0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tilesX = 0;
    int tilesY = 0;
};

template<typename T>
struct Rect
{
    T x = T{};
    T y = T{};
    T width = T{};
    T height = T{};
};

} // namespace slideio::viewer::core
