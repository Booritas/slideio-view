#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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

struct ChannelInfo
{
    std::string name;
    DataType dataType = DataType::None;
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float intensity = 1.0f; // 0.0–1.0 brightness multiplier
    bool visible = true;
    DisplayRange displayRange;
};

/// Assign a default pseudo-color for fluorescence channels.
/// Matches known dye names first, then falls back to index-based defaults.
inline void assignDefaultFluorescenceColor(ChannelInfo& ch, int channelIndex)
{
    struct DefaultColor { float r, g, b; };
    static const DefaultColor kIndexDefaults[] = {
        {0.0f, 0.4f, 1.0f},  // blue (DAPI-like)
        {0.0f, 1.0f, 0.0f},  // green (FITC-like)
        {1.0f, 0.0f, 0.0f},  // red (Cy3-like)
        {1.0f, 0.0f, 1.0f},  // magenta (Cy5-like)
        {0.0f, 1.0f, 1.0f},  // cyan
        {1.0f, 1.0f, 0.0f},  // yellow
        {1.0f, 1.0f, 1.0f},  // white
    };

    // Name-based matching (case-insensitive prefix)
    auto nameUpper = ch.name;
    std::transform(nameUpper.begin(), nameUpper.end(), nameUpper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (nameUpper.find("DAPI") != std::string::npos) {
        ch.colorR = 0.0f; ch.colorG = 0.4f; ch.colorB = 1.0f; return;
    }
    if (nameUpper.find("FITC") != std::string::npos ||
        nameUpper.find("GFP") != std::string::npos ||
        nameUpper.find("ALEXA488") != std::string::npos ||
        nameUpper.find("ALEXA 488") != std::string::npos) {
        ch.colorR = 0.0f; ch.colorG = 1.0f; ch.colorB = 0.0f; return;
    }
    if (nameUpper.find("CY3") != std::string::npos ||
        nameUpper.find("TRITC") != std::string::npos ||
        nameUpper.find("ALEXA555") != std::string::npos ||
        nameUpper.find("ALEXA 555") != std::string::npos) {
        ch.colorR = 1.0f; ch.colorG = 0.0f; ch.colorB = 0.0f; return;
    }
    if (nameUpper.find("CY5") != std::string::npos ||
        nameUpper.find("ALEXA647") != std::string::npos ||
        nameUpper.find("ALEXA 647") != std::string::npos) {
        ch.colorR = 1.0f; ch.colorG = 0.0f; ch.colorB = 1.0f; return;
    }

    // Fallback to index-based defaults
    int idx = channelIndex % 7;
    ch.colorR = kIndexDefaults[idx].r;
    ch.colorG = kIndexDefaults[idx].g;
    ch.colorB = kIndexDefaults[idx].b;
}

/// Assign default channel colors for brightfield RGB images.
/// Channel 0=Red, 1=Green, 2=Blue (matching typical RGB interleaving).
inline void assignDefaultBrightfieldColor(ChannelInfo& ch, int channelIndex)
{
    switch (channelIndex) {
    case 0: ch.colorR = 1.0f; ch.colorG = 0.0f; ch.colorB = 0.0f; break; // Red
    case 1: ch.colorR = 0.0f; ch.colorG = 1.0f; ch.colorB = 0.0f; break; // Green
    case 2: ch.colorR = 0.0f; ch.colorG = 0.0f; ch.colorB = 1.0f; break; // Blue
    default: ch.colorR = 1.0f; ch.colorG = 1.0f; ch.colorB = 1.0f; break; // White
    }
}

struct SceneInfo
{
    int index = -1;             // scene index (0..N-1); -1 for auxiliary images
    std::string name;
    int width = 0;
    int height = 0;
    int numChannels = 0;
    bool isAuxiliary = false;   // true for label/macro images
    std::string auxiliaryName;  // SlideIO aux image name (for retrieval)
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
    std::vector<ChannelInfo> channels;
    bool isBrightfield = false;
    std::vector<SceneInfo> scenes;
    std::vector<SceneInfo> auxImages;
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
