#include "slideio/viewer/ui/ViewportWidget.h"
#include "slideio/viewer/ui/PathDisplay.h"
#include "slideio/viewer/ui/ViewportController.h"

#include "slideio/viewer/core/CoordinateSystem.h"
#include "slideio/viewer/core/Histogram.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/TileSampling.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/infra/LruTileCache.h"
#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/infra/TileLoadScheduler.h"

#include <QImage>
#include <QMetaObject>
#include <QMouseEvent>
#include <QTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QWheelEvent>

#include <mutex>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slideio::viewer::ui
{
// Result of a background slide-open operation. Holds everything the UI thread
// needs to install a freshly-opened slide. Movable but not copyable for safety.
struct SceneOpenResult
{
    bool success = false;
    std::string filePath;
    std::string errorMsg;
    bool isAuxImage = false;          // true when opened via openAuxImage path

    // Scene/aux enumeration (only filled by openSlide path)
    std::vector<core::SceneInfo> scenes;
    std::vector<core::SceneInfo> auxImages;

    // Core data structures built off the UI thread
    std::shared_ptr<core::ISlideSource> slideSource;
    core::SlideInfo slideInfo;
    std::shared_ptr<core::TilePyramid> pyramid;
    std::shared_ptr<infra::LruTileCache> tileCache;
    QImage thumbnail;
};
} // namespace slideio::viewer::ui

namespace
{

const char* kTileVertexShaderSource = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec4 uScreenRect;
uniform vec2 uViewportSize;
uniform vec2 uTexCoordOffset;  // (0,0) for normal tiles, sub-region offset for fallback
uniform vec2 uTexCoordScale;   // (1,1) for normal tiles, sub-region scale for fallback

out vec2 vTexCoord;

void main()
{
    float screenX = uScreenRect.x + aPos.x * uScreenRect.z;
    float screenY = uScreenRect.y + aPos.y * uScreenRect.w;

    float ndcX = (screenX / uViewportSize.x) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / uViewportSize.y) * 2.0;

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);

    vTexCoord = uTexCoordOffset + vec2(aPos.x, aPos.y) * uTexCoordScale;
}
)glsl";

const char* kTileFragmentShaderSource = R"glsl(
#version 330 core

in vec2 vTexCoord;

uniform sampler2D uTileTexture;
uniform float uAlpha;
uniform float uDisplayMin;
uniform float uDisplayMax;
uniform vec3 uChannelColor;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(uTileTexture, vTexCoord);
    float range = uDisplayMax - uDisplayMin;
    float invRange = (range > 0.0) ? (1.0 / range) : 1.0;
    vec3 mapped = clamp((texel.rgb - uDisplayMin) * invRange, 0.0, 1.0);
    fragColor = vec4(mapped * uChannelColor, texel.a * uAlpha);
}
)glsl";

const char* kChannelFragmentShaderSource = R"glsl(
#version 330 core

in vec2 vTexCoord;

uniform sampler2D uTileTexture;
uniform float uDisplayMin;
uniform float uDisplayMax;
uniform vec3 uChannelColor;

out vec4 fragColor;

void main()
{
    float value = texture(uTileTexture, vTexCoord).r;
    float range = uDisplayMax - uDisplayMin;
    float invRange = (range > 0.0) ? (1.0 / range) : 1.0;
    float mapped = clamp((value - uDisplayMin) * invRange, 0.0, 1.0);
    fragColor = vec4(uChannelColor * mapped, 1.0);
}
)glsl";

const char* kSnapshotFragmentShaderSource = R"glsl(
#version 330 core

in vec2 vTexCoord;

uniform sampler2D uSnapshotTexture;

out vec4 fragColor;

void main()
{
    fragColor = texture(uSnapshotTexture, vTexCoord);
}
)glsl";

// Composite shader: passthrough with per-pixel alpha preserved so that
// transparent regions of the source FBO let the underlying snapshot show through.
const char* kCompositeFragmentShaderSource = R"glsl(
#version 330 core

in vec2 vTexCoord;

uniform sampler2D uSourceTexture;

out vec4 fragColor;

void main()
{
    fragColor = texture(uSourceTexture, vTexCoord);
}
)glsl";

using DataType = slideio::viewer::core::DataType;

constexpr int kMaxTextureUploadsPerFrame = 8;
constexpr double kZoomInFactor = 1.25;
constexpr double kZoomOutFactor = 1.0 / 1.25;
constexpr double kPanPixels = 20.0;
constexpr double kWheelZoomFactor = 1.1;

// --- Data type support helpers ---

struct GlTextureFormat
{
    GLenum internalFormat;
    GLenum format;
    GLenum type;
    bool requiresCpuConversion;
};

GlTextureFormat glTextureFormatForDataType(DataType dataType, int numChannels)
{
    // Select base internal format and type per data type
    GLenum baseInternal1 = GL_R8;
    GLenum baseInternal3 = GL_RGB8;
    GLenum glType = GL_UNSIGNED_BYTE;
    bool cpuConvert = false;

    switch (dataType) {
    case DataType::Byte:
        baseInternal1 = GL_R8;       baseInternal3 = GL_RGB8;       glType = GL_UNSIGNED_BYTE;    break;
    case DataType::Int8:
        baseInternal1 = GL_R8_SNORM; baseInternal3 = GL_RGB8_SNORM; glType = GL_BYTE;             break;
    case DataType::UInt16:
        baseInternal1 = GL_R16;      baseInternal3 = GL_RGB16;      glType = GL_UNSIGNED_SHORT;   break;
    case DataType::Int16:
        baseInternal1 = GL_R16_SNORM;baseInternal3 = GL_RGB16_SNORM;glType = GL_SHORT;            break;
    case DataType::Float32:
        baseInternal1 = GL_R32F;     baseInternal3 = GL_RGB32F;     glType = GL_FLOAT;            break;
    case DataType::Float16:
        baseInternal1 = GL_R16F;     baseInternal3 = GL_RGB16F;     glType = GL_HALF_FLOAT;       break;
    case DataType::UInt32:
    case DataType::Int32:
    case DataType::Int64:
    case DataType::UInt64:
    case DataType::Float64:
        baseInternal1 = GL_R32F;     baseInternal3 = GL_RGB32F;     glType = GL_FLOAT;
        cpuConvert = true;
        break;
    default:
        break;
    }

    // Select format and internal format based on channel count
    GLenum format = GL_RGB;
    GLenum internalFormat = baseInternal3;
    if (numChannels == 1) {
        format = GL_RED;
        internalFormat = baseInternal1;
    } else if (numChannels == 2) {
        format = GL_RG;
        // Derive 2-channel internal format from 1-channel by pattern
        // GL_R8 -> GL_RG8, GL_R16 -> GL_RG16, GL_R32F -> GL_RG32F, etc.
        if (baseInternal1 == GL_R8)        internalFormat = GL_RG8;
        else if (baseInternal1 == GL_R8_SNORM)  internalFormat = GL_RG8_SNORM;
        else if (baseInternal1 == GL_R16)       internalFormat = GL_RG16;
        else if (baseInternal1 == GL_R16_SNORM) internalFormat = GL_RG16_SNORM;
        else if (baseInternal1 == GL_R16F)      internalFormat = GL_RG16F;
        else if (baseInternal1 == GL_R32F)      internalFormat = GL_RG32F;
        else internalFormat = GL_RG8;
    } else if (numChannels == 4) {
        format = GL_RGBA;
        if (baseInternal3 == GL_RGB8)        internalFormat = GL_RGBA8;
        else if (baseInternal3 == GL_RGB8_SNORM)  internalFormat = GL_RGBA8_SNORM;
        else if (baseInternal3 == GL_RGB16)       internalFormat = GL_RGBA16;
        else if (baseInternal3 == GL_RGB16_SNORM) internalFormat = GL_RGBA16_SNORM;
        else if (baseInternal3 == GL_RGB16F)      internalFormat = GL_RGBA16F;
        else if (baseInternal3 == GL_RGB32F)      internalFormat = GL_RGBA32F;
        else internalFormat = GL_RGBA8;
    }

    return {internalFormat, format, glType, cpuConvert};
}

std::vector<uint8_t> convertBufferToFloat32(const uint8_t* src, size_t pixelCount,
                                             DataType srcType)
{
    std::vector<uint8_t> result(pixelCount * sizeof(float));
    float* dst = reinterpret_cast<float*>(result.data());

    switch (srcType) {
    case DataType::UInt32: {
        const uint32_t* s = reinterpret_cast<const uint32_t*>(src);
        for (size_t i = 0; i < pixelCount; ++i) dst[i] = static_cast<float>(s[i]);
        break;
    }
    case DataType::Int32: {
        const int32_t* s = reinterpret_cast<const int32_t*>(src);
        for (size_t i = 0; i < pixelCount; ++i) dst[i] = static_cast<float>(s[i]);
        break;
    }
    case DataType::Int64: {
        const int64_t* s = reinterpret_cast<const int64_t*>(src);
        for (size_t i = 0; i < pixelCount; ++i) dst[i] = static_cast<float>(s[i]);
        break;
    }
    case DataType::UInt64: {
        const uint64_t* s = reinterpret_cast<const uint64_t*>(src);
        for (size_t i = 0; i < pixelCount; ++i) dst[i] = static_cast<float>(s[i]);
        break;
    }
    case DataType::Float64: {
        const double* s = reinterpret_cast<const double*>(src);
        for (size_t i = 0; i < pixelCount; ++i) dst[i] = static_cast<float>(s[i]);
        break;
    }
    default:
        break;
    }
    return result;
}

// Convert raw pixel-space min/max to the range that the GL sampler outputs
// after normalized texture format upload.
std::pair<float, float> toSamplerRange(double rawMin, double rawMax, DataType dataType)
{
    switch (dataType) {
    case DataType::Byte:
        return {static_cast<float>(rawMin / 255.0), static_cast<float>(rawMax / 255.0)};
    case DataType::Int8:
        return {static_cast<float>(rawMin / 127.0), static_cast<float>(rawMax / 127.0)};
    case DataType::UInt16:
        return {static_cast<float>(rawMin / 65535.0), static_cast<float>(rawMax / 65535.0)};
    case DataType::Int16:
        return {static_cast<float>(rawMin / 32767.0), static_cast<float>(rawMax / 32767.0)};
    default:
        // Float32, CPU-converted types: values pass through as-is
        return {static_cast<float>(rawMin), static_cast<float>(rawMax)};
    }
}

// Min/max scanning helpers for auto-detection
template<typename T>
std::pair<double, double> scanMinMax(const uint8_t* data, size_t pixelCount)
{
    const T* typed = reinterpret_cast<const T*>(data);
    T minVal = typed[0];
    T maxVal = typed[0];
    for (size_t i = 1; i < pixelCount; ++i) {
        if (typed[i] < minVal) minVal = typed[i];
        if (typed[i] > maxVal) maxVal = typed[i];
    }
    return {static_cast<double>(minVal), static_cast<double>(maxVal)};
}

template<typename T>
void scanMinMaxStrided(const uint8_t* data, size_t pixelCount, int numChannels, int channelIndex,
                       double& outMin, double& outMax)
{
    const T* typed = reinterpret_cast<const T*>(data);
    T minVal = typed[channelIndex];
    T maxVal = typed[channelIndex];
    const size_t numChannels_z = static_cast<size_t>(numChannels);
    const size_t channelIndex_z = static_cast<size_t>(channelIndex);
    for (size_t p = 1; p < pixelCount; ++p) {
        T v = typed[p * numChannels_z + channelIndex_z];
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }
    outMin = std::min(outMin, static_cast<double>(minVal));
    outMax = std::max(outMax, static_cast<double>(maxVal));
}

void computeMinMaxStrided(const uint8_t* data, size_t pixelCount, int numChannels,
                          int channelIndex, DataType dataType, double& outMin, double& outMax)
{
    if (pixelCount == 0) return;
    switch (dataType) {
    case DataType::Byte:    scanMinMaxStrided<uint8_t>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::Int8:    scanMinMaxStrided<int8_t>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::UInt16:  scanMinMaxStrided<uint16_t>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::Int16:   scanMinMaxStrided<int16_t>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::UInt32:  scanMinMaxStrided<uint32_t>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::Int32:   scanMinMaxStrided<int32_t>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::Float32: scanMinMaxStrided<float>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    case DataType::Float64: scanMinMaxStrided<double>(data, pixelCount, numChannels, channelIndex, outMin, outMax); break;
    default: break;
    }
}

std::pair<double, double> computeMinMax(const uint8_t* data, size_t totalElements,
                                         DataType dataType)
{
    if (totalElements == 0) return {0.0, 1.0};
    switch (dataType) {
    case DataType::Byte:    return scanMinMax<uint8_t>(data, totalElements);
    case DataType::Int8:    return scanMinMax<int8_t>(data, totalElements);
    case DataType::UInt16:  return scanMinMax<uint16_t>(data, totalElements);
    case DataType::Int16:   return scanMinMax<int16_t>(data, totalElements);
    case DataType::UInt32:  return scanMinMax<uint32_t>(data, totalElements);
    case DataType::Int32:   return scanMinMax<int32_t>(data, totalElements);
    case DataType::Float32: return scanMinMax<float>(data, totalElements);
    case DataType::Float64: return scanMinMax<double>(data, totalElements);
    default: return {0.0, 1.0};
    }
}

// Full data-type range used for per-channel histogram binning. Matches the
// fallback ranges in setSlideOpenResult around line 1022 so handle math in
// the UI lines up with renderer's toSamplerRange normalization.
std::pair<double, double> dataTypeHistogramRange(DataType dt)
{
    switch (dt) {
        case DataType::Byte:    return {0.0, 255.0};
        case DataType::Int8:    return {-128.0, 127.0};
        case DataType::UInt16:  return {0.0, 65535.0};
        case DataType::Int16:   return {0.0, 32767.0};
        case DataType::UInt32:  return {0.0, 4294967295.0};
        case DataType::Int32:   return {0.0, 2147483647.0};
        case DataType::Float32: return {0.0, 1.0};
        case DataType::Float64: return {0.0, 1.0};
        default:                return {0.0, 255.0};
    }
}

// Map a pixel element from native type through [min,max] -> [0,1] for thumbnail mixing.
double mapPixelNormalized(const uint8_t* buffer, size_t elementIndex,
                          DataType dataType, double minVal, double maxVal)
{
    double value = 0.0;
    switch (dataType) {
    case DataType::Byte:    value = static_cast<double>(buffer[elementIndex]); break;
    case DataType::Int8:    value = static_cast<double>(reinterpret_cast<const int8_t*>(buffer)[elementIndex]); break;
    case DataType::UInt16:  value = static_cast<double>(reinterpret_cast<const uint16_t*>(buffer)[elementIndex]); break;
    case DataType::Int16:   value = static_cast<double>(reinterpret_cast<const int16_t*>(buffer)[elementIndex]); break;
    case DataType::UInt32:  value = static_cast<double>(reinterpret_cast<const uint32_t*>(buffer)[elementIndex]); break;
    case DataType::Int32:   value = static_cast<double>(reinterpret_cast<const int32_t*>(buffer)[elementIndex]); break;
    case DataType::Float32: value = static_cast<double>(reinterpret_cast<const float*>(buffer)[elementIndex]); break;
    case DataType::Float64: value = static_cast<double>(reinterpret_cast<const double*>(buffer)[elementIndex]); break;
    default: value = static_cast<double>(buffer[elementIndex]); break;
    }
    double range = maxVal - minVal;
    if (range <= 0.0) return 0.0;
    return std::clamp((value - minVal) / range, 0.0, 1.0);
}

