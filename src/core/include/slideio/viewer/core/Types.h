#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "slideio/viewer/core/Metadata.h"

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

/// Number of bins used for per-channel histograms. Byte data gets exactly
/// one bin per code; higher-precision data is quantized.
constexpr int kNumBins = 256;

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

struct ChannelHistogram
{
    std::vector<uint32_t> bins;    // size == kNumBins when valid; empty otherwise
    double rangeMin = 0.0;         // pixel value mapped to bins[0]
    double rangeMax = 0.0;         // pixel value mapped to bins.back()
    uint64_t totalSamples = 0;     // sum of bins[]; used to guard log-axis scaling
    bool valid = false;            // false if computation failed or was skipped
};

struct ChannelInfo
{
    std::string name;
    DataType dataType = DataType::None;
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float intensity = 1.0f; // 0.0–4.0 brightness multiplier (1.0 = unity gain)
    bool visible = true;
    DisplayRange displayRange;
    DisplayRange autoDisplayRange;   // frozen snapshot of autodetect result; "Auto" button restores from this.
                                     // INVARIANT: autoDisplayRange.autoDetected is true iff autodetect produced a usable range for this channel.
    ChannelHistogram histogram;      // populated once at slide open
    bool userOverrideRange = false;  // true when user has edited min/max or clicked Reset
};

/// Map a visible-spectrum wavelength (nm) to an sRGB color via Bruton's
/// piecewise-linear approximation. Returns false (and leaves outputs untouched)
/// for wavelengths outside 380–780 nm. Outputs saturated colors — no
/// brightness falloff at spectrum edges — because callers want a hue cue,
/// not a perceptual luminance.
inline bool wavelengthToSrgb(double nm, float& r, float& g, float& b)
{
    if (nm < 380.0 || nm > 780.0) return false;
    if (nm < 440.0) {
        r = static_cast<float>(-(nm - 440.0) / (440.0 - 380.0));
        g = 0.0f;
        b = 1.0f;
    } else if (nm < 490.0) {
        r = 0.0f;
        g = static_cast<float>((nm - 440.0) / (490.0 - 440.0));
        b = 1.0f;
    } else if (nm < 510.0) {
        r = 0.0f;
        g = 1.0f;
        b = static_cast<float>(-(nm - 510.0) / (510.0 - 490.0));
    } else if (nm < 580.0) {
        r = static_cast<float>((nm - 510.0) / (580.0 - 510.0));
        g = 1.0f;
        b = 0.0f;
    } else if (nm < 645.0) {
        r = 1.0f;
        g = static_cast<float>(-(nm - 645.0) / (645.0 - 580.0));
        b = 0.0f;
    } else { // 645–780 nm
        r = 1.0f;
        g = 0.0f;
        b = 0.0f;
    }
    return true;
}

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
/// Single-channel brightfield is grayscale, so its lone channel is tinted white.
inline void assignDefaultBrightfieldColor(ChannelInfo& ch, int channelIndex, int numChannels)
{
    if (numChannels <= 1) {
        ch.colorR = 1.0f; ch.colorG = 1.0f; ch.colorB = 1.0f;
        return;
    }
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

/// Where a scene's ICC colour profile came from. Mirrors slideio::ColorProfileSource.
enum class ColorProfileSource
{
    None,       ///< the file carries no profile
    Embedded,   ///< a real ICC profile read out of the file
    Assumed,    ///< none embedded; sRGB substituted by SlideIO
    Supplied,   ///< handed to SlideIO by the caller, not found in the file
};

/// Colour space of ICC profile data. Mirrors slideio::IccColorSpace, and is
/// unrelated to the channel colours above.
enum class IccColorSpace { Unknown, Gray, RGB, CMYK, Lab, XYZ, YCbCr };

/// ICC rendering intent. Mirrors slideio::RenderingIntent.
enum class RenderingIntent { Perceptual, RelativeColorimetric, Saturation, AbsoluteColorimetric };

/// Why colour management is or is not offered for a slide.
enum class ColorManagementAvailability
{
    Available,
    /// Not three channels of Byte/UInt16, or the channels are fluorescence.
    NotColorimetric,
    /// Colorimetric, but the slide embeds no profile and no default is configured.
    NoProfile,
    /// SlideIO refused the wrap; the accompanying detail carries its message.
    BindFailed,
};

/// Parsed header of a scene's ICC colour profile.
///
/// A mirror of slideio::ColorProfileInfo rather than a reuse of it: that type is
/// exported from SlideIO::core, and this library links nothing. Translation
/// happens in the infrastructure layer, where the SlideIO headers already live.
///
/// `present` gates every other field. It is false both when the scene carries no
/// profile and when it carries bytes that would not parse — and in the second
/// case `source` still reads Embedded, because it is copied from the tag the
/// driver found, not derived from a successful parse. So `present == false`
/// means "no usable colorimetry", whatever `source` says.
struct ColorProfileInfo
{
    bool present = false;
    ColorProfileSource source = ColorProfileSource::None;
    std::string description;
    std::string manufacturer;
    std::string model;
    std::string version;
    IccColorSpace dataSpace = IccColorSpace::Unknown;
    IccColorSpace connectionSpace = IccColorSpace::Unknown;
    RenderingIntent intent = RenderingIntent::RelativeColorimetric;
    size_t dataSize = 0;
};

/// One-line description of a scene's colour profile, for the properties panel:
/// what the profile calls itself, and whether the slide actually carries it.
/// Returns a "not color-managed" line when there is no usable colorimetry.
std::string colorProfileSummary(const ColorProfileInfo& info);

/// Display names for the colour-profile enums, for the properties panel rows.
const char* colorProfileSourceName(ColorProfileSource source);
const char* iccColorSpaceName(IccColorSpace space);
const char* renderingIntentName(RenderingIntent intent);

struct SlideInfo
{
    std::string filePath;
    int width = 0;
    int height = 0;
    int numChannels = 0;
    DataType channelDataType = DataType::None;
    int numZoomLevels = 0;
    double magnification = 0.0;
    double resolutionX = 0.0;   // pixel size in meters along X
    double resolutionY = 0.0;   // pixel size in meters along Y
    std::string driverName;
    std::string driverId;       // SlideIO driver id used to open this slide
    std::string compression;    // human-readable compression name (e.g., "Jpeg")
    DisplayRange displayRange;
    std::vector<ChannelInfo> channels;
    bool isBrightfield = false;
    // False when the slide has no downsampled level cheap enough to build a
    // whole-slide overview from. Rendering one would mean decoding every tile
    // of the full-resolution level, so the minimap says so instead.
    bool overviewAvailable = true;
    int numZSlices = 1;
    int numTFrames = 1;
    std::vector<SceneInfo> scenes;
    std::vector<SceneInfo> auxImages;
    std::vector<LevelInfo> levels;
    ColorProfileInfo colorProfileInfo; // populated from slideio::Scene::getColorProfileInfo()
    ColorManagementAvailability colorManagement = ColorManagementAvailability::NotColorimetric;
    std::string colorManagementDetail;  // SlideIO's message when colorManagement == BindFailed
    // Surfaced because isBrightfield is the wrong gate for ICC conversion in both
    // directions: it is false for 16-bit RGB brightfield, which ColorManagement
    // supports, and this hint is what separates 3x8-bit fluorescence from brightfield.
    bool fluorescenceHint = false;
    MetadataNode slideMetadata;   // populated from slideio::Slide::getMetadata()
    MetadataNode sceneMetadata;   // populated from slideio::Scene::getMetadata()
    MetadataNode channelMetadata; // populated from slideio::Scene::getChannelAttributes()
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
