#include "slideio/viewer/ui/ViewportWidget.h"
#include "slideio/viewer/ui/ViewportController.h"

#include "slideio/viewer/core/CoordinateSystem.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/infra/LruTileCache.h"
#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/infra/SlideIOAdapterPool.h"
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
    std::shared_ptr<infra::SlideIOAdapterPool> adapterPool;
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

out vec4 fragColor;

void main()
{
    vec4 texel = texture(uTileTexture, vTexCoord);
    float range = uDisplayMax - uDisplayMin;
    float invRange = (range > 0.0) ? (1.0 / range) : 1.0;
    vec3 mapped = clamp((texel.rgb - uDisplayMin) * invRange, 0.0, 1.0);
    fragColor = vec4(mapped, texel.a * uAlpha);
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
    for (size_t p = 1; p < pixelCount; ++p) {
        T v = typed[p * numChannels + channelIndex];
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

// Map a pixel element from native type through [min,max] -> [0,255] for thumbnails
uint8_t mapPixelToUint8(const uint8_t* buffer, size_t elementIndex,
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
    if (range <= 0.0) return 0;
    double normalized = (value - minVal) / range;
    return static_cast<uint8_t>(std::clamp(normalized * 255.0, 0.0, 255.0));
}

// Read the coarsest pyramid level synchronously: detect global and per-channel
// display ranges, build a thumbnail QImage, and insert the tiles into the
// cache so the very first paint has a base layer to show. Throws on errors;
// caller wraps in try/catch.
void readCoarseLevelAndBuildThumbnail(slideio::viewer::infra::SlideIOAdapterPool& pool,
                                      slideio::viewer::core::SlideInfo& slideInfo,
                                      const slideio::viewer::core::TilePyramid& pyramid,
                                      slideio::viewer::core::ITileCache& tileCache,
                                      QImage& thumbnailOut)
{
    namespace core = slideio::viewer::core;
    int coarsestLevel = pyramid.numLevels() - 1;
    if (coarsestLevel < 0) return;
    const auto& coarseLvl = pyramid.levelInfo(coarsestLevel);

    // Target thumbnail dimensions are derived from the FULL SLIDE dims (not the
    // coarsest pyramid level), so even when the coarsest level is small (e.g.,
    // ~200x160 in a 9-level pyramid) we still ask SlideIO for a sharp 800x800-
    // ish thumbnail. SlideIO picks the appropriate pyramid level internally.
    constexpr int kMaxThumbDim = 800;
    int thumbW = slideInfo.width;
    int thumbH = slideInfo.height;
    if (thumbW > kMaxThumbDim || thumbH > kMaxThumbDim) {
        double ratio = std::min(static_cast<double>(kMaxThumbDim) / thumbW,
                                static_cast<double>(kMaxThumbDim) / thumbH);
        thumbW = std::max(1, static_cast<int>(thumbW * ratio));
        thumbH = std::max(1, static_cast<int>(thumbH * ratio));
    }

    auto loan = pool.acquire();
    int numCh = slideInfo.numChannels;
    DataType dt = slideInfo.channelDataType;

    // Pass 1: read all coarsest-level tiles and compute global min/max
    std::vector<core::TileData> coarseTiles;
    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();

    for (int r = 0; r < coarseLvl.tilesY; ++r) {
        for (int c = 0; c < coarseLvl.tilesX; ++c) {
            core::TileKey tileKey(coarsestLevel, c, r);
            auto tileData = loan->readTile(tileKey);
            if (tileData.isEmpty() || tileData.isError()) {
                coarseTiles.push_back(std::move(tileData));
                continue;
            }
            size_t totalElements = static_cast<size_t>(tileData.width())
                                 * static_cast<size_t>(tileData.height())
                                 * static_cast<size_t>(tileData.numChannels());
            auto [tMin, tMax] = computeMinMax(tileData.buffer().data(), totalElements, dt);
            globalMin = std::min(globalMin, tMin);
            globalMax = std::max(globalMax, tMax);
            coarseTiles.push_back(std::move(tileData));
        }
    }

    if (globalMin < globalMax) {
        slideInfo.displayRange.displayMin = globalMin;
        slideInfo.displayRange.displayMax = globalMax;
        slideInfo.displayRange.autoDetected = true;
    }

    // Per-channel min/max for multi-channel slides
    if (numCh > 1) {
        std::vector<double> channelMin(numCh, std::numeric_limits<double>::max());
        std::vector<double> channelMax(numCh, std::numeric_limits<double>::lowest());
        for (const auto& tileData : coarseTiles) {
            if (tileData.isEmpty() || tileData.isError()) continue;
            size_t pixelCount = static_cast<size_t>(tileData.width()) * tileData.height();
            for (int ch = 0; ch < numCh; ++ch) {
                computeMinMaxStrided(tileData.buffer().data(), pixelCount,
                                     numCh, ch, dt, channelMin[ch], channelMax[ch]);
            }
        }
        for (int ch = 0; ch < numCh; ++ch) {
            if (ch < static_cast<int>(slideInfo.channels.size())) {
                if (channelMin[ch] < channelMax[ch]) {
                    slideInfo.channels[ch].displayRange.displayMin = channelMin[ch];
                    slideInfo.channels[ch].displayRange.displayMax = channelMax[ch];
                    slideInfo.channels[ch].displayRange.autoDetected = true;
                }
            }
        }
    }

    // Thumbnail uses the same per-channel display ranges as the viewport's
    // multi-channel render path (where each channel is stretched independently),
    // so the minimap and viewport show matching colors. Falls back to global
    // range for single-channel rendering or when per-channel ranges weren't
    // detected.
    auto channelMinFor = [&](int ch) {
        if (numCh > 1 && ch < static_cast<int>(slideInfo.channels.size())
            && slideInfo.channels[ch].displayRange.autoDetected) {
            return slideInfo.channels[ch].displayRange.displayMin;
        }
        return globalMin;
    };
    auto channelMaxFor = [&](int ch) {
        if (numCh > 1 && ch < static_cast<int>(slideInfo.channels.size())
            && slideInfo.channels[ch].displayRange.autoDetected) {
            return slideInfo.channels[ch].displayRange.displayMax;
        }
        return globalMax;
    };
    double rMin = channelMinFor(0), rMax = channelMaxFor(0);
    double gMin = channelMinFor(1), gMax = channelMaxFor(1);
    double bMin = channelMinFor(2), bMax = channelMaxFor(2);

    // Pass 2: build the thumbnail QImage. Read the whole slide resampled to
    // the target thumbnail resolution via SlideIO (it picks the best pyramid
    // level internally). This gives a sharp thumbnail even when the coarsest
    // pyramid level itself is very small (e.g., 200x160) — much sharper than
    // upscaling from the coarsest tiles.
    QImage::Format imgFmt = (numCh >= 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage thumbImg(thumbW, thumbH, imgFmt);
    thumbImg.fill(Qt::white);

    // SlideIOAdapterPool uses a different concrete type as the loan; cast back
    // to access readBlock. (loan is a unique_ptr-like wrapper around the adapter.)
    auto thumbLoan = pool.acquire();
    auto blockData = thumbLoan->readBlock(0, 0, slideInfo.width, slideInfo.height, thumbW, thumbH);
    if (!blockData.isEmpty() && !blockData.isError()) {
        const uint8_t* src = blockData.buffer().data();
        int srcCh = blockData.numChannels();
        for (int y = 0; y < thumbH; ++y) {
            uint8_t* dst = thumbImg.scanLine(y);
            for (int x = 0; x < thumbW; ++x) {
                size_t srcElem = static_cast<size_t>((y * thumbW + x) * srcCh);
                if (numCh >= 3) {
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
        levelImg.fill(Qt::white);
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
                        if (numCh >= 3) {
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

    // Insert coarsest tiles into cache for instant base-layer preview
    {
        int idx = 0;
        for (int r = 0; r < coarseLvl.tilesY; ++r) {
            for (int c = 0; c < coarseLvl.tilesX; ++c) {
                auto& td = coarseTiles[static_cast<size_t>(idx++)];
                if (td.isEmpty() || td.isError()) continue;
                core::TileKey tileKey(coarsestLevel, c, r);
                tileCache.insert(tileKey, std::make_shared<core::TileData>(std::move(td)));
            }
        }
    }
}

using slideio::viewer::ui::SceneOpenResult;

// Open a scene synchronously (intended to run on a background thread). Captures
// any exception into result.errorMsg.
SceneOpenResult openSceneSync(const std::string& filePath, int sceneIndex)
{
    namespace core = slideio::viewer::core;
    namespace infra = slideio::viewer::infra;
    SceneOpenResult r;
    r.filePath = filePath;
    try {
        r.adapterPool = std::make_shared<infra::SlideIOAdapterPool>(filePath, sceneIndex, 4);
        {
            auto loan = r.adapterPool->acquire();
            r.slideInfo = loan->slideInfo();
            auto levels = loan->levels();
            r.pyramid = std::make_shared<core::TilePyramid>(
                r.slideInfo.width, r.slideInfo.height, levels);
        }
        r.tileCache = std::make_shared<infra::LruTileCache>();
        try {
            readCoarseLevelAndBuildThumbnail(*r.adapterPool, r.slideInfo, *r.pyramid,
                                             *r.tileCache, r.thumbnail);
        } catch (const std::exception& ex) {
            spdlog::warn("openSceneSync: thumbnail/coarse-cache pass failed: {}", ex.what());
        }

        // Apply fallback display range if auto-detection didn't yield one
        if (!r.slideInfo.displayRange.autoDetected) {
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
SceneOpenResult openAuxImageSync(const std::string& filePath, const std::string& auxImageName)
{
    namespace core = slideio::viewer::core;
    namespace infra = slideio::viewer::infra;
    SceneOpenResult r;
    r.filePath = filePath;
    r.isAuxImage = true;
    try {
        r.adapterPool = std::make_shared<infra::SlideIOAdapterPool>(filePath, auxImageName, 4);
        {
            auto loan = r.adapterPool->acquire();
            r.slideInfo = loan->slideInfo();
            auto levels = loan->levels();
            r.pyramid = std::make_shared<core::TilePyramid>(
                r.slideInfo.width, r.slideInfo.height, levels);
        }
        r.tileCache = std::make_shared<infra::LruTileCache>();
        try {
            readCoarseLevelAndBuildThumbnail(*r.adapterPool, r.slideInfo, *r.pyramid,
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
    std::shared_ptr<infra::SlideIOAdapterPool> adapterPool;
    std::shared_ptr<core::ITileCache> tileCache;
    std::shared_ptr<infra::TileLoadScheduler> scheduler;
    std::shared_ptr<core::TilePyramid> pyramid;
    std::unique_ptr<ViewportController> controller;
    core::SlideInfo slideInfo;
    bool slideOpen = false;
    std::string currentFilePath;
    int currentZSlice = 0;
    int currentTFrame = 0;

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
            size_t pixelCount = static_cast<size_t>(tile.width()) * tile.height() * channels;
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
        size_t pixelCount = static_cast<size_t>(tile.width()) * tile.height();
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
            for (size_t p = 0; p < pixelCount; ++p) {
                const uint8_t* srcElem = src + (p * numChannels + ch) * bytesPerElement;
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

            core::TileKey fallbackKey(fallbackLevel, fallbackCol, fallbackRow);

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

void ViewportWidget::openSlide(const std::string& filePath)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = QString::fromStdString(filePath);
    int slash = std::max(displayName.lastIndexOf('/'), displayName.lastIndexOf('\\'));
    if (slash >= 0) displayName = displayName.mid(slash + 1);
    emit loadingStarted(displayName);

    std::thread([this, opId, filePath]() {
        SceneOpenResult result = openSceneSync(filePath, 0);
        // Always enumerate scenes so the scene panel can populate, even if scene 0 worked.
        try {
            auto enumResult = infra::SlideIOAdapter::enumerateScenes(filePath);
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

    const auto& filePath = m_impl->currentFilePath;
    spdlog::info("generateSceneThumbnails: {} scenes, {} aux images",
                 m_impl->slideInfo.scenes.size(), m_impl->slideInfo.auxImages.size());

    for (const auto& sceneInfo : m_impl->slideInfo.scenes) {
        try {
            infra::SlideIOAdapter tempAdapter(filePath, sceneInfo.index);
            auto tempInfo = tempAdapter.slideInfo();

            if (tempInfo.width <= 0 || tempInfo.height <= 0) continue;
            int numCh = tempInfo.numChannels;
            if (numCh <= 0) continue;

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

            auto blockData = tempAdapter.readBlock(0, 0, tempInfo.width, tempInfo.height,
                                                    thumbW, thumbH);
            if (blockData.isEmpty() || blockData.isError()) continue;

            QImage::Format imgFmt = (numCh >= 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
            QImage thumb(thumbW, thumbH, imgFmt);
            thumb.fill(Qt::white);

            const uint8_t* src = blockData.buffer().data();
            int srcCh = blockData.numChannels();
            for (int y = 0; y < thumbH; ++y) {
                uint8_t* dst = thumb.scanLine(y);
                for (int x = 0; x < thumbW; ++x) {
                    int srcIdx = (y * thumbW + x) * srcCh;
                    if (numCh >= 3 && srcCh >= 3) {
                        int dstIdx = x * 3;
                        dst[dstIdx + 0] = src[srcIdx + 0];
                        dst[dstIdx + 1] = src[srcIdx + 1];
                        dst[dstIdx + 2] = src[srcIdx + 2];
                    } else {
                        dst[x] = src[srcIdx];
                    }
                }
            }

            emit sceneThumbnailReady(sceneInfo.index, false, sceneInfo.name, thumb.copy());
        } catch (const std::exception& ex) {
            spdlog::warn("generateSceneThumbnails: failed for scene {}: {}", sceneInfo.index, ex.what());
        }
    }

}

void ViewportWidget::openScene(const std::string& filePath, int sceneIndex)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = QString::fromStdString(filePath);
    int slash = std::max(displayName.lastIndexOf('/'), displayName.lastIndexOf('\\'));
    if (slash >= 0) displayName = displayName.mid(slash + 1);
    if (sceneIndex > 0) displayName += QStringLiteral(" (scene %1)").arg(sceneIndex);
    emit loadingStarted(displayName);

    std::thread([this, opId, filePath, sceneIndex]() {
        SceneOpenResult result = openSceneSync(filePath, sceneIndex);
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
    m_impl->adapterPool.reset();

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

void ViewportWidget::openAuxImage(const std::string& filePath, const std::string& auxImageName)
{
    closeSlide();
    m_impl->currentFilePath = filePath;
    const uint64_t opId = ++m_impl->openOpId;

    QString displayName = QString::fromStdString(filePath);
    int slash = std::max(displayName.lastIndexOf('/'), displayName.lastIndexOf('\\'));
    if (slash >= 0) displayName = displayName.mid(slash + 1);
    displayName += QStringLiteral(" (%1)").arg(QString::fromStdString(auxImageName));
    emit loadingStarted(displayName);

    std::thread([this, opId, filePath, auxImageName]() {
        SceneOpenResult result = openAuxImageSync(filePath, auxImageName);
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

    m_impl->adapterPool = std::move(result.adapterPool);
    m_impl->tileCache = std::move(result.tileCache);
    m_impl->pyramid = std::move(result.pyramid);
    m_impl->slideInfo = std::move(result.slideInfo);
    if (!result.scenes.empty()) {
        m_impl->slideInfo.scenes = std::move(result.scenes);
    }
    if (!result.auxImages.empty()) {
        m_impl->slideInfo.auxImages = std::move(result.auxImages);
    }

    m_impl->scheduler = std::make_shared<infra::TileLoadScheduler>(
        m_impl->adapterPool, m_impl->tileCache);
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
    m_impl->controller->setScheduler(m_impl->scheduler);
    m_impl->controller->requestVisibleTiles();

    m_impl->slideOpen = true;

    spdlog::info("ViewportWidget::installSceneOpenResult: slide {}x{}, {} levels, {} channels",
                 m_impl->slideInfo.width, m_impl->slideInfo.height,
                 m_impl->slideInfo.numZoomLevels, m_impl->slideInfo.numChannels);

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
    if (channels.size() == m_impl->slideInfo.channels.size()) {
        m_impl->slideInfo.channels = channels;
        update();
    }
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

    // Set clear color: black for multi-channel (additive blending), gray for single-channel
    if (m_impl->slideOpen && m_impl->slideInfo.numChannels > 1) {
        m_impl->gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        m_impl->gl->glClearColor(0.251f, 0.251f, 0.251f, 1.0f);
    }
    m_impl->gl->glClear(GL_COLOR_BUFFER_BIT);

    if (!m_impl->controller || !m_impl->slideOpen) {
        return;
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

    if (m_impl->slideInfo.numChannels <= 1) {
        // --- Single-channel rendering path ---
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

        // Default tex coord uniforms (full texture, no sub-region)
        m_impl->tileShader->setUniformValue("uTexCoordOffset", 0.0f, 0.0f);
        m_impl->tileShader->setUniformValue("uTexCoordScale", 1.0f, 1.0f);
        m_impl->tileShader->setUniformValue("uAlpha", 1.0f);

        // Pass 1: Render cached coarser-level tiles as a blurry background.
        // Walk from coarsest to the level just above the current visible level.
        int currentLevel = coordSystem.bestLevel(viewport.scale());
        for (int lvl = m_impl->pyramid->numLevels() - 1; lvl > currentLevel; --lvl) {
            // Find all tiles at this level that overlap the visible area
            auto visSlideRect = viewport.visibleSlideRect();
            auto coarseTiles = m_impl->pyramid->visibleTiles(
                lvl,
                std::max(0, static_cast<int>(visSlideRect.x)),
                std::max(0, static_cast<int>(visSlideRect.y)),
                static_cast<int>(std::ceil(visSlideRect.width)),
                static_cast<int>(std::ceil(visSlideRect.height)));

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

                auto [chSamplerMin, chSamplerMax] = toSamplerRange(
                    chInfo.displayRange.displayMin, chInfo.displayRange.displayMax, chInfo.dataType);
                m_impl->channelShader->setUniformValue("uDisplayMin", chSamplerMin);
                m_impl->channelShader->setUniformValue("uDisplayMax", chSamplerMax);
                float intensity = std::clamp(chInfo.intensity, 0.0f, 1.0f);
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
            auto visSlideRect = viewport.visibleSlideRect();
            auto coarseTiles = m_impl->pyramid->visibleTiles(
                lvl,
                std::max(0, static_cast<int>(visSlideRect.x)),
                std::max(0, static_cast<int>(visSlideRect.y)),
                static_cast<int>(std::ceil(visSlideRect.width)),
                static_cast<int>(std::ceil(visSlideRect.height)));

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
                if (!tileData || tileData->isEmpty() || tileData->isError()) {
                    ++tilesSkipped;
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