// Map a pixel element from native type through [min,max] -> [0,255] for thumbnails
uint8_t mapPixelToUint8(const uint8_t* buffer, size_t elementIndex,
                        DataType dataType, double minVal, double maxVal)
{
    return static_cast<uint8_t>(
        std::round(mapPixelNormalized(buffer, elementIndex, dataType, minVal, maxVal) * 255.0));
}

// Render a resampled block to an 8-bit QImage (RGB888 or Grayscale8),
// auto-stretching each channel through the block's native data type. Without
// this rescale, non-Byte data types (e.g., UInt16) produce distorted thumbnails
// because the buffer carries multiple bytes per sample, but a straight byte
// copy assumes one byte per channel.
//
// Channel-mix mode (mirrors the minimap / viewport channelShader path): when
// the slide is not single-channel brightfield, each channel's normalized value
// is multiplied by its assigned colorR/G/B * intensity and summed into RGB.
// This matters for Bgr24 CZIs, where SlideIO labels channels 0/1/2 with
// colors blue/green/red — a naive src[0]→R / src[1]→G / src[2]→B copy would
// swap R and B.
QImage tileDataToQImage(const slideio::viewer::core::TileData& block,
                        int slideNumChannels,
                        const std::vector<slideio::viewer::core::ChannelInfo>& channels,
                        bool isBrightfield)
{
    if (block.isEmpty() || block.isError()) return {};
    const int width = block.width();
    const int height = block.height();
    const int srcCh = block.numChannels();
    if (width <= 0 || height <= 0 || srcCh <= 0) return {};

    const DataType dt = block.dataType();
    const uint8_t* src = block.buffer().data();
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    const bool useChannelMix = !(isBrightfield && slideNumChannels <= 1)
                               && slideNumChannels >= 1
                               && !channels.empty();
    const int mixCh = useChannelMix
        ? std::min({slideNumChannels, srcCh, static_cast<int>(channels.size())})
        : 0;

    QImage::Format imgFmt = useChannelMix
        ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage out(width, height, imgFmt);
    out.fill(isBrightfield ? Qt::white : Qt::black);

    if (useChannelMix) {
        struct ChannelMix
        {
            double minVal = 0.0;
            double maxVal = 1.0;
            float r = 0.0f, g = 0.0f, b = 0.0f;
            bool active = false;
        };
        std::vector<ChannelMix> mix(static_cast<size_t>(mixCh));
        for (int ch = 0; ch < mixCh; ++ch) {
            ChannelMix& m = mix[static_cast<size_t>(ch)];
            double mn = std::numeric_limits<double>::max();
            double mx = std::numeric_limits<double>::lowest();
            computeMinMaxStrided(src, pixelCount, srcCh, ch, dt, mn, mx);
            m.minVal = mn; m.maxVal = mx;
            const auto& chInfo = channels[static_cast<size_t>(ch)];
            if (!chInfo.visible) continue;
            float intensity = std::clamp(chInfo.intensity, 0.0f, 4.0f);
            m.r = chInfo.colorR * intensity;
            m.g = chInfo.colorG * intensity;
            m.b = chInfo.colorB * intensity;
            m.active = true;
        }
        for (int y = 0; y < height; ++y) {
            uint8_t* dst = out.scanLine(y);
            for (int x = 0; x < width; ++x) {
                size_t srcElem = static_cast<size_t>((y * width + x) * srcCh);
                float accR = 0.0f, accG = 0.0f, accB = 0.0f;
                for (int ch = 0; ch < mixCh; ++ch) {
                    const ChannelMix& m = mix[static_cast<size_t>(ch)];
                    if (!m.active) continue;
                    float v = static_cast<float>(mapPixelNormalized(
                        src, srcElem + static_cast<size_t>(ch), dt, m.minVal, m.maxVal));
                    accR += v * m.r;
                    accG += v * m.g;
                    accB += v * m.b;
                }
                int dstIdx = x * 3;
                dst[dstIdx + 0] = static_cast<uint8_t>(std::clamp(accR * 255.0f, 0.0f, 255.0f));
                dst[dstIdx + 1] = static_cast<uint8_t>(std::clamp(accG * 255.0f, 0.0f, 255.0f));
                dst[dstIdx + 2] = static_cast<uint8_t>(std::clamp(accB * 255.0f, 0.0f, 255.0f));
            }
        }
    } else {
        double mn = std::numeric_limits<double>::max();
        double mx = std::numeric_limits<double>::lowest();
        computeMinMaxStrided(src, pixelCount, srcCh, 0, dt, mn, mx);
        for (int y = 0; y < height; ++y) {
            uint8_t* dst = out.scanLine(y);
            for (int x = 0; x < width; ++x) {
                size_t srcElem = static_cast<size_t>((y * width + x) * srcCh);
                dst[x] = mapPixelToUint8(src, srcElem, dt, mn, mx);
            }
        }
    }

    return out;
}

// Fallback used when readBlock fails (e.g., a single corrupted tile at the
// pyramid level SlideIO picked taints the whole resampled read). Walks the
// coarsest level tile-by-tile through the source, skipping individual tile
// failures, assembles the survivors at coarse-level dimensions, and scales
// down to (targetW, targetH). Returns null if the level is unusable or no
// tile could be read.
// True when the source's coarsest level is small enough to read in full. This
// is what separates a real pyramid — whose coarsest level is a handful of
// tiles — from a slide with no downsampled level, where the only level is full
// resolution and any whole-slide read decodes the entire image. Every
// whole-slide pass (overview thumbnail, scene thumbnails, the coarse-tile
// assembly fallback) is gated on it.
bool hasAffordableOverview(slideio::viewer::core::ISlideSource& source)
{
    namespace core = slideio::viewer::core;
    auto levels = source.levels();
    if (levels.empty()) return false;
    const auto& lvl = levels.back();
    if (lvl.width <= 0 || lvl.height <= 0) return false;

    const int tilesX = lvl.tilesX > 0
        ? lvl.tilesX
        : (lvl.tileWidth > 0 ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth : 1);
    const int tilesY = lvl.tilesY > 0
        ? lvl.tilesY
        : (lvl.tileHeight > 0 ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight : 1);

    return core::planTileSampling(tilesX, tilesY, core::kMaxOverviewTiles).complete;
}

QImage buildCoarseLevelThumbnailFallback(slideio::viewer::core::ISlideSource& source,
                                          int slideNumChannels,
                                          const std::vector<slideio::viewer::core::ChannelInfo>& channels,
                                          bool isBrightfield,
                                          int targetW, int targetH,
                                          int zIndex = 0)
{
    namespace core = slideio::viewer::core;
    // Assembling this image means reading every tile of the coarsest level and
    // allocating a buffer its full size — fine for a real pyramid's top level,
    // ruinous for a slide whose only level is full resolution.
    if (!hasAffordableOverview(source)) return {};
    auto levels = source.levels();
    if (levels.empty()) return {};
    const int coarsestLevel = static_cast<int>(levels.size()) - 1;
    const auto& lvl = levels[static_cast<size_t>(coarsestLevel)];
    if (lvl.width <= 0 || lvl.height <= 0
        || lvl.tileWidth <= 0 || lvl.tileHeight <= 0) return {};

    const int tilesX = lvl.tilesX > 0 ? lvl.tilesX
                                      : (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth;
    const int tilesY = lvl.tilesY > 0 ? lvl.tilesY
                                      : (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight;

    std::vector<core::TileData> tiles;
    tiles.reserve(static_cast<size_t>(tilesX) * static_cast<size_t>(tilesY));
    DataType dt = DataType::None;
    int srcCh = 0;
    int validTiles = 0;
    for (int r = 0; r < tilesY; ++r) {
        for (int c = 0; c < tilesX; ++c) {
            core::TileKey key(coarsestLevel, c, r, zIndex);
            auto td = source.readTile(key);
            if (!td.isEmpty() && !td.isError()) {
                if (dt == DataType::None) dt = td.dataType();
                if (srcCh == 0) srcCh = td.numChannels();
                ++validTiles;
            }
            tiles.push_back(std::move(td));
        }
    }
    if (validTiles == 0 || srcCh <= 0) return {};

    // Channel-mix mode mirrors tileDataToQImage / the viewport channelShader:
    // apply each channel's color * intensity. See the comment on
    // tileDataToQImage for the BGR-ordering rationale.
    const bool useChannelMix = !(isBrightfield && slideNumChannels <= 1)
                               && slideNumChannels >= 1
                               && !channels.empty();
    const int mixCh = useChannelMix
        ? std::min({slideNumChannels, srcCh, static_cast<int>(channels.size())})
        : 0;

    struct ChannelMix
    {
        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::lowest();
        float r = 0.0f, g = 0.0f, b = 0.0f;
        bool active = false;
    };
    std::vector<ChannelMix> mix;
    double mn = std::numeric_limits<double>::max();
    double mx = std::numeric_limits<double>::lowest();

    if (useChannelMix) {
        mix.resize(static_cast<size_t>(mixCh));
        for (int ch = 0; ch < mixCh; ++ch) {
            const auto& chInfo = channels[static_cast<size_t>(ch)];
            ChannelMix& m = mix[static_cast<size_t>(ch)];
            if (!chInfo.visible) continue;
            float intensity = std::clamp(chInfo.intensity, 0.0f, 4.0f);
            m.r = chInfo.colorR * intensity;
            m.g = chInfo.colorG * intensity;
            m.b = chInfo.colorB * intensity;
            m.active = true;
        }
        for (const auto& td : tiles) {
            if (td.isEmpty() || td.isError()) continue;
            size_t pixelCount = static_cast<size_t>(td.width()) * static_cast<size_t>(td.height());
            for (int ch = 0; ch < mixCh; ++ch) {
                computeMinMaxStrided(td.buffer().data(), pixelCount, srcCh, ch, dt,
                                     mix[static_cast<size_t>(ch)].minVal,
                                     mix[static_cast<size_t>(ch)].maxVal);
            }
        }
    } else {
        for (const auto& td : tiles) {
            if (td.isEmpty() || td.isError()) continue;
            size_t pixelCount = static_cast<size_t>(td.width()) * static_cast<size_t>(td.height());
            computeMinMaxStrided(td.buffer().data(), pixelCount, srcCh, 0, dt, mn, mx);
        }
    }

    QImage::Format imgFmt = useChannelMix
        ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage levelImg(lvl.width, lvl.height, imgFmt);
    levelImg.fill(isBrightfield ? Qt::white : Qt::black);

    int tileIdx = 0;
    for (int r = 0; r < tilesY; ++r) {
        for (int c = 0; c < tilesX; ++c) {
            const auto& td = tiles[static_cast<size_t>(tileIdx++)];
            if (td.isEmpty() || td.isError()) continue;
            const int tw = td.width();
            const int th = td.height();
            const int tileX0 = c * lvl.tileWidth;
            const int tileY0 = r * lvl.tileHeight;
            const uint8_t* src = td.buffer().data();
            for (int y = 0; y < th && (tileY0 + y) < lvl.height; ++y) {
                uint8_t* dst = levelImg.scanLine(tileY0 + y);
                for (int x = 0; x < tw && (tileX0 + x) < lvl.width; ++x) {
                    size_t srcElem = static_cast<size_t>((y * tw + x) * srcCh);
                    if (useChannelMix) {
                        float accR = 0.0f, accG = 0.0f, accB = 0.0f;
                        for (int ch = 0; ch < mixCh; ++ch) {
                            const ChannelMix& m = mix[static_cast<size_t>(ch)];
                            if (!m.active) continue;
                            float v = static_cast<float>(mapPixelNormalized(
                                src, srcElem + static_cast<size_t>(ch), dt, m.minVal, m.maxVal));
                            accR += v * m.r;
                            accG += v * m.g;
                            accB += v * m.b;
                        }
                        int dstIdx = (tileX0 + x) * 3;
                        dst[dstIdx + 0] = static_cast<uint8_t>(std::clamp(accR * 255.0f, 0.0f, 255.0f));
                        dst[dstIdx + 1] = static_cast<uint8_t>(std::clamp(accG * 255.0f, 0.0f, 255.0f));
                        dst[dstIdx + 2] = static_cast<uint8_t>(std::clamp(accB * 255.0f, 0.0f, 255.0f));
                    } else {
                        dst[tileX0 + x] = mapPixelToUint8(src, srcElem, dt, mn, mx);
                    }
                }
            }
        }
    }

    if (targetW > 0 && targetH > 0 && (targetW != lvl.width || targetH != lvl.height)) {
        return levelImg.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return levelImg;
}

// Read the coarsest pyramid level synchronously: detect global and per-channel
// display ranges, build a thumbnail QImage, and insert the tiles into the
// cache so the very first paint has a base layer to show. Throws on errors;
// caller wraps in try/catch.
void readCoarseLevelAndBuildThumbnail(slideio::viewer::core::ISlideSource& slideSource,
                                      slideio::viewer::core::SlideInfo& slideInfo,
                                      const slideio::viewer::core::TilePyramid& pyramid,
                                      slideio::viewer::core::ITileCache& tileCache,
                                      QImage& thumbnailOut)
{
    namespace core = slideio::viewer::core;
    int coarsestLevel = pyramid.numLevels() - 1;
    if (coarsestLevel < 0) return;
    const auto& coarseLvl = pyramid.levelInfo(coarsestLevel);

    // A pyramid's coarsest level is a handful of tiles, so it gets read whole.
    // A slide with no downsampled level has only its full-resolution level:
    // a 82432x103936 WSI is a 322x406 grid of 256px tiles, and reading all
    // 130732 of them costs ~27 minutes at the ~12ms a full-res tile takes to
    // decode. Sample such a level instead -- a few hundred tiles spread across
    // the slide give a display range in a few seconds.
    const auto samplingPlan = core::planTileSampling(coarseLvl.tilesX, coarseLvl.tilesY,
                                                     core::kMaxCoarseScanTiles);
    // Separate, far more generous budget: thinning the scan only costs
    // precision, but refusing to read the level in full costs the overview
    // image entirely, so that decision gets its own threshold.
    const bool overviewAffordable =
        core::planTileSampling(coarseLvl.tilesX, coarseLvl.tilesY,
                               core::kMaxOverviewTiles).complete;
    if (!samplingPlan.complete) {
        spdlog::info("autodetect: coarsest level is {}x{} tiles -- scanning every {}x{} "
                     "({} of {} tiles), overviewAffordable={}",
                     coarseLvl.tilesX, coarseLvl.tilesY,
                     samplingPlan.strideX, samplingPlan.strideY,
                     core::sampledTileCount(coarseLvl.tilesX, coarseLvl.tilesY, samplingPlan),
                     static_cast<long long>(coarseLvl.tilesX) * coarseLvl.tilesY,
                     overviewAffordable);
    }
    slideInfo.overviewAvailable = overviewAffordable;

    // Target thumbnail dimensions are derived from the FULL SLIDE dims (not the
    // coarsest pyramid level), so even when the coarsest level is small (e.g.,
    // ~200x160 in a 9-level pyramid) we still ask SlideIO for a sharp 800x800-
    // ish thumbnail. SlideIO picks the appropriate pyramid level internally.
    constexpr int kMaxThumbDim = 1000;
    int thumbW = slideInfo.width;
    int thumbH = slideInfo.height;
    if (thumbW > kMaxThumbDim || thumbH > kMaxThumbDim) {
        double ratio = std::min(static_cast<double>(kMaxThumbDim) / thumbW,
                                static_cast<double>(kMaxThumbDim) / thumbH);
        thumbW = std::max(1, static_cast<int>(thumbW * ratio));
        thumbH = std::max(1, static_cast<int>(thumbH * ratio));
    }

    int numCh = slideInfo.numChannels;
    DataType dt = slideInfo.channelDataType;

    // Pass 1: read all coarsest-level tiles and compute global min/max
    // Iterate over all Z slices when scanning for display ranges. A Z=0
    // default would miss the signal in fluorescence Z-stacks whose top slice
    // is out of focus and reads as all zeros (the case that prompted this:
    // a VSI 2-channel UInt16 Z-stack rendered as black because Z=0 was empty
    // and QuPath's reported peaks lived in middle slices). Tiles are cached
    // only for Z=0 / T=0 — the active slice on slide open — to match the
    // viewport's initial render state without bloating the cache for stacks.
    std::vector<core::TileData> coarseTiles;
    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    std::vector<double> channelMin;
    std::vector<double> channelMax;
    if (numCh > 1) {
        channelMin.assign(static_cast<size_t>(numCh), std::numeric_limits<double>::max());
        channelMax.assign(static_cast<size_t>(numCh), std::numeric_limits<double>::lowest());
    }

    const int numZ = std::max(1, slideInfo.numZSlices);
    // Preferred Z for cache + thumbnail rendering. Middle slice for stacks,
    // which is the usual in-focus slice for fluorescence Z-stacks and far
    // more useful as a startup view than Z=0 (which is often blank).
    const int preferredZ = numZ > 1 ? numZ / 2 : 0;
    const int numT = std::max(1, slideInfo.numTFrames);
    const int preferredT = numT > 1 ? numT / 2 : 0;
    // TODO: the coarse-tile loop below iterates only over Z; T is only sampled
    // in the readBlock thumbnail call. If a future T-series slide has an empty
    // T=0, autodetect min/max may be wrong unless the block fallback succeeds.

    for (int r = 0; r < coarseLvl.tilesY; r += samplingPlan.strideY) {
        for (int c = 0; c < coarseLvl.tilesX; c += samplingPlan.strideX) {
            for (int z = 0; z < numZ; ++z) {
                core::TileKey tileKey(coarsestLevel, c, r, z);
                auto tileData = slideSource.readTile(tileKey);
                const bool valid = !tileData.isEmpty() && !tileData.isError();
                if (valid) {
                    size_t pixelCount = static_cast<size_t>(tileData.width())
                                      * static_cast<size_t>(tileData.height());
                    size_t totalElements = pixelCount
                                         * static_cast<size_t>(tileData.numChannels());
                    auto [tMin, tMax] = computeMinMax(tileData.buffer().data(), totalElements, dt);
                    globalMin = std::min(globalMin, tMin);
                    globalMax = std::max(globalMax, tMax);
                    if (numCh > 1) {
                        for (int ch = 0; ch < numCh; ++ch) {
                            const size_t ch_z = static_cast<size_t>(ch);
                            computeMinMaxStrided(tileData.buffer().data(), pixelCount,
                                                 numCh, ch, dt, channelMin[ch_z], channelMax[ch_z]);
                        }
                    }
                }
                if (z == preferredZ) {
                    coarseTiles.push_back(std::move(tileData));
                }
            }
        }
    }

    spdlog::info("autodetect: coarse-tile pass: numZ={} numTiles={} valid={} dt={} globalMin={} globalMax={}",
                 numZ, coarseTiles.size(),
                 std::count_if(coarseTiles.begin(), coarseTiles.end(),
                               [](const core::TileData& t){ return !t.isEmpty() && !t.isError(); }),
                 static_cast<int>(dt), globalMin, globalMax);

    if (globalMin < globalMax) {
        slideInfo.displayRange.displayMin = globalMin;
        slideInfo.displayRange.displayMax = globalMax;
        slideInfo.displayRange.autoDetected = true;
    }

    if (numCh > 1) {
        for (int ch = 0; ch < numCh; ++ch) {
            const size_t ch_z = static_cast<size_t>(ch);
            spdlog::info("autodetect: coarse-tile pass: channel {} min={} max={}",
                         ch, channelMin[ch_z], channelMax[ch_z]);
            if (ch < static_cast<int>(slideInfo.channels.size())) {
                if (channelMin[ch_z] < channelMax[ch_z]) {
                    slideInfo.channels[ch_z].displayRange.displayMin = channelMin[ch_z];
                    slideInfo.channels[ch_z].displayRange.displayMax = channelMax[ch_z];
                    slideInfo.channels[ch_z].displayRange.autoDetected = true;
                }
            }
        }
    }

    // Read the full-slide thumbnail block up front. SlideIO picks its own
    // pyramid level here, so this usually succeeds even when the coarsest
    // level above failed to yield any readable tiles (VSI files in particular
    // can have an unreliable coarsest level). The block data is consumed
    // below for thumbnail rendering, and — when the per-tile pass above did
    // not set autoDetected — used as a fallback source for display-range
    // autodetection. Without this fallback, 16-bit slides whose data only
    // populates a small fraction of the type range (e.g., UInt16 with peak
    // ~2k) inherit the {0, 65535} fallback from openSceneSync and render
    // as nearly black.
    //
    // Skipped when the level is too large to read in full: SlideIO resamples
    // this block from the only level it has, so asking for the whole slide
    // would decode every tile of a full-resolution level.
    core::TileData blockData;
    if (overviewAffordable) {
        blockData = slideSource.readBlock(0, 0, slideInfo.width, slideInfo.height,
                                          thumbW, thumbH, preferredZ, preferredT);
    }
    const bool blockUsable = !blockData.isEmpty() && !blockData.isError();
    spdlog::info("autodetect: readBlock usable={} blockSize={}x{} blockCh={} blockDt={}",
                 blockUsable, blockData.width(), blockData.height(),
                 blockData.numChannels(), static_cast<int>(blockData.dataType()));

    if (!slideInfo.displayRange.autoDetected && blockUsable) {
        const int blockCh = blockData.numChannels();
        const size_t blockPixels = static_cast<size_t>(blockData.width())
                                 * static_cast<size_t>(blockData.height());
        const size_t totalElements = blockPixels * static_cast<size_t>(blockCh);
        auto [bMin, bMax] = computeMinMax(blockData.buffer().data(), totalElements, dt);
        spdlog::info("autodetect: block fallback global: min={} max={}", bMin, bMax);
        if (bMin < bMax) {
            slideInfo.displayRange.displayMin = bMin;
            slideInfo.displayRange.displayMax = bMax;
            slideInfo.displayRange.autoDetected = true;
            globalMin = bMin;
            globalMax = bMax;
        }
        if (numCh > 1 && blockCh >= numCh) {
            for (int ch = 0; ch < numCh; ++ch) {
                const size_t ch_z = static_cast<size_t>(ch);
                if (ch >= static_cast<int>(slideInfo.channels.size())) break;
                if (slideInfo.channels[ch_z].displayRange.autoDetected) continue;
                double cMin = std::numeric_limits<double>::max();
                double cMax = std::numeric_limits<double>::lowest();
                computeMinMaxStrided(blockData.buffer().data(), blockPixels,
                                     blockCh, ch, dt, cMin, cMax);
                spdlog::info("autodetect: block fallback channel {} min={} max={}",
                             ch, cMin, cMax);
                if (cMin < cMax) {
                    slideInfo.channels[ch_z].displayRange.displayMin = cMin;
                    slideInfo.channels[ch_z].displayRange.displayMax = cMax;
                    slideInfo.channels[ch_z].displayRange.autoDetected = true;
                }
            }
        }
    }

    // Per-channel histograms. Computed from the readBlock buffer when there is
    // one, and otherwise accumulated across the sampled coarse tiles so that a
    // slide with no downsampled level still gets histograms. Uses the data
    // type's full range so draggable-handle math in the UI lines up with the
    // renderer's toSamplerRange normalization.
    if (numCh >= 1) {
        for (int ch = 0; ch < numCh; ++ch) {
            const size_t ch_z = static_cast<size_t>(ch);
            if (ch_z >= slideInfo.channels.size()) break;

            auto& chInfo = slideInfo.channels[ch_z];
            auto [rMin, rMax] = dataTypeHistogramRange(chInfo.dataType);

            chInfo.histogram.bins.assign(core::kNumBins, 0);
            chInfo.histogram.rangeMin = rMin;
            chInfo.histogram.rangeMax = rMax;

            // computeHistogramStrided increments in place, so several sources
            // accumulate into the same bins.
            if (blockUsable) {
                const int blockCh = blockData.numChannels();
                if (ch < blockCh) {
                    const size_t blockPixels = static_cast<size_t>(blockData.width())
                                             * static_cast<size_t>(blockData.height());
                    core::computeHistogramStrided(blockData.buffer().data(), blockPixels,
                                                  blockCh, ch,
                                                  chInfo.dataType, rMin, rMax,
                                                  chInfo.histogram.bins.data(),
                                                  core::kNumBins);
                }
            } else {
                for (const auto& tileData : coarseTiles) {
                    if (tileData.isEmpty() || tileData.isError()) continue;
                    const int tileCh = tileData.numChannels();
                    if (ch >= tileCh) continue;
                    const size_t tilePixels = static_cast<size_t>(tileData.width())
                                            * static_cast<size_t>(tileData.height());
                    core::computeHistogramStrided(tileData.buffer().data(), tilePixels,
                                                  tileCh, ch,
                                                  chInfo.dataType, rMin, rMax,
                                                  chInfo.histogram.bins.data(),
                                                  core::kNumBins);
                }
            }

            uint64_t total = 0;
            for (uint32_t v : chInfo.histogram.bins) total += v;
            chInfo.histogram.totalSamples = total;
            chInfo.histogram.valid = (total > 0);
        }
    }

    // Freeze the autodetect result for the "Auto" button. Done after both the
    // coarse-tile and block-fallback passes so it captures the final values.
    for (auto& chInfo : slideInfo.channels) {
        chInfo.autoDisplayRange = chInfo.displayRange;
    }

    // Final ranges, after both passes
    spdlog::info("autodetect: final slide-wide: min={} max={} autoDetected={}",
                 slideInfo.displayRange.displayMin,
                 slideInfo.displayRange.displayMax,
                 slideInfo.displayRange.autoDetected);
    for (size_t ch = 0; ch < slideInfo.channels.size(); ++ch) {
        const auto& dr = slideInfo.channels[ch].displayRange;
        spdlog::info("autodetect: final channel {} min={} max={} autoDetected={} color=({:.2f},{:.2f},{:.2f}) intensity={:.2f} visible={}",
                     ch, dr.displayMin, dr.displayMax, dr.autoDetected,
                     slideInfo.channels[ch].colorR,
                     slideInfo.channels[ch].colorG,
                     slideInfo.channels[ch].colorB,
                     slideInfo.channels[ch].intensity,
                     slideInfo.channels[ch].visible);
    }

    // Insert the tiles we read into the cache for an instant base-layer preview
    // at the preferred Z (middle slice for Z-stacks; otherwise Z=0). Walks the
    // same strides as the read loop, so it stays in step with coarseTiles.
    // Consumes coarseTiles, so it must run after every pass that reads them.
    auto insertCoarseTilesIntoCache = [&]() {
        size_t idx = 0;
        for (int r = 0; r < coarseLvl.tilesY; r += samplingPlan.strideY) {
            for (int c = 0; c < coarseLvl.tilesX; c += samplingPlan.strideX) {
                if (idx >= coarseTiles.size()) return;
                auto& td = coarseTiles[idx++];
                if (td.isEmpty() || td.isError()) continue;
                core::TileKey tileKey(coarsestLevel, c, r, preferredZ);
                tileCache.insert(tileKey, std::make_shared<core::TileData>(std::move(td)));
            }
        }
    };

    // The tile-assembly fallback below walks the full tile grid and allocates
    // an image the size of the whole level, so it is only valid when the scan
    // read every tile. When neither thumbnail path can run, say so rather than
    // producing a partial image: slideInfo.overviewAvailable tells the minimap
    // to explain the empty panel.
    if (!overviewAffordable || (!blockUsable && !samplingPlan.complete)) {
        spdlog::info("autodetect: no whole-slide overview "
                     "(overviewAffordable={}, blockUsable={}, fullScan={})",
                     overviewAffordable, blockUsable, samplingPlan.complete);
        slideInfo.overviewAvailable = false;
        thumbnailOut = QImage();
        insertCoarseTilesIntoCache();
        return;
    }

    // Thumbnail uses the same per-channel display ranges as the viewport's
    // multi-channel render path (where each channel is stretched independently),
    // so the minimap and viewport show matching colors. Falls back to global
    // range for single-channel rendering or when per-channel ranges weren't
    // detected.
    auto channelMinFor = [&](int ch) {
        const size_t ch_z = static_cast<size_t>(ch);
        if (numCh > 1 && ch < static_cast<int>(slideInfo.channels.size())
            && slideInfo.channels[ch_z].displayRange.autoDetected) {
            return slideInfo.channels[ch_z].displayRange.displayMin;
        }
        return globalMin;
    };
    auto channelMaxFor = [&](int ch) {
        const size_t ch_z = static_cast<size_t>(ch);
        if (numCh > 1 && ch < static_cast<int>(slideInfo.channels.size())
            && slideInfo.channels[ch_z].displayRange.autoDetected) {
            return slideInfo.channels[ch_z].displayRange.displayMax;
        }
        return globalMax;
    };
    double rMin = channelMinFor(0), rMax = channelMaxFor(0);
    double gMin = channelMinFor(1), gMax = channelMaxFor(1);
    double bMin = channelMinFor(2), bMax = channelMaxFor(2);

    // Mix all visible channels using ChannelInfo::colorR/G/B * intensity to
    // mirror the viewport's channelShader path (which is taken whenever the
    // viewport does not use its single-channel-brightfield fast path). This
    // matters for 3-channel brightfield CZIs where SlideIO labels the BGR
    // planes with channel colors blue/green/red — a naive src[0]→R / src[1]→G
    // / src[2]→B copy would swap R and B and the minimap would not match the
    // viewport.
    const bool useChannelMix = !(slideInfo.isBrightfield && numCh <= 1) && numCh >= 1;
    struct ChannelMix
    {
        double minVal = 0.0;
        double maxVal = 1.0;
        float r = 1.0f, g = 1.0f, b = 1.0f;  // colorR/G/B * intensity, premultiplied
        bool active = false;
    };
    std::vector<ChannelMix> mix;
    if (useChannelMix) {
        mix.resize(static_cast<size_t>(numCh));
        for (int ch = 0; ch < numCh; ++ch) {
            const size_t ch_z = static_cast<size_t>(ch);
            ChannelMix& m = mix[ch_z];
            m.minVal = channelMinFor(ch);
            m.maxVal = channelMaxFor(ch);
            if (ch < static_cast<int>(slideInfo.channels.size())) {
                const auto& chInfo = slideInfo.channels[ch_z];
                if (!chInfo.visible) continue;
                float intensity = std::clamp(chInfo.intensity, 0.0f, 4.0f);
                m.r = chInfo.colorR * intensity;
                m.g = chInfo.colorG * intensity;
                m.b = chInfo.colorB * intensity;
                m.active = true;
            }
        }
    }

    auto writeMixedPixel = [&](uint8_t* dst, const uint8_t* src, size_t srcElem, int srcCh) {
        float accR = 0.0f, accG = 0.0f, accB = 0.0f;
        const int upTo = std::min(numCh, srcCh);
        for (int ch = 0; ch < upTo; ++ch) {
            const ChannelMix& m = mix[static_cast<size_t>(ch)];
            if (!m.active) continue;
            float v = static_cast<float>(mapPixelNormalized(
                src, srcElem + static_cast<size_t>(ch), dt, m.minVal, m.maxVal));
            accR += v * m.r;
            accG += v * m.g;
            accB += v * m.b;
        }
        dst[0] = static_cast<uint8_t>(std::clamp(accR * 255.0f, 0.0f, 255.0f));
        dst[1] = static_cast<uint8_t>(std::clamp(accG * 255.0f, 0.0f, 255.0f));
        dst[2] = static_cast<uint8_t>(std::clamp(accB * 255.0f, 0.0f, 255.0f));
    };

    // Pass 2: build the thumbnail QImage. Read the whole slide resampled to
    // the target thumbnail resolution via SlideIO (it picks the best pyramid
    // level internally). This gives a sharp thumbnail even when the coarsest
    // pyramid level itself is very small (e.g., 200x160) — much sharper than
    // upscaling from the coarsest tiles.
    QImage::Format imgFmt = (useChannelMix || numCh >= 3)
        ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage thumbImg(thumbW, thumbH, imgFmt);
    thumbImg.fill(slideInfo.isBrightfield ? Qt::white : Qt::black);

    // blockData was read above (before display-range autodetection so its
    // contents can serve as a fallback source for the min/max scan).
    if (blockUsable) {
        const uint8_t* src = blockData.buffer().data();
        int srcCh = blockData.numChannels();
        for (int y = 0; y < thumbH; ++y) {
            uint8_t* dst = thumbImg.scanLine(y);
            for (int x = 0; x < thumbW; ++x) {
                size_t srcElem = static_cast<size_t>((y * thumbW + x) * srcCh);
                if (useChannelMix) {
                    writeMixedPixel(dst + x * 3, src, srcElem, srcCh);
                } else if (numCh >= 3) {
                    int dstIdx = x * 3;
                    dst[dstIdx + 0] = mapPixelToUint8(src, srcElem + 0, dt, rMin, rMax);
                    dst[dstIdx + 1] = mapPixelToUint8(src, srcElem + 1, dt, gMin, gMax);
                    dst[dstIdx + 2] = mapPixelToUint8(src, srcElem + 2, dt, bMin, bMax);
                } else {
                    dst[x] = mapPixelToUint8(src, srcElem, dt, globalMin, globalMax);
                }
            }
        }
        thumbnailOut = thumbImg;
    } else {
        // Fallback: assemble from the coarsest tiles we already have
        QImage levelImg(coarseLvl.width, coarseLvl.height, imgFmt);
        levelImg.fill(slideInfo.isBrightfield ? Qt::white : Qt::black);
        int tileIdx = 0;
        for (int r = 0; r < coarseLvl.tilesY; ++r) {
            for (int c = 0; c < coarseLvl.tilesX; ++c) {
                const auto& tileData = coarseTiles[static_cast<size_t>(tileIdx++)];
                if (tileData.isEmpty() || tileData.isError()) continue;
                int tw = tileData.width();
                int th = tileData.height();
                int tCh = tileData.numChannels();
                int tileX0 = c * coarseLvl.tileWidth;
                int tileY0 = r * coarseLvl.tileHeight;
                const uint8_t* src = tileData.buffer().data();
                for (int y = 0; y < th && (tileY0 + y) < coarseLvl.height; ++y) {
                    uint8_t* dst = levelImg.scanLine(tileY0 + y);
                    for (int x = 0; x < tw && (tileX0 + x) < coarseLvl.width; ++x) {
                        size_t srcElem = static_cast<size_t>((y * tw + x) * tCh);
                        if (useChannelMix) {
                            writeMixedPixel(dst + (tileX0 + x) * 3, src, srcElem, tCh);
                        } else if (numCh >= 3) {
                            int dstIdx = (tileX0 + x) * 3;
                            dst[dstIdx + 0] = mapPixelToUint8(src, srcElem + 0, dt, rMin, rMax);
                            dst[dstIdx + 1] = mapPixelToUint8(src, srcElem + 1, dt, gMin, gMax);
                            dst[dstIdx + 2] = mapPixelToUint8(src, srcElem + 2, dt, bMin, bMax);
                        } else {
                            dst[tileX0 + x] = mapPixelToUint8(src, srcElem, dt, globalMin, globalMax);
                        }
                    }
                }
            }
        }
        thumbnailOut = levelImg.scaled(thumbW, thumbH,
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    insertCoarseTilesIntoCache();
}

using slideio::viewer::ui::SceneOpenResult;

// Open a scene synchronously (intended to run on a background thread). Captures
// any exception into result.errorMsg. statusCallback (if non-null) is invoked
// when the adapter first decides a pyramid level is unreliable, so the loading
// overlay can change its label to indicate the slow-but-correct fallback path.
SceneOpenResult openSceneSync(const std::string& filePath, int sceneIndex,
                              const std::string& driverId,
                              std::function<void(QString)> statusCallback = {},
                              std::vector<uint8_t> defaultProfileBytes = {})
{
    namespace core = slideio::viewer::core;
    namespace infra = slideio::viewer::infra;
    SceneOpenResult r;
    r.filePath = filePath;
    try {
        r.slideSource = std::make_shared<infra::SlideIOAdapter>(
            filePath, sceneIndex, driverId, std::move(defaultProfileBytes));
        {
            r.slideInfo = r.slideSource->slideInfo();
            auto levels = r.slideSource->levels();
            r.pyramid = std::make_shared<core::TilePyramid>(
                r.slideInfo.width, r.slideInfo.height, levels);
        }
        if (statusCallback) {
            r.slideSource->addOnLevelMarkedUnreliable([statusCallback](int level) {
                statusCallback(QStringLiteral("Working around corrupted tiles (level %1)…").arg(level));
            });
        }
        r.tileCache = std::make_shared<infra::LruTileCache>();
        try {
            readCoarseLevelAndBuildThumbnail(*r.slideSource, r.slideInfo, *r.pyramid,
                                             *r.tileCache, r.thumbnail);
        } catch (const std::exception& ex) {
            spdlog::warn("openSceneSync: thumbnail/coarse-cache pass failed: {}", ex.what());
        }

        // Apply fallback display range if auto-detection didn't yield one
        if (!r.slideInfo.displayRange.autoDetected) {
            spdlog::warn("openSceneSync: autodetection FAILED, applying fallback for dt={}",
                         static_cast<int>(r.slideInfo.channelDataType));
            switch (r.slideInfo.channelDataType) {
            case DataType::Byte:    r.slideInfo.displayRange = {0.0, 255.0, false}; break;
            case DataType::UInt16:  r.slideInfo.displayRange = {0.0, 65535.0, false}; break;
            case DataType::Int16:   r.slideInfo.displayRange = {0.0, 32767.0, false}; break;
            case DataType::Float32: r.slideInfo.displayRange = {0.0, 1.0, false}; break;
            default:                r.slideInfo.displayRange = {0.0, 255.0, false}; break;
            }
        }
        r.success = true;
    } catch (const std::exception& ex) {
        r.errorMsg = ex.what();
    } catch (...) {
        r.errorMsg = "unknown error";
    }
    return r;
}

// Open an auxiliary image synchronously (background thread).
SceneOpenResult openAuxImageSync(const std::string& filePath, const std::string& auxImageName,
                                 const std::string& driverId,
                                 std::function<void(QString)> statusCallback = {},
                                 std::vector<uint8_t> defaultProfileBytes = {})
{
    namespace core = slideio::viewer::core;
    namespace infra = slideio::viewer::infra;
    SceneOpenResult r;
    r.filePath = filePath;
    r.isAuxImage = true;
    try {
        r.slideSource = std::make_shared<infra::SlideIOAdapter>(
            filePath, auxImageName, driverId, std::move(defaultProfileBytes));
        {
            r.slideInfo = r.slideSource->slideInfo();
            auto levels = r.slideSource->levels();
            r.pyramid = std::make_shared<core::TilePyramid>(
                r.slideInfo.width, r.slideInfo.height, levels);
        }
        if (statusCallback) {
            r.slideSource->addOnLevelMarkedUnreliable([statusCallback](int level) {
                statusCallback(QStringLiteral("Working around corrupted tiles (level %1)…").arg(level));
            });
        }
        r.tileCache = std::make_shared<infra::LruTileCache>();
        try {
            readCoarseLevelAndBuildThumbnail(*r.slideSource, r.slideInfo, *r.pyramid,
                                             *r.tileCache, r.thumbnail);
        } catch (const std::exception& ex) {
            spdlog::warn("openAuxImageSync: thumbnail/coarse-cache pass failed: {}", ex.what());
        }

        if (!r.slideInfo.displayRange.autoDetected) {
            switch (r.slideInfo.channelDataType) {
            case DataType::Byte:    r.slideInfo.displayRange = {0.0, 255.0, false}; break;
            case DataType::UInt16:  r.slideInfo.displayRange = {0.0, 65535.0, false}; break;
            default:                r.slideInfo.displayRange = {0.0, 255.0, false}; break;
            }
        }
        r.success = true;
    } catch (const std::exception& ex) {
        r.errorMsg = ex.what();
    } catch (...) {
        r.errorMsg = "unknown error";
    }
    return r;
}

} // anonymous namespace

namespace slideio::viewer::ui
{

struct ViewportWidget::Impl
{
    // OpenGL resources
    QOpenGLFunctions_3_3_Core* gl = nullptr;

    // Captured in initializeGL(). glGetString can only be called on the thread
    // holding the context, so the About dialog reads this copy rather than
    // reaching into GL from wherever it happens to be opened.
    GpuInfo gpuInfo;
    std::unique_ptr<QOpenGLShaderProgram> tileShader;
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    // Texture management
    std::unordered_map<core::TileKey, GLuint> textures;
    std::vector<core::TileKey> pendingUploads;
    std::mutex pendingUploadsMutex;

    // Fluorescence rendering
    std::unique_ptr<QOpenGLShaderProgram> channelShader;
    std::unique_ptr<QOpenGLShaderProgram> snapshotShader;
    std::unique_ptr<QOpenGLShaderProgram> compositeShader;

    // Offscreen FBO for additive tile compositing — keeps the additive blend
    // confined to the tile pass so the snapshot underneath is not added to.
    GLuint tileFbo = 0;
    GLuint tileFboTexture = 0;
    int tileFboWidth = 0;
    int tileFboHeight = 0;

    struct TileTextures
    {
        std::vector<GLuint> channelTexIds;
    };
    std::unordered_map<core::TileKey, TileTextures> fluorescenceTextures;

    // Slide management
    std::shared_ptr<core::ISlideSource> slideSource;
    std::shared_ptr<core::ITileCache> tileCache;
    std::shared_ptr<infra::TileLoadScheduler> scheduler;
    std::shared_ptr<core::TilePyramid> pyramid;
    std::unique_ptr<ViewportController> controller;
    core::SlideInfo slideInfo;
    bool slideOpen = false;
    std::string currentFilePath;
    std::string currentDriverId;  // empty for auto-detect
    int currentZSlice = 0;
    int currentTFrame = 0;

    // Bytes of the profile to assume for slides embedding none, set by
    // MainWindow (from QSettings, on the UI thread) before a slide is opened.
    // Captured by value into the background open threads below.
    std::vector<uint8_t> defaultColorProfile;

    // Async open: every slide-open call increments openOpId. Background workers
    // capture the id at start and finalize-on-UI checks it; mismatch means the
    // user issued another open (or close) and the result should be discarded.
    std::atomic<uint64_t> openOpId{0};

    // Zoom snapshot for smooth visual transitions
    GLuint snapshotTexture = 0;
    int snapshotFbWidth = 0;
    int snapshotFbHeight = 0;
    double snapshotCenterX = 0.0;
    double snapshotCenterY = 0.0;
    double snapshotScale = 0.0;
    bool snapshotReady = false;  // texture contains valid content from a completed frame
    bool snapshotActive = false; // actively showing snapshot during zoom transition

    // Mouse interaction
    bool isPanning = false;
    QPoint lastMousePos;

    // GL state
    bool glInitialized = false;

    void createQuadGeometry()
    {
        float vertices[] = {
            // Position (unit quad, two triangles)
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
        };

        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);

        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

        gl->glBindVertexArray(0);
        gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GLuint uploadTileTexture(const core::TileData& tile)
    {
        GLuint texId = 0;
        gl->glGenTextures(1, &texId);
        gl->glBindTexture(GL_TEXTURE_2D, texId);

        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        int channels = tile.numChannels();

        // Swizzle R -> RGB for single-channel textures so shader's texel.rgb reads grayscale
        if (channels == 1) {
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        }

        auto glFmt = glTextureFormatForDataType(tile.dataType(), channels);

        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        if (glFmt.requiresCpuConversion) {
            size_t pixelCount = static_cast<size_t>(tile.width())
                              * static_cast<size_t>(tile.height())
                              * static_cast<size_t>(channels);
            auto converted = convertBufferToFloat32(tile.buffer().data(), pixelCount, tile.dataType());
            gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(glFmt.internalFormat),
                             tile.width(), tile.height(), 0, glFmt.format, glFmt.type,
                             converted.data());
        } else {
            gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(glFmt.internalFormat),
                             tile.width(), tile.height(), 0, glFmt.format, glFmt.type,
                             tile.buffer().data());
        }

        gl->glBindTexture(GL_TEXTURE_2D, 0);
        return texId;
    }

    void deleteTexture(const core::TileKey& key)
    {
        auto it = textures.find(key);
        if (it != textures.end()) {
            gl->glDeleteTextures(1, &it->second);
            textures.erase(it);
        }
    }

    void clearAllTextures()
    {
        for (auto& pair : textures) {
            gl->glDeleteTextures(1, &pair.second);
        }
        textures.clear();
        pendingUploads.clear();
    }

    TileTextures uploadTileChannelTextures(const core::TileData& tile)
    {
        TileTextures result;
        int numChannels = tile.numChannels();
        size_t pixelCount = static_cast<size_t>(tile.width())
                          * static_cast<size_t>(tile.height());
        size_t bytesPerElement = core::dataTypeSize(tile.dataType());
        auto glFmt = glTextureFormatForDataType(tile.dataType(), 1);

        for (int ch = 0; ch < numChannels; ++ch) {
            GLuint texId = 0;
            gl->glGenTextures(1, &texId);
            gl->glBindTexture(GL_TEXTURE_2D, texId);

            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

            // Extract single-channel data from interleaved buffer
            std::vector<uint8_t> channelData(pixelCount * bytesPerElement);
            const uint8_t* src = tile.buffer().data();
            const size_t numChannels_z = static_cast<size_t>(numChannels);
            const size_t ch_z = static_cast<size_t>(ch);
            for (size_t p = 0; p < pixelCount; ++p) {
                const uint8_t* srcElem = src + (p * numChannels_z + ch_z) * bytesPerElement;
                uint8_t* dstElem = channelData.data() + p * bytesPerElement;
                std::memcpy(dstElem, srcElem, bytesPerElement);
            }

            if (glFmt.requiresCpuConversion) {
                auto converted = convertBufferToFloat32(channelData.data(), pixelCount, tile.dataType());
                gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(glFmt.internalFormat),
                                 tile.width(), tile.height(), 0, glFmt.format, glFmt.type,
                                 converted.data());
            } else {
                gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(glFmt.internalFormat),
                                 tile.width(), tile.height(), 0, glFmt.format, glFmt.type,
                                 channelData.data());
            }

            gl->glBindTexture(GL_TEXTURE_2D, 0);
            result.channelTexIds.push_back(texId);
        }
        return result;
    }

    void clearFluorescenceTextures()
    {
        for (auto& pair : fluorescenceTextures) {
            for (GLuint texId : pair.second.channelTexIds) {
                gl->glDeleteTextures(1, &texId);
            }
        }
        fluorescenceTextures.clear();
    }

    // Capture the current framebuffer at the end of a fully-rendered paintGL.
    // At this point the FBO is guaranteed to contain valid content.
    void captureSnapshotTexture(QOpenGLWidget* widget, const core::Viewport& viewport)
    {
        if (!gl || !glInitialized) return;

        int fbWidth = static_cast<int>(widget->width() * widget->devicePixelRatioF());
        int fbHeight = static_cast<int>(widget->height() * widget->devicePixelRatioF());
        if (fbWidth <= 0 || fbHeight <= 0) return;

        if (snapshotTexture == 0) {
            gl->glGenTextures(1, &snapshotTexture);
        }
        gl->glBindTexture(GL_TEXTURE_2D, snapshotTexture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Use glCopyTexSubImage2D if size matches to avoid re-allocation
        if (fbWidth == snapshotFbWidth && fbHeight == snapshotFbHeight) {
            gl->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, fbWidth, fbHeight);
        } else {
            gl->glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, fbWidth, fbHeight, 0);
            snapshotFbWidth = fbWidth;
            snapshotFbHeight = fbHeight;
        }
        gl->glBindTexture(GL_TEXTURE_2D, 0);

        snapshotCenterX = viewport.centerX();
        snapshotCenterY = viewport.centerY();
        snapshotScale = viewport.scale();
        snapshotReady = true;
    }

    // Activate the warm snapshot for zoom transition.
    // Called from event handlers before zoom is applied.
    void activateSnapshot()
    {
        if (snapshotReady && !snapshotActive) {
            snapshotActive = true;
        }
    }

    void renderSnapshot(const core::Viewport& viewport)
    {
        if (!snapshotActive || snapshotTexture == 0 || !snapshotShader) return;

        double sr = snapshotScale / viewport.scale();
        int w = viewport.screenWidth();
        int h = viewport.screenHeight();

        // Map the snapshot's captured slide region to a screen rectangle in the
        // current viewport. Drawing the snapshot only at this rect (with full
        // [0,1] tex coords) lets the cleared background show through outside it,
        // instead of streaking the snapshot's edge pixels via GL_CLAMP_TO_EDGE.
        double rectW = w / sr;
        double rectH = h / sr;
        double rectCenterX = (snapshotCenterX - viewport.centerX()) * viewport.scale() + w * 0.5;
        double rectCenterY = (snapshotCenterY - viewport.centerY()) * viewport.scale() + h * 0.5;
        double rectX = rectCenterX - rectW * 0.5;
        double rectY = rectCenterY - rectH * 0.5;

        snapshotShader->bind();
        snapshotShader->setUniformValue("uViewportSize",
            static_cast<float>(w), static_cast<float>(h));
        snapshotShader->setUniformValue("uScreenRect",
            static_cast<float>(rectX), static_cast<float>(rectY),
            static_cast<float>(rectW), static_cast<float>(rectH));
        // Full texture coverage with Y-flip (FBO is sampled origin-bottom-left).
        snapshotShader->setUniformValue("uTexCoordOffset", 0.0f, 1.0f);
        snapshotShader->setUniformValue("uTexCoordScale", 1.0f, -1.0f);
        snapshotShader->setUniformValue("uSnapshotTexture", 0);

        // The copied framebuffer texture may contain non-opaque alpha.
        // Draw snapshot as opaque to avoid washed-out/overexposed blending artifacts.
        gl->glDisable(GL_BLEND);
        gl->glBindVertexArray(quadVAO);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, snapshotTexture);
        gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glBindVertexArray(0);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        snapshotShader->release();
    }

    void clearSnapshot()
    {
        snapshotActive = false;
        snapshotReady = false;
    }

    void deleteSnapshotTexture()
    {
        if (snapshotTexture != 0 && gl) {
            gl->glDeleteTextures(1, &snapshotTexture);
            snapshotTexture = 0;
        }
        snapshotActive = false;
        snapshotReady = false;
        snapshotFbWidth = 0;
        snapshotFbHeight = 0;
    }

    void ensureTileFbo(int w, int h)
    {
        if (!gl || w <= 0 || h <= 0) return;
        if (tileFbo == 0) {
            gl->glGenFramebuffers(1, &tileFbo);
            gl->glGenTextures(1, &tileFboTexture);
        }
        if (w != tileFboWidth || h != tileFboHeight) {
            gl->glBindTexture(GL_TEXTURE_2D, tileFboTexture);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glBindTexture(GL_TEXTURE_2D, 0);

            gl->glBindFramebuffer(GL_FRAMEBUFFER, tileFbo);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, tileFboTexture, 0);
            GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                spdlog::error("ViewportWidget: tile FBO incomplete, status=0x{:x}", status);
            }
            tileFboWidth = w;
            tileFboHeight = h;
        }
    }

    void destroyTileFbo()
    {
        if (gl) {
            if (tileFbo != 0) {
                gl->glDeleteFramebuffers(1, &tileFbo);
                tileFbo = 0;
            }
            if (tileFboTexture != 0) {
                gl->glDeleteTextures(1, &tileFboTexture);
                tileFboTexture = 0;
            }
        }
        tileFboWidth = 0;
        tileFboHeight = 0;
    }

    // Find a fallback tile at a coarser level that covers the given tile's area.
    // Returns the fallback TileKey and computes the texture sub-region (offset + scale)
    // that maps to the missing tile's area within the coarser tile.
    // Returns false if no fallback is available.
    struct FallbackInfo
    {
        core::TileKey key;
        float texOffsetX = 0.0f, texOffsetY = 0.0f;
        float texScaleX = 1.0f, texScaleY = 1.0f;
    };

    bool findFallbackTile(const core::TileKey& missingKey, FallbackInfo& out) const
    {
        if (!pyramid) return false;

        int missingLevel = missingKey.level();
        int missingCol = missingKey.column();
        int missingRow = missingKey.row();
        int slideW = pyramid->slideWidth();
        int slideH = pyramid->slideHeight();
        if (slideW <= 0 || slideH <= 0) return false;

        const auto& missingLvl = pyramid->levelInfo(missingLevel);
        if (missingLvl.tileWidth <= 0 || missingLvl.tileHeight <= 0 ||
            missingLvl.width <= 0 || missingLvl.height <= 0) {
            return false;
        }

        // Compute the missing tile's position as a normalized fraction [0,1] of the full slide.
        // This avoids relying on the 'scale' field for cross-level mapping.
        double missingLevelX = static_cast<double>(missingCol * missingLvl.tileWidth);
        double missingLevelY = static_cast<double>(missingRow * missingLvl.tileHeight);
        double missingLevelW = std::min(static_cast<double>(missingLvl.tileWidth),
                                        static_cast<double>(missingLvl.width) - missingLevelX);
        double missingLevelH = std::min(static_cast<double>(missingLvl.tileHeight),
                                        static_cast<double>(missingLvl.height) - missingLevelY);

        // Normalized position within the full image [0,1]
        double normX = missingLevelX / missingLvl.width;
        double normY = missingLevelY / missingLvl.height;
        double normW = missingLevelW / missingLvl.width;
        double normH = missingLevelH / missingLvl.height;

        // Walk up to coarser levels
        for (int fallbackLevel = missingLevel + 1; fallbackLevel < pyramid->numLevels(); ++fallbackLevel) {
            const auto& fbLvl = pyramid->levelInfo(fallbackLevel);
            if (fbLvl.tileWidth <= 0 || fbLvl.tileHeight <= 0 ||
                fbLvl.width <= 0 || fbLvl.height <= 0) {
                continue;
            }

            // Convert normalized position to fallback level pixel coords
            double fbX = normX * fbLvl.width;
            double fbY = normY * fbLvl.height;

            // Which fallback tile contains this point?
            int fbCol = static_cast<int>(fbX / fbLvl.tileWidth);
            int fbRow = static_cast<int>(fbY / fbLvl.tileHeight);
            fbCol = std::clamp(fbCol, 0, fbLvl.tilesX - 1);
            fbRow = std::clamp(fbRow, 0, fbLvl.tilesY - 1);

            core::TileKey fbKey(fallbackLevel, fbCol, fbRow);

            // Check if this fallback tile is in the texture cache
            if (textures.find(fbKey) != textures.end()) {
                // Actual texture dimensions (clamped for edge tiles)
                double fbTileOriginX = static_cast<double>(fbCol * fbLvl.tileWidth);
                double fbTileOriginY = static_cast<double>(fbRow * fbLvl.tileHeight);
                double fbTexW = std::min(static_cast<double>(fbLvl.tileWidth),
                                         static_cast<double>(fbLvl.width) - fbTileOriginX);
                double fbTexH = std::min(static_cast<double>(fbLvl.tileHeight),
                                         static_cast<double>(fbLvl.height) - fbTileOriginY);
                if (fbTexW <= 0 || fbTexH <= 0) continue;

                // The missing tile's area within the fallback texture, in pixel coords
                double localX = fbX - fbTileOriginX;
                double localY = fbY - fbTileOriginY;
                double localW = normW * fbLvl.width;
                double localH = normH * fbLvl.height;

                // Convert to UV coordinates [0,1] within the actual texture
                out.key = fbKey;
                out.texOffsetX = static_cast<float>(localX / fbTexW);
                out.texOffsetY = static_cast<float>(localY / fbTexH);
                out.texScaleX = static_cast<float>(localW / fbTexW);
                out.texScaleY = static_cast<float>(localH / fbTexH);
                return true;
            }
        }
        return false;
    }

    // Returns true if there are still pending uploads remaining
    bool uploadPendingTextures()
    {
        if (!tileCache) {
            return false;
        }

        // Swap pending list under lock to minimize lock hold time
        std::vector<core::TileKey> toUpload;
        {
            std::lock_guard<std::mutex> lock(pendingUploadsMutex);
            toUpload.swap(pendingUploads);
        }

        if (toUpload.empty()) {
            return false;
        }

        int actualUploads = 0;
        size_t processed = 0;
        for (processed = 0; processed < toUpload.size(); ++processed) {
            if (actualUploads >= kMaxTextureUploadsPerFrame) {
                break;
            }
            const auto& key = toUpload[processed];
            if (textures.find(key) != textures.end()) {
                continue; // Already uploaded
            }
            auto tileData = tileCache->lookup(key);
            if (tileData && !tileData->isEmpty() && !tileData->isError()) {
                GLuint texId = uploadTileTexture(*tileData);
                textures[key] = texId;
                ++actualUploads;
            }
        }

        // Put unprocessed tiles back
        if (processed < toUpload.size()) {
            std::lock_guard<std::mutex> lock(pendingUploadsMutex);
            pendingUploads.insert(pendingUploads.end(),
                toUpload.begin() + static_cast<ptrdiff_t>(processed), toUpload.end());
            return true; // Still have pending
        }
        return false;
    }

    core::TileKey findFallbackTile(const core::TileKey& key) const
    {
        if (!pyramid || !tileCache) {
            return core::TileKey();
        }

        int level = key.level();
        int col = key.column();
        int row = key.row();

        for (int fallbackLevel = level + 1; fallbackLevel < pyramid->numLevels(); ++fallbackLevel) {
            const auto& currentLevelInfo = pyramid->levelInfo(level);
            const auto& fallbackLevelInfo = pyramid->levelInfo(fallbackLevel);

            if (currentLevelInfo.tileWidth <= 0 || currentLevelInfo.tileHeight <= 0 ||
                fallbackLevelInfo.tileWidth <= 0 || fallbackLevelInfo.tileHeight <= 0) {
                continue;
            }

            double scaleRatio = fallbackLevelInfo.scale / currentLevelInfo.scale;
            int fallbackCol = static_cast<int>(std::floor(col * scaleRatio));
            int fallbackRow = static_cast<int>(std::floor(row * scaleRatio));

            fallbackCol = std::max(0, std::min(fallbackCol, fallbackLevelInfo.tilesX - 1));
            fallbackRow = std::max(0, std::min(fallbackRow, fallbackLevelInfo.tilesY - 1));

            core::TileKey fallbackKey(fallbackLevel, fallbackCol, fallbackRow,
                                      key.zIndex(), key.tFrame(), key.colorMode());

            if (textures.find(fallbackKey) != textures.end()) {
                return fallbackKey;
            }
        }

        return core::TileKey();
    }
};

ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Prevent Qt from painting a background over the OpenGL content.
    // Without these, the parent's stylesheet background can cover the FBO.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
}

ViewportWidget::~ViewportWidget()
{
    makeCurrent();
    if (m_impl->glInitialized) {
        m_impl->clearAllTextures();
        m_impl->clearFluorescenceTextures();
        m_impl->deleteSnapshotTexture();
        m_impl->destroyTileFbo();
        if (m_impl->quadVAO) {
            m_impl->gl->glDeleteVertexArrays(1, &m_impl->quadVAO);
        }
        if (m_impl->quadVBO) {
            m_impl->gl->glDeleteBuffers(1, &m_impl->quadVBO);
        }
    }
    doneCurrent();
}

void ViewportWidget::openSlide(const std::string& filePath, const std::string& driverId)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    m_impl->currentDriverId = driverId;
    const uint64_t opId = ++m_impl->openOpId;

    emit loadingStarted(slideDisplayName(QString::fromStdString(filePath)));

    auto statusCallback = [this](QString msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            emit loadingStatusChanged(msg);
        }, Qt::QueuedConnection);
    };

    std::vector<uint8_t> defaultProfileBytes = m_impl->defaultColorProfile;
    std::thread([this, opId, filePath, driverId, statusCallback, defaultProfileBytes]() {
        SceneOpenResult result = openSceneSync(filePath, 0, driverId, statusCallback, defaultProfileBytes);
        // Always enumerate scenes so the scene panel can populate, even if scene 0 worked.
        try {
            auto enumResult = infra::SlideIOAdapter::enumerateScenes(filePath, driverId);
            result.scenes = std::move(enumResult.first);
            result.auxImages = std::move(enumResult.second);
        } catch (const std::exception& ex) {
            spdlog::warn("openSlide: failed to enumerate scenes: {}", ex.what());
        }
        QMetaObject::invokeMethod(this,
            [this, opId, r = std::move(result)]() mutable {
                installSceneOpenResult(opId, std::move(r));
            }, Qt::QueuedConnection);
    }).detach();
}

void ViewportWidget::generateSceneThumbnails()
{
    if (!m_impl->slideOpen || m_impl->currentFilePath.empty()) {
        spdlog::info("generateSceneThumbnails: skipped (slideOpen={}, filePath='{}')",
                     m_impl->slideOpen, m_impl->currentFilePath);
        return;
    }

    // Snapshot inputs and run on a background thread. With the per-tile
    // finer-level fallback active for slides that have a corrupt pyramid
    // level, the coarse-tile assembly inside this loop can take tens of
    // seconds; doing it on the UI thread froze the loading spinner. Each
    // scene gets its own SlideIOAdapter (no shared pool), so this work is
    // isolated from the open viewport's adapter pool.
    const std::string filePath = m_impl->currentFilePath;
    const std::string driverId = m_impl->currentDriverId;
    const auto scenes = m_impl->slideInfo.scenes;
    const uint64_t opId = m_impl->openOpId.load();

    spdlog::info("generateSceneThumbnails: {} scenes, {} aux images",
                 scenes.size(), m_impl->slideInfo.auxImages.size());

    std::thread([this, opId, filePath, driverId, scenes]() {
        for (const auto& sceneInfo : scenes) {
            // Bail if the user opened a different slide in the meantime.
            if (m_impl->openOpId.load() != opId) return;
            try {
                infra::SlideIOAdapter tempAdapter(filePath, sceneInfo.index, driverId);
                auto tempInfo = tempAdapter.slideInfo();

                if (tempInfo.width <= 0 || tempInfo.height <= 0) continue;
                int numCh = tempInfo.numChannels;
                if (numCh <= 0) continue;

                // Both thumbnail paths below read the whole scene, so a scene
                // with no downsampled level would decode every full-resolution
                // tile — tens of minutes per scene on a background thread,
                // with nothing to show for it. Skip it instead.
                if (!hasAffordableOverview(tempAdapter)) {
                    spdlog::info("generateSceneThumbnails: scene {} has no downsampled level; "
                                 "skipping thumbnail", sceneInfo.index);
                    continue;
                }

                // Target thumbnail dimensions are derived from the FULL SCENE dims so
                // SlideIO can pick a finer pyramid level and resample down. Without
                // this, scenes whose coarsest pyramid level is small (e.g., ~200x160)
                // would only produce a tiny coarse thumbnail.
                constexpr int kThumbSize = 512;
                int thumbW = tempInfo.width;
                int thumbH = tempInfo.height;
                if (thumbW > kThumbSize || thumbH > kThumbSize) {
                    double ratio = std::min(static_cast<double>(kThumbSize) / thumbW,
                                            static_cast<double>(kThumbSize) / thumbH);
                    thumbW = std::max(1, static_cast<int>(thumbW * ratio));
                    thumbH = std::max(1, static_cast<int>(thumbH * ratio));
                }

                // For Z-stacks, sample the middle slice — Z=0 is often blank
                // in fluorescence stacks, which would produce an all-black thumbnail.
                const int sceneZ = tempInfo.numZSlices > 1 ? tempInfo.numZSlices / 2 : 0;
                auto blockData = tempAdapter.readBlock(0, 0, tempInfo.width, tempInfo.height,
                                                        thumbW, thumbH, sceneZ);
                QImage thumb;
                if (!blockData.isEmpty() && !blockData.isError()) {
                    thumb = tileDataToQImage(blockData, numCh, tempInfo.channels, tempInfo.isBrightfield);
                }
                if (thumb.isNull()) {
                    spdlog::info("generateSceneThumbnails: scene {} readBlock failed; "
                                 "falling back to coarse-tile assembly", sceneInfo.index);
                    thumb = buildCoarseLevelThumbnailFallback(tempAdapter, numCh, tempInfo.channels,
                                                              tempInfo.isBrightfield, thumbW, thumbH,
                                                              sceneZ);
                }
                if (thumb.isNull()) continue;
                if (m_impl->openOpId.load() != opId) return;

                QImage copy = thumb.copy();
                int idx = sceneInfo.index;
                std::string name = sceneInfo.name;
                QMetaObject::invokeMethod(this, [this, idx, name, copy]() {
                    emit sceneThumbnailReady(idx, false, name, copy);
                }, Qt::QueuedConnection);
            } catch (const std::exception& ex) {
                spdlog::warn("generateSceneThumbnails: failed for scene {}: {}", sceneInfo.index, ex.what());
            }
        }
    }).detach();
}

void ViewportWidget::generateAuxImageThumbnails()
{
    if (!m_impl->slideOpen || m_impl->currentFilePath.empty()) {
        spdlog::info("generateAuxImageThumbnails: skipped (slideOpen={}, filePath='{}')",
                     m_impl->slideOpen, m_impl->currentFilePath);
        return;
    }

    const std::string filePath = m_impl->currentFilePath;
    const std::string driverId = m_impl->currentDriverId;
    const auto auxImages = m_impl->slideInfo.auxImages;
    const uint64_t opId = m_impl->openOpId.load();

    spdlog::info("generateAuxImageThumbnails: {} aux images", auxImages.size());

    std::thread([this, opId, filePath, driverId, auxImages]() {
        for (const auto& auxInfo : auxImages) {
            if (m_impl->openOpId.load() != opId) return;
            try {
                const std::string& auxName = auxInfo.auxiliaryName.empty()
                    ? auxInfo.name : auxInfo.auxiliaryName;
                infra::SlideIOAdapter tempAdapter(filePath, auxName, driverId);
                auto tempInfo = tempAdapter.slideInfo();

                if (tempInfo.width <= 0 || tempInfo.height <= 0) continue;
                int numCh = tempInfo.numChannels;
                if (numCh <= 0) continue;

                // Same guard as the scene thumbnails: never read a whole image
                // that has no downsampled level to read it from.
                if (!hasAffordableOverview(tempAdapter)) {
                    spdlog::info("generateAuxImageThumbnails: aux '{}' has no downsampled level; "
                                 "skipping thumbnail", auxName);
                    continue;
                }

                constexpr int kThumbSize = 512;
                int thumbW = tempInfo.width;
                int thumbH = tempInfo.height;
                if (thumbW > kThumbSize || thumbH > kThumbSize) {
                    double ratio = std::min(static_cast<double>(kThumbSize) / thumbW,
                                            static_cast<double>(kThumbSize) / thumbH);
                    thumbW = std::max(1, static_cast<int>(thumbW * ratio));
                    thumbH = std::max(1, static_cast<int>(thumbH * ratio));
                }

                auto blockData = tempAdapter.readBlock(0, 0, tempInfo.width, tempInfo.height,
                                                        thumbW, thumbH);
                QImage thumb;
                if (!blockData.isEmpty() && !blockData.isError()) {
                    thumb = tileDataToQImage(blockData, numCh, tempInfo.channels, tempInfo.isBrightfield);
                }
                if (thumb.isNull()) {
                    spdlog::info("generateAuxImageThumbnails: aux '{}' readBlock failed; "
                                 "falling back to coarse-tile assembly", auxName);
                    thumb = buildCoarseLevelThumbnailFallback(tempAdapter, numCh, tempInfo.channels,
                                                              tempInfo.isBrightfield, thumbW, thumbH);
                }
                if (thumb.isNull()) continue;
                if (m_impl->openOpId.load() != opId) return;

                // Aux images don't have a scene index; use -1 and identify by name.
                QImage copy = thumb.copy();
                std::string nameCopy = auxName;
                QMetaObject::invokeMethod(this, [this, nameCopy, copy]() {
                    emit sceneThumbnailReady(-1, true, nameCopy, copy);
                }, Qt::QueuedConnection);
            } catch (const std::exception& ex) {
                spdlog::warn("generateAuxImageThumbnails: failed for '{}': {}",
                             auxInfo.name, ex.what());
            }
        }
    }).detach();
}

QImage ViewportWidget::loadAuxImage(const std::string& auxImageName)
{
    if (!m_impl->slideOpen || m_impl->currentFilePath.empty()) {
        return {};
    }
    try {
        infra::SlideIOAdapter adapter(m_impl->currentFilePath, auxImageName,
                                       m_impl->currentDriverId);
        auto info = adapter.slideInfo();
        if (info.width <= 0 || info.height <= 0) return {};
        int numCh = info.numChannels;
        if (numCh <= 0) return {};

        auto block = adapter.readBlock(0, 0, info.width, info.height,
                                        info.width, info.height);
        if (block.isEmpty() || block.isError()) return {};

        QImage out = tileDataToQImage(block, numCh, info.channels, info.isBrightfield);
        if (out.isNull()) return {};
        return out.copy();
    } catch (const std::exception& ex) {
        spdlog::warn("ViewportWidget::loadAuxImage('{}'): {}", auxImageName, ex.what());
        return {};
    }
}

void ViewportWidget::openScene(const std::string& filePath, int sceneIndex,
                               const std::string& driverId)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    m_impl->currentDriverId = driverId;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = QString::fromStdString(filePath);
    const auto slash = std::max(displayName.lastIndexOf('/'), displayName.lastIndexOf('\\'));
    if (slash >= 0) displayName = displayName.mid(slash + 1);
    if (sceneIndex > 0) displayName += QStringLiteral(" (scene %1)").arg(sceneIndex);
    emit loadingStarted(displayName);

    auto statusCallback = [this](QString msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            emit loadingStatusChanged(msg);
        }, Qt::QueuedConnection);
    };

    std::vector<uint8_t> defaultProfileBytes = m_impl->defaultColorProfile;
    std::thread([this, opId, filePath, sceneIndex, driverId, statusCallback, defaultProfileBytes]() {
        SceneOpenResult result = openSceneSync(filePath, sceneIndex, driverId, statusCallback, defaultProfileBytes);
        QMetaObject::invokeMethod(this,
            [this, opId, r = std::move(result)]() mutable {
                installSceneOpenResult(opId, std::move(r));
            }, Qt::QueuedConnection);
    }).detach();
}

void ViewportWidget::closeSlide()
{
    // Bump opId so any in-flight async open result will be discarded when it
    // arrives back on the UI thread, and tell the overlay to come down.
    ++m_impl->openOpId;
    emit loadingFinished();

    if (m_impl->scheduler) {
        m_impl->scheduler->stop();
    }

    m_impl->controller.reset();
    m_impl->scheduler.reset();
    m_impl->pyramid.reset();
    m_impl->tileCache.reset();
    m_impl->slideSource.reset();

    if (m_impl->glInitialized) {
        makeCurrent();
        m_impl->clearAllTextures();
        m_impl->clearFluorescenceTextures();
        m_impl->deleteSnapshotTexture();
        doneCurrent();
    }

    m_impl->slideOpen = false;
    emit slideClosed();
    update();
}

void ViewportWidget::openAuxImage(const std::string& filePath, const std::string& auxImageName,
                                  const std::string& driverId)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    m_impl->currentDriverId = driverId;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = QString::fromStdString(filePath);
    const auto slash = std::max(displayName.lastIndexOf('/'), displayName.lastIndexOf('\\'));
    if (slash >= 0) displayName = displayName.mid(slash + 1);
    displayName += QStringLiteral(" (%1)").arg(QString::fromStdString(auxImageName));
    emit loadingStarted(displayName);

    auto statusCallback = [this](QString msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            emit loadingStatusChanged(msg);
        }, Qt::QueuedConnection);
    };

    std::vector<uint8_t> defaultProfileBytes = m_impl->defaultColorProfile;
    std::thread([this, opId, filePath, auxImageName, driverId, statusCallback, defaultProfileBytes]() {
        SceneOpenResult result = openAuxImageSync(filePath, auxImageName, driverId, statusCallback,
                                                   defaultProfileBytes);
        QMetaObject::invokeMethod(this,
            [this, opId, r = std::move(result)]() mutable {
                installSceneOpenResult(opId, std::move(r));
            }, Qt::QueuedConnection);
    }).detach();
}

void ViewportWidget::installSceneOpenResult(uint64_t opId, SceneOpenResult result)
{
    if (opId != m_impl->openOpId.load()) {
        spdlog::info("ViewportWidget::installSceneOpenResult: discarding stale result (opId={}, current={})",
                     opId, m_impl->openOpId.load());
        return;
    }

    if (!result.success) {
        spdlog::error("ViewportWidget::installSceneOpenResult: open failed: {}", result.errorMsg);
        emit errorOccurred(std::string("Failed to open slide: ") + result.errorMsg);
        emit loadingFinished();
        return;
    }

    // Preserve the prior scenes/auxImages list when the new open targets the
    // same file (a scene switch or aux-image open within the same slide).
    // openSlide populates result.scenes/auxImages via enumerateScenes; the
    // openScene/openAuxImage paths leave them empty, so without this carry-
    // over the scene and associated-images panels would be cleared on every
    // intra-slide click.
    const bool sameFile = (result.filePath == m_impl->slideInfo.filePath);
    std::vector<core::SceneInfo> carriedScenes;
    std::vector<core::SceneInfo> carriedAuxImages;
    if (sameFile && result.scenes.empty()) {
        carriedScenes = m_impl->slideInfo.scenes;
    }
    if (sameFile && result.auxImages.empty()) {
        carriedAuxImages = m_impl->slideInfo.auxImages;
    }

    m_impl->slideSource = std::move(result.slideSource);
    m_impl->tileCache = std::move(result.tileCache);
    m_impl->pyramid = std::move(result.pyramid);
    m_impl->slideInfo = std::move(result.slideInfo);
    if (!result.scenes.empty()) {
        m_impl->slideInfo.scenes = std::move(result.scenes);
    } else if (!carriedScenes.empty()) {
        m_impl->slideInfo.scenes = std::move(carriedScenes);
    }
    if (!result.auxImages.empty()) {
        m_impl->slideInfo.auxImages = std::move(result.auxImages);
    } else if (!carriedAuxImages.empty()) {
        m_impl->slideInfo.auxImages = std::move(carriedAuxImages);
    }

    m_impl->scheduler = std::make_shared<infra::TileLoadScheduler>(
        m_impl->slideSource, m_impl->tileCache);
    m_impl->scheduler->setOnTileLoaded([this](const core::TileKey& key) {
        {
            std::lock_guard<std::mutex> lock(m_impl->pendingUploadsMutex);
            m_impl->pendingUploads.push_back(key);
        }
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    });

    m_impl->controller = std::make_unique<ViewportController>(
        m_impl->pyramid, m_impl->tileCache, nullptr);
    m_impl->controller->setSlide(m_impl->slideInfo, *m_impl->pyramid);
    if (width() > 0 && height() > 0) {
        m_impl->controller->resize(width(), height());
        m_impl->controller->fitToSlide();
    }
    // Default to the middle slice for Z-stacks. Z=0 is often blank in
    // fluorescence stacks (out-of-focus top slice), so opening there would
    // show a black viewport even when the data is correct.
    m_impl->currentZSlice = m_impl->slideInfo.numZSlices > 1
        ? m_impl->slideInfo.numZSlices / 2 : 0;
    m_impl->currentTFrame = 0;
    m_impl->controller->setZT(m_impl->currentZSlice, m_impl->currentTFrame);
    m_impl->controller->setScheduler(m_impl->scheduler);
    m_impl->controller->requestVisibleTiles();

    m_impl->slideOpen = true;

    spdlog::info("ViewportWidget::installSceneOpenResult: slide {}x{}, {} levels, {} channels",
                 m_impl->slideInfo.width, m_impl->slideInfo.height,
                 m_impl->slideInfo.numZoomLevels, m_impl->slideInfo.numChannels);
    spdlog::info("installSceneOpenResult: slide-wide displayRange min={} max={} autoDetected={}",
                 m_impl->slideInfo.displayRange.displayMin,
                 m_impl->slideInfo.displayRange.displayMax,
                 m_impl->slideInfo.displayRange.autoDetected);
    for (size_t ch = 0; ch < m_impl->slideInfo.channels.size(); ++ch) {
        const auto& dr = m_impl->slideInfo.channels[ch].displayRange;
        spdlog::info("installSceneOpenResult: channel {} displayRange min={} max={} autoDetected={}",
                     ch, dr.displayMin, dr.displayMax, dr.autoDetected);
    }

    if (!result.thumbnail.isNull()) {
        emit thumbnailReady(result.thumbnail);
    }
    emit slideOpened(result.filePath);
    emit loadingFinished();
    emit viewportChanged();
    update();
}

const std::string& ViewportWidget::currentFilePath() const
{
    return m_impl->currentFilePath;
}

bool ViewportWidget::isSlideOpen() const
{
    return m_impl->slideOpen;
}

ViewportController* ViewportWidget::controller() const
{
    return m_impl->controller.get();
}

GpuInfo ViewportWidget::glInfo() const
{
    return m_impl->gpuInfo;
}

void ViewportWidget::fitToSlide()
{
    if (m_impl->controller) {
        if (m_impl->glInitialized && m_impl->slideOpen) {
            m_impl->activateSnapshot();
        }
        m_impl->controller->fitToSlide();
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::setActualPixels()
{
    if (m_impl->controller) {
        if (m_impl->glInitialized && m_impl->slideOpen) {
            m_impl->activateSnapshot();
        }
        m_impl->controller->setActualPixels();
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::zoomIn()
{
    if (m_impl->controller) {
        if (m_impl->glInitialized && m_impl->slideOpen) {
            m_impl->activateSnapshot();
        }
        double cx = width() / 2.0;
        double cy = height() / 2.0;
        m_impl->controller->zoomToPoint(cx, cy, kZoomInFactor);
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::zoomOut()
{
    if (m_impl->controller) {
        if (m_impl->glInitialized && m_impl->slideOpen) {
            m_impl->activateSnapshot();
        }
        double cx = width() / 2.0;
        double cy = height() / 2.0;
        m_impl->controller->zoomToPoint(cx, cy, kZoomOutFactor);
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::panByPixels(double dx, double dy)
{
    if (m_impl->controller) {
        m_impl->controller->pan(dx, dy);
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::setChannelSettings(const std::vector<core::ChannelInfo>& channels)
{
    for (size_t ch = 0; ch < channels.size(); ++ch) {
        const auto& dr = channels[ch].displayRange;
        spdlog::info("setChannelSettings: incoming ch {} displayRange min={} max={} autoDetected={} color=({:.2f},{:.2f},{:.2f}) intensity={:.2f} visible={}",
                     ch, dr.displayMin, dr.displayMax, dr.autoDetected,
                     channels[ch].colorR, channels[ch].colorG, channels[ch].colorB,
                     channels[ch].intensity, channels[ch].visible);
    }
    if (channels.size() == m_impl->slideInfo.channels.size()) {
        m_impl->slideInfo.channels = channels;
        update();
    }
}

void ViewportWidget::setDefaultColorProfile(std::vector<uint8_t> bytes)
{
    m_impl->defaultColorProfile = std::move(bytes);
}

void ViewportWidget::setColorMode(core::ColorMode mode)
{
    if (!m_impl->slideSource || !m_impl->controller) {
        return;
    }
    m_impl->slideSource->setColorMode(mode);
    m_impl->controller->setColorMode(mode);
    releaseTexturesOfOtherMode(mode);
    // No cache clear and no scheduler cancellation: the mode is part of the key,
    // so tiles of the other mode are simply not looked up, and any read already
    // in flight files its result under the key it was issued with.
    m_impl->controller->requestVisibleTiles();
    update();
}

core::ColorProfileInfo ViewportWidget::activeColorProfileInfo() const
{
    if (!m_impl->slideSource) {
        return {};
    }
    return m_impl->slideSource->activeColorProfileInfo();
}

void ViewportWidget::releaseTexturesOfOtherMode(core::ColorMode keep)
{
    if (!m_impl->gl) {
        return;
    }
    makeCurrent();
    for (auto it = m_impl->textures.begin(); it != m_impl->textures.end(); ) {
        if (it->first.colorMode() != keep) {
            m_impl->gl->glDeleteTextures(1, &it->second);
            it = m_impl->textures.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_impl->fluorescenceTextures.begin();
         it != m_impl->fluorescenceTextures.end(); ) {
        if (it->first.colorMode() != keep) {
            for (GLuint id : it->second.channelTexIds) {
                m_impl->gl->glDeleteTextures(1, &id);
            }
            it = m_impl->fluorescenceTextures.erase(it);
        } else {
            ++it;
        }
    }
    doneCurrent();
}

void ViewportWidget::setZSlice(int zIndex)
{
    if (!m_impl->slideOpen || !m_impl->controller) return;
    if (zIndex == m_impl->currentZSlice) return;

    // Activate the warm snapshot of the previous frame so the transition shows
    // the prior Z slice as a backdrop while new-Z tiles stream in, instead of
    // briefly flashing the cleared background.
    if (m_impl->glInitialized) {
        m_impl->activateSnapshot();
    }

    m_impl->currentZSlice = zIndex;
    m_impl->controller->setZT(zIndex, m_impl->currentTFrame);

    // Clear textures and cache — tiles at the old Z are no longer valid
    makeCurrent();
    m_impl->clearAllTextures();
    m_impl->clearFluorescenceTextures();
    doneCurrent();
    m_impl->tileCache->clear();

    m_impl->controller->requestVisibleTiles();
    update();
}

void ViewportWidget::setTFrame(int tFrame)
{
    if (!m_impl->slideOpen || !m_impl->controller) return;
    if (tFrame == m_impl->currentTFrame) return;

    if (m_impl->glInitialized) {
        m_impl->activateSnapshot();
    }

    m_impl->currentTFrame = tFrame;
    m_impl->controller->setZT(m_impl->currentZSlice, tFrame);

    makeCurrent();
    m_impl->clearAllTextures();
    m_impl->clearFluorescenceTextures();
    doneCurrent();
    m_impl->tileCache->clear();

    m_impl->controller->requestVisibleTiles();
    update();
}

int ViewportWidget::currentZSlice() const
{
    return m_impl->currentZSlice;
}

int ViewportWidget::currentTFrame() const
{
    return m_impl->currentTFrame;
}

const core::SlideInfo& ViewportWidget::slideInfo() const
{
    return m_impl->slideInfo;
}

void ViewportWidget::initializeGL()
{
    m_impl->gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(QOpenGLContext::currentContext());
    if (!m_impl->gl) {
        return;
    }
    m_impl->gl->initializeOpenGLFunctions();

    // Record what we are actually rendering on, for the About dialog and the
    // log. A driver may return null for any of these, so do not hand null to
    // QString.
    const auto glString = [this](GLenum name) {
        const GLubyte* value = m_impl->gl->glGetString(name);
        return value ? QString::fromLatin1(reinterpret_cast<const char*>(value)) : QString();
    };
    m_impl->gpuInfo.vendor = glString(GL_VENDOR);
    m_impl->gpuInfo.renderer = glString(GL_RENDERER);
    m_impl->gpuInfo.version = glString(GL_VERSION);
    m_impl->gpuInfo.shadingLanguageVersion = glString(GL_SHADING_LANGUAGE_VERSION);
    spdlog::info("ViewportWidget: OpenGL renderer '{}', version '{}'",
                 m_impl->gpuInfo.renderer.toStdString(), m_impl->gpuInfo.version.toStdString());

    m_impl->gl->glClearColor(0.251f, 0.251f, 0.251f, 1.0f); // #404040

    m_impl->gl->glEnable(GL_BLEND);
    m_impl->gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_impl->tileShader = std::make_unique<QOpenGLShaderProgram>();
    bool vsOk = m_impl->tileShader->addShaderFromSourceCode(QOpenGLShader::Vertex, kTileVertexShaderSource);
    if (!vsOk) {
        spdlog::error("ViewportWidget: vertex shader compilation failed: {}",
                      m_impl->tileShader->log().toStdString());
    }
    bool fsOk = m_impl->tileShader->addShaderFromSourceCode(QOpenGLShader::Fragment, kTileFragmentShaderSource);
    if (!fsOk) {
        spdlog::error("ViewportWidget: fragment shader compilation failed: {}",
                      m_impl->tileShader->log().toStdString());
    }
    bool linkOk = m_impl->tileShader->link();
    if (!linkOk) {
        spdlog::error("ViewportWidget: shader program link failed: {}",
                      m_impl->tileShader->log().toStdString());
    }
    spdlog::info("ViewportWidget: shader compiled: vs={} fs={} link={}", vsOk, fsOk, linkOk);

    // Compile channel (fluorescence) shader
    m_impl->channelShader = std::make_unique<QOpenGLShaderProgram>();
    m_impl->channelShader->addShaderFromSourceCode(QOpenGLShader::Vertex, kTileVertexShaderSource);
    m_impl->channelShader->addShaderFromSourceCode(QOpenGLShader::Fragment, kChannelFragmentShaderSource);
    if (!m_impl->channelShader->link()) {
        spdlog::error("ViewportWidget: channel shader link failed: {}",
                      m_impl->channelShader->log().toStdString());
    }

    // Compile snapshot shader (raw framebuffer texture passthrough)
    m_impl->snapshotShader = std::make_unique<QOpenGLShaderProgram>();
    m_impl->snapshotShader->addShaderFromSourceCode(QOpenGLShader::Vertex, kTileVertexShaderSource);
    m_impl->snapshotShader->addShaderFromSourceCode(QOpenGLShader::Fragment, kSnapshotFragmentShaderSource);
    if (!m_impl->snapshotShader->link()) {
        spdlog::error("ViewportWidget: snapshot shader link failed: {}",
                      m_impl->snapshotShader->log().toStdString());
    }

    // Compile composite shader (used to draw tile FBO over snapshot)
    m_impl->compositeShader = std::make_unique<QOpenGLShaderProgram>();
    m_impl->compositeShader->addShaderFromSourceCode(QOpenGLShader::Vertex, kTileVertexShaderSource);
    m_impl->compositeShader->addShaderFromSourceCode(QOpenGLShader::Fragment, kCompositeFragmentShaderSource);
    if (!m_impl->compositeShader->link()) {
        spdlog::error("ViewportWidget: composite shader link failed: {}",
                      m_impl->compositeShader->log().toStdString());
    }

    m_impl->createQuadGeometry();
    spdlog::info("ViewportWidget: GL initialized, VAO={} VBO={}", m_impl->quadVAO, m_impl->quadVBO);

    m_impl->glInitialized = true;
}

void ViewportWidget::resizeGL(int /*w*/, int /*h*/)
{
    // Viewport and controller use logical (widget) pixels.
    // The actual glViewport is set in paintGL using devicePixelRatio.
    if (m_impl->controller) {
        m_impl->controller->resize(width(), height());
        emit viewportChanged();
    }
}

void ViewportWidget::paintGL()
{
    if (!m_impl->gl || !m_impl->glInitialized) {
        return;
    }

    // Use framebuffer dimensions for both viewport and shader
    int fbWidth = static_cast<int>(width() * devicePixelRatioF());
    int fbHeight = static_cast<int>(height() * devicePixelRatioF());
    m_impl->gl->glViewport(0, 0, fbWidth, fbHeight);

    // Periphery (outside the slide rect) stays the neutral chrome gray
    // regardless of slide type — what the user sees before any slide is open.
    m_impl->gl->glClearColor(0.251f, 0.251f, 0.251f, 1.0f);
    m_impl->gl->glClear(GL_COLOR_BUFFER_BIT);

    if (!m_impl->controller || !m_impl->slideOpen) {
        return;
    }

    // Inside the slide rect, fill the background with a color that matches
    // what an untiled region "should" look like: white for brightfield H&E
    // (slide glass) so a failed tile reads as background, black for
    // fluorescence (FBO composite's additive-neutral). Only applied when the
    // slide kind is unambiguous (>=3 channels) — 1-channel slides could be
    // grayscale brightfield OR grayscale fluorescence and the isBrightfield
    // heuristic in SlideIOAdapter flags both as brightfield, which would
    // paint white under a dark fluorescence image. Falling through to the
    // chrome gray keeps the slide-rect / periphery boundary invisible.
    if (m_impl->slideInfo.numChannels >= 3) {
        const auto& vpInfo = m_impl->controller->viewport();
        double tlx = 0.0, tly = 0.0, brx = 0.0, bry = 0.0;
        vpInfo.slideToScreen(0.0, 0.0, tlx, tly);
        vpInfo.slideToScreen(static_cast<double>(m_impl->slideInfo.width),
                             static_cast<double>(m_impl->slideInfo.height),
                             brx, bry);
        const double dpr = devicePixelRatioF();
        int sx = static_cast<int>(std::floor(tlx * dpr));
        int sy = static_cast<int>(std::floor(tly * dpr));
        int sw = static_cast<int>(std::ceil((brx - tlx) * dpr));
        int sh = static_cast<int>(std::ceil((bry - tly) * dpr));
        int x0 = std::max(0, sx);
        int y0 = std::max(0, sy);
        int x1 = std::min(fbWidth, sx + sw);
        int y1 = std::min(fbHeight, sy + sh);
        if (x1 > x0 && y1 > y0) {
            // glScissor uses bottom-left origin, screen Y is top-down.
            int scissorY = fbHeight - y1;
            int scissorH = y1 - y0;
            int scissorW = x1 - x0;
            if (m_impl->slideInfo.isBrightfield) {
                m_impl->gl->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            } else {
                m_impl->gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            }
            m_impl->gl->glEnable(GL_SCISSOR_TEST);
            m_impl->gl->glScissor(x0, scissorY, scissorW, scissorH);
            m_impl->gl->glClear(GL_COLOR_BUFFER_BIT);
            m_impl->gl->glDisable(GL_SCISSOR_TEST);
        }
    }

    bool morePending = m_impl->uploadPendingTextures();

    auto visibleKeys = m_impl->controller->visibleTileKeys();
    const auto& viewport = m_impl->controller->viewport();

    static int paintCount = 0;
    if (++paintCount <= 5 || paintCount % 100 == 0) {
        spdlog::info("paintGL[{}]: {} visible tiles, {} textures uploaded, scale={:.4f}, "
                     "viewport={}x{}, fb={}x{}, widget={}x{}, dpr={:.2f}, brightfield={}",
                     paintCount, visibleKeys.size(), m_impl->textures.size(),
                     viewport.scale(), viewport.screenWidth(), viewport.screenHeight(),
                     fbWidth, fbHeight, width(), height(), devicePixelRatioF(),
                     m_impl->slideInfo.isBrightfield);
    }

    if (visibleKeys.empty()) {
        return;
    }

    // Render zoom snapshot as immediate visual feedback before tile rendering
    if (m_impl->snapshotActive) {
        m_impl->renderSnapshot(viewport);
    }

    core::CoordinateSystem coordSystem(*m_impl->pyramid);
    int tilesRendered = 0;
    int tilesSkipped = 0;

    if (m_impl->slideInfo.isBrightfield && m_impl->slideInfo.numChannels <= 1) {
        // --- Single-channel brightfield rendering path ---
        // Direct tile sampling with a single channel-color * intensity tint
        // pulled from slideInfo.channels[0]. The default brightfield color
        // for a single-channel slide is white, so an unmoved slider at 1.0×
        // reproduces native grayscale. (1-channel fluorescence falls through
        // to the channel-mix path below.)
        m_impl->tileShader->bind();
        m_impl->tileShader->setUniformValue("uViewportSize",
            static_cast<float>(viewport.screenWidth()), static_cast<float>(viewport.screenHeight()));

        m_impl->gl->glBindVertexArray(m_impl->quadVAO);
        m_impl->gl->glActiveTexture(GL_TEXTURE0);
        m_impl->tileShader->setUniformValue("uTileTexture", 0);

        auto [samplerMin, samplerMax] = toSamplerRange(
            m_impl->slideInfo.displayRange.displayMin,
            m_impl->slideInfo.displayRange.displayMax,
            m_impl->slideInfo.channelDataType);
        m_impl->tileShader->setUniformValue("uDisplayMin", samplerMin);
        m_impl->tileShader->setUniformValue("uDisplayMax", samplerMax);

        // Channel color × intensity from the channel mixer panel. When the
        // channel is hidden or the channels vector is unexpectedly empty,
        // pass black so the slide is suppressed instead of rendering with
        // stale uniform values from a previous frame.
        float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
        if (!m_impl->slideInfo.channels.empty()) {
            const auto& ch0 = m_impl->slideInfo.channels.front();
            if (ch0.visible) {
                float intensity = std::clamp(ch0.intensity, 0.0f, 4.0f);
                tintR = ch0.colorR * intensity;
                tintG = ch0.colorG * intensity;
                tintB = ch0.colorB * intensity;
            } else {
                tintR = tintG = tintB = 0.0f;
            }
        }
        m_impl->tileShader->setUniformValue("uChannelColor", tintR, tintG, tintB);

        // Default tex coord uniforms (full texture, no sub-region)
        m_impl->tileShader->setUniformValue("uTexCoordOffset", 0.0f, 0.0f);
        m_impl->tileShader->setUniformValue("uTexCoordScale", 1.0f, 1.0f);
        m_impl->tileShader->setUniformValue("uAlpha", 1.0f);

        // Pass 1: Render cached coarser-level tiles as a blurry background.
        // Walk from coarsest to the level just above the current visible level.
        int currentLevel = coordSystem.bestLevel(viewport.scale());
        for (int lvl = m_impl->pyramid->numLevels() - 1; lvl > currentLevel; --lvl) {
            // Fully stamped with the active colour mode and plane -- see
            // CoordinateSystem::coarseTiles for why that matters.
            auto coarseTiles = coordSystem.coarseTiles(viewport, lvl,
                                                       m_impl->controller->colorMode(),
                                                       m_impl->currentZSlice,
                                                       m_impl->currentTFrame);

            for (const auto& fbKey : coarseTiles) {
                auto fbTexIt = m_impl->textures.find(fbKey);
                if (fbTexIt == m_impl->textures.end()) {
                    // Also check the tile cache for un-uploaded tiles
                    auto tileData = m_impl->tileCache->lookup(fbKey);
                    if (tileData && !tileData->isEmpty() && !tileData->isError()) {
                        GLuint texId = m_impl->uploadTileTexture(*tileData);
                        m_impl->textures[fbKey] = texId;
                        fbTexIt = m_impl->textures.find(fbKey);
                    } else {
                        continue;
                    }
                }

                auto fbScreenRect = coordSystem.tileScreenRect(fbKey, viewport);
                m_impl->tileShader->setUniformValue("uScreenRect",
                    static_cast<float>(fbScreenRect.x),
                    static_cast<float>(fbScreenRect.y),
                    static_cast<float>(fbScreenRect.width),
                    static_cast<float>(fbScreenRect.height));

                m_impl->gl->glBindTexture(GL_TEXTURE_2D, fbTexIt->second);
                m_impl->gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        // Pass 2: Render actual visible tiles on top (overwrites blurry background).
        for (const auto& key : visibleKeys) {
            auto screenRect = coordSystem.tileScreenRect(key, viewport);

            GLuint texId = 0;

            auto texIt = m_impl->textures.find(key);
            if (texIt != m_impl->textures.end()) {
                texId = texIt->second;
            } else {
                auto tileData = m_impl->tileCache->lookup(key);
                if (tileData && !tileData->isEmpty() && !tileData->isError()) {
                    texId = m_impl->uploadTileTexture(*tileData);
                    m_impl->textures[key] = texId;
                } else if (tileData && tileData->isError()) {
                    // Permanent read failure: don't retry, don't trigger
                    // the tilesSkipped repaint storm. Region falls through
                    // to the clear color (white for brightfield).
                    continue;
                } else {
                    ++tilesSkipped;
                    continue;
                }
            }

            m_impl->tileShader->setUniformValue("uScreenRect",
                static_cast<float>(screenRect.x),
                static_cast<float>(screenRect.y),
                static_cast<float>(screenRect.width),
                static_cast<float>(screenRect.height));

            m_impl->gl->glBindTexture(GL_TEXTURE_2D, texId);
            m_impl->gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            ++tilesRendered;
        }

        m_impl->gl->glBindVertexArray(0);
        m_impl->gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_impl->tileShader->release();

    } else {
        // --- Fluorescence rendering path ---
        // Tiles use additive blending across channels. To keep additive blending
        // confined to a tile pass (and prevent over-exposure with whatever is
        // already on the main framebuffer), tiles are rendered into an offscreen
        // FBO that is then composited over the main framebuffer with normal alpha
        // blending. Coarse fallback and fine current-level tiles run in TWO
        // SEPARATE passes (each with its own clear+composite), so coarse content
        // shows through under untiled fine areas without additively double-
        // exposing where fine tiles overlap.
        const GLuint defaultFbo = static_cast<GLuint>(defaultFramebufferObject());
        m_impl->ensureTileFbo(fbWidth, fbHeight);

        // Helper: render all visible channels of a fluorescence tile additively
        // at the given screen rect (assumes channelShader bound, additive blend on).
        auto drawFluorescenceTile = [&](const Impl::TileTextures& texs, const auto& screenRect) {
            m_impl->channelShader->setUniformValue("uScreenRect",
                static_cast<float>(screenRect.x),
                static_cast<float>(screenRect.y),
                static_cast<float>(screenRect.width),
                static_cast<float>(screenRect.height));

            for (size_t ch = 0; ch < texs.channelTexIds.size(); ++ch) {
                if (ch >= m_impl->slideInfo.channels.size()) break;
                const auto& chInfo = m_impl->slideInfo.channels[ch];
                if (!chInfo.visible) continue;

                // Fall back to the slide's global display range when per-channel
                // autodetection didn't run (e.g. single-channel fluorescence,
                // which historically took the brightfield path). Without this the
                // default {0,255} clamps Gray16 data into saturation and the
                // entire image renders as a flat color.
                const bool useChannelRange = chInfo.userOverrideRange || chInfo.displayRange.autoDetected;
                const double dispMin = useChannelRange
                    ? chInfo.displayRange.displayMin
                    : m_impl->slideInfo.displayRange.displayMin;
                const double dispMax = useChannelRange
                    ? chInfo.displayRange.displayMax
                    : m_impl->slideInfo.displayRange.displayMax;
                auto [chSamplerMin, chSamplerMax] = toSamplerRange(dispMin, dispMax, chInfo.dataType);
                m_impl->channelShader->setUniformValue("uDisplayMin", chSamplerMin);
                m_impl->channelShader->setUniformValue("uDisplayMax", chSamplerMax);
                float intensity = std::clamp(chInfo.intensity, 0.0f, 4.0f);
                m_impl->channelShader->setUniformValue("uChannelColor",
                    chInfo.colorR * intensity, chInfo.colorG * intensity, chInfo.colorB * intensity);

                m_impl->gl->glBindTexture(GL_TEXTURE_2D, texs.channelTexIds[ch]);
                m_impl->gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        };

        // Helper: composite the tile FBO onto the default framebuffer using normal
        // alpha blending, with full texture coverage and Y-flip (since the FBO was
        // written with the screen-Y-down vertex shader but textures sample Y-up).
        auto compositeTileFboToMain = [&]() {
            m_impl->gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            m_impl->gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
            m_impl->gl->glViewport(0, 0, fbWidth, fbHeight);

            m_impl->compositeShader->bind();
            m_impl->compositeShader->setUniformValue("uViewportSize",
                static_cast<float>(viewport.screenWidth()), static_cast<float>(viewport.screenHeight()));
            m_impl->compositeShader->setUniformValue("uScreenRect",
                0.0f, 0.0f,
                static_cast<float>(viewport.screenWidth()),
                static_cast<float>(viewport.screenHeight()));
            m_impl->compositeShader->setUniformValue("uTexCoordOffset", 0.0f, 1.0f);
            m_impl->compositeShader->setUniformValue("uTexCoordScale", 1.0f, -1.0f);
            m_impl->compositeShader->setUniformValue("uSourceTexture", 0);

            m_impl->gl->glBindVertexArray(m_impl->quadVAO);
            m_impl->gl->glActiveTexture(GL_TEXTURE0);
            m_impl->gl->glBindTexture(GL_TEXTURE_2D, m_impl->tileFboTexture);
            m_impl->gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            m_impl->gl->glBindTexture(GL_TEXTURE_2D, 0);
            m_impl->gl->glBindVertexArray(0);
            m_impl->compositeShader->release();
        };

        // Helper: prepare the tile FBO for a fresh additive tile pass.
        auto beginTileFboPass = [&]() {
            m_impl->gl->glBindFramebuffer(GL_FRAMEBUFFER, m_impl->tileFbo);
            m_impl->gl->glViewport(0, 0, fbWidth, fbHeight);
            m_impl->gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            m_impl->gl->glClear(GL_COLOR_BUFFER_BIT);

            m_impl->channelShader->bind();
            m_impl->channelShader->setUniformValue("uViewportSize",
                static_cast<float>(viewport.screenWidth()), static_cast<float>(viewport.screenHeight()));
            m_impl->channelShader->setUniformValue("uTileTexture", 0);
            m_impl->channelShader->setUniformValue("uTexCoordOffset", 0.0f, 0.0f);
            m_impl->channelShader->setUniformValue("uTexCoordScale", 1.0f, 1.0f);

            m_impl->gl->glBindVertexArray(m_impl->quadVAO);
            m_impl->gl->glActiveTexture(GL_TEXTURE0);
            m_impl->gl->glEnable(GL_BLEND);
            m_impl->gl->glBlendFunc(GL_ONE, GL_ONE);
        };

        auto endTileFboPass = [&]() {
            m_impl->gl->glBindVertexArray(0);
            m_impl->gl->glBindTexture(GL_TEXTURE_2D, 0);
            m_impl->channelShader->release();
        };

        // === PHASE 1: Coarse fallback ===
        // Walk pyramid levels coarsest -> finest. Each level is rendered into
        // the FBO in its own additive pass then composited onto the main
        // framebuffer so finer levels overwrite coarser ones in shared regions.
        // (Doing all levels in one additive pass would sum them and over-expose.)
        int currentLevel = coordSystem.bestLevel(viewport.scale());
        for (int lvl = m_impl->pyramid->numLevels() - 1; lvl > currentLevel; --lvl) {
            // Fully stamped with the active colour mode and plane -- see
            // CoordinateSystem::coarseTiles for why that matters.
            auto coarseTiles = coordSystem.coarseTiles(viewport, lvl,
                                                       m_impl->controller->colorMode(),
                                                       m_impl->currentZSlice,
                                                       m_impl->currentTFrame);

            bool drewAnyAtLevel = false;
            beginTileFboPass();
            for (const auto& fbKey : coarseTiles) {
                auto fbTexIt = m_impl->fluorescenceTextures.find(fbKey);
                if (fbTexIt == m_impl->fluorescenceTextures.end()) {
                    auto tileData = m_impl->tileCache->lookup(fbKey);
                    if (tileData && !tileData->isEmpty() && !tileData->isError()) {
                        m_impl->fluorescenceTextures[fbKey] = m_impl->uploadTileChannelTextures(*tileData);
                        fbTexIt = m_impl->fluorescenceTextures.find(fbKey);
                    } else {
                        continue;
                    }
                }
                auto fbScreenRect = coordSystem.tileScreenRect(fbKey, viewport);
                drawFluorescenceTile(fbTexIt->second, fbScreenRect);
                drewAnyAtLevel = true;
            }
            endTileFboPass();
            if (drewAnyAtLevel) {
                compositeTileFboToMain();
            }
        }

        // === PHASE 2: Fine current-level tiles ===
        // Fresh FBO clear so coarse content from phase 1 doesn't additively
        // double-expose with fine tiles. Composite over the main framebuffer
        // (which now has snapshot + coarse) with normal alpha blending.
        beginTileFboPass();
        for (const auto& key : visibleKeys) {
            auto screenRect = coordSystem.tileScreenRect(key, viewport);

            auto texIt = m_impl->fluorescenceTextures.find(key);
            if (texIt == m_impl->fluorescenceTextures.end()) {
                auto tileData = m_impl->tileCache->lookup(key);
                if (!tileData || tileData->isEmpty()) {
                    ++tilesSkipped;
                    continue;
                }
                if (tileData->isError()) {
                    // Permanent read failure: don't retry, don't trigger
                    // the tilesSkipped repaint storm. Region falls through
                    // to the clear color (white for brightfield).
                    continue;
                }
                m_impl->fluorescenceTextures[key] = m_impl->uploadTileChannelTextures(*tileData);
                texIt = m_impl->fluorescenceTextures.find(key);
            }

            drawFluorescenceTile(texIt->second, screenRect);
            ++tilesRendered;
        }
        endTileFboPass();
        compositeTileFboToMain();
    }

    if (paintCount <= 5 || paintCount % 100 == 0) {
        spdlog::info("paintGL[{}]: rendered={} skipped={}", paintCount, tilesRendered, tilesSkipped);
    }

    // If tiles were skipped (not yet loaded), schedule repaints until all are rendered
    if (tilesSkipped > 0 || morePending) {
        // Use a short timer to allow tile workers to make progress
        QTimer::singleShot(16, this, [this]() { update(); });
    } else {
        // All tiles rendered — deactivate the zoom snapshot and capture a fresh
        // snapshot of this complete frame for the next zoom operation
        m_impl->snapshotActive = false;
        m_impl->captureSnapshotTexture(this, viewport);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_impl->isPanning = true;
        m_impl->lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QOpenGLWidget::mousePressEvent(event);
    }
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_impl->isPanning) {
        m_impl->isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QOpenGLWidget::mouseReleaseEvent(event);
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_impl->isPanning && m_impl->controller) {
        QPoint delta = event->pos() - m_impl->lastMousePos;
        m_impl->controller->pan(static_cast<double>(delta.x()),
                                static_cast<double>(delta.y()));
        m_impl->lastMousePos = event->pos();
        emit viewportChanged();
        update();
        event->accept();
    }

    if (m_impl->controller) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(
            static_cast<double>(event->pos().x()),
            static_cast<double>(event->pos().y()),
            slideX, slideY);
        emit cursorMoved(slideX, slideY);
    }

    if (!event->isAccepted()) {
        QOpenGLWidget::mouseMoveEvent(event);
    }
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_impl->controller) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    double angleDelta = event->angleDelta().y();
    if (std::abs(angleDelta) < 1.0) {
        event->accept();
        return;
    }

    // Record viewport state before zoom for smooth visual transition
    if (m_impl->glInitialized && m_impl->slideOpen) {
        m_impl->activateSnapshot();
    }

    double steps = angleDelta / 120.0;
    double factor = std::pow(kWheelZoomFactor, steps);

    auto pos = event->position();
    m_impl->controller->zoomToPoint(pos.x(), pos.y(), factor);
    emit viewportChanged();
    update();
    event->accept();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_impl->controller) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Left:
        m_impl->controller->pan(kPanPixels, 0.0);
        break;
    case Qt::Key_Right:
        m_impl->controller->pan(-kPanPixels, 0.0);
        break;
    case Qt::Key_Up:
        m_impl->controller->pan(0.0, kPanPixels);
        break;
    case Qt::Key_Down:
        m_impl->controller->pan(0.0, -kPanPixels);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        return; // zoomIn already emits and updates
    case Qt::Key_Minus:
        zoomOut();
        return; // zoomOut already emits and updates
    default:
        handled = false;
        break;
    }

    if (handled) {
        emit viewportChanged();
        update();
        event->accept();
    } else {
        QOpenGLWidget::keyPressEvent(event);
    }
}

} // namespace slideio::viewer::ui
