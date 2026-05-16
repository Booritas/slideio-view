#include "slideio/viewer/infra/SlideIOAdapter.h"

#include <slideio/slideio/slideio.hpp>
#include <slideio/slideio/slide.hpp>
#include <slideio/slideio/scene.hpp>
#include <slideio/core/levelinfo.hpp>
#include <slideio/core/metadata.hpp>
#include <slideio/base/slideio_enums.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace
{

// Parse a channel "Color" attribute string into normalized [0,1] R/G/B floats.
// Accepts "RRGGBB" or "AARRGGBB" (alpha is leading and is ignored), with or
// without leading '#', case-insensitive. Returns false (and leaves r/g/b
// unchanged) for any other form.
bool parseChannelHexColor(const std::string& hex, float& r, float& g, float& b)
{
    const char* p = hex.c_str();
    size_t n = hex.size();
    if (n > 0 && p[0] == '#') { ++p; --n; }
    if (n != 6 && n != 8) return false;

    auto hexDigit = [](char c, int& out) -> bool {
        if (c >= '0' && c <= '9') { out = c - '0';        return true; }
        if (c >= 'A' && c <= 'F') { out = c - 'A' + 10;   return true; }
        if (c >= 'a' && c <= 'f') { out = c - 'a' + 10;   return true; }
        return false;
    };
    auto byteAt = [&](size_t i, int& out) -> bool {
        int hi, lo;
        if (!hexDigit(p[i], hi) || !hexDigit(p[i + 1], lo)) return false;
        out = (hi << 4) | lo;
        return true;
    };

    const size_t rOff = (n == 8) ? 2 : 0;  // skip leading AA in AARRGGBB
    int ri, gi, bi;
    if (!byteAt(rOff, ri) || !byteAt(rOff + 2, gi) || !byteAt(rOff + 4, bi)) return false;

    r = static_cast<float>(ri) / 255.0f;
    g = static_cast<float>(gi) / 255.0f;
    b = static_cast<float>(bi) / 255.0f;
    return true;
}

// Override default channel colors with the "Color" attribute from the scene's
// channel metadata, when present and parseable. SlideIO exposes this via
// Scene::getChannelAttributes() — an Array of length numChannels(), each entry
// an Object keyed by attribute name. Drivers (OME-TIFF, CZI) populate "Color"
// verbatim from the source file; format is typically "#RRGGBB".
void applyMetadataChannelColors(const ::slideio::Scene& scene,
                                std::vector<slideio::viewer::core::ChannelInfo>& channels)
{
    try {
        const ::slideio::Metadata& attrs = scene.getChannelAttributes();
        if (attrs.type() != ::slideio::Metadata::Type::Array) return;
        const size_t n = std::min(attrs.size(), channels.size());
        for (size_t ch = 0; ch < n; ++ch) {
            ::slideio::Metadata chanAttrs = attrs[ch];
            if (chanAttrs.type() != ::slideio::Metadata::Type::Object) continue;
            // Step 1: apply an explicit, non-white "Color" attribute if the
            // driver exposes one. White is initially treated as a "no
            // preference" sentinel (Zen/CZI convention for spectral channels)
            // and resolution is deferred to Step 2/3.
            bool explicitColorApplied = false;
            bool parsedAsWhite = false;
            if (chanAttrs.contains("Color")) {
                const std::string colorStr = chanAttrs["Color"].asString();
                float r, g, b;
                if (parseChannelHexColor(colorStr, r, g, b)) {
                    parsedAsWhite = r >= 0.999f && g >= 0.999f && b >= 0.999f;
                    if (!parsedAsWhite) {
                        channels[ch].colorR = r;
                        channels[ch].colorG = g;
                        channels[ch].colorB = b;
                        explicitColorApplied = true;
                    }
                } else if (!colorStr.empty()) {
                    spdlog::debug("SlideIOAdapter: channel {} 'Color' attribute '{}' is not a hex color, keeping default",
                                  ch, colorStr);
                }
            }

            // Step 2: if no explicit color (missing Color, or white sentinel),
            // derive color from EmissionWavelength when available. SlideIO's
            // CZI driver exposes EmissionWavelength under
            // Information/Image/Dimensions/Channels/Channel but does NOT
            // forward DisplaySetting/.../Color — so for spectral CZI files
            // this path is what produces the correct hue (matches Zen/QuPath).
            bool wavelengthApplied = false;
            if (!explicitColorApplied && chanAttrs.contains("EmissionWavelength")) {
                double nm = 0.0;
                try { nm = chanAttrs["EmissionWavelength"].asDouble(); }
                catch (const std::exception&) { nm = 0.0; }
                float wr, wg, wb;
                if (nm > 0.0 && slideio::viewer::core::wavelengthToSrgb(nm, wr, wg, wb)) {
                    channels[ch].colorR = wr;
                    channels[ch].colorG = wg;
                    channels[ch].colorB = wb;
                    wavelengthApplied = true;
                    spdlog::debug("SlideIOAdapter: channel {} color derived from emission wavelength "
                                  "{:.1f} nm -> ({:.2f},{:.2f},{:.2f})",
                                  ch, nm, wr, wg, wb);
                }
            }

            // Step 3: Color was white and we couldn't derive a wavelength
            // color — honor the literal white. This is the right call for
            // non-fluorescence channels (transmission/DIC overlay) where
            // white is a deliberate display choice, not a sentinel.
            if (!explicitColorApplied && !wavelengthApplied && parsedAsWhite) {
                channels[ch].colorR = 1.0f;
                channels[ch].colorG = 1.0f;
                channels[ch].colorB = 1.0f;
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getChannelAttributes failed: {}", ex.what());
    }
}

// Returns true if any channel's attributes indicate fluorescence imaging
// (ContrastMethod or IlluminationType). Used to disambiguate the otherwise
// ambiguous "single-channel grayscale" case in the isBrightfield heuristic:
// without this, a grayscale fluorescence channel (e.g. EGFP) would be flagged
// as brightfield, take the grayscale render path, and ignore its assigned
// channel color.
bool channelsIndicateFluorescence(const ::slideio::Scene& scene)
{
    auto containsFluorescenceHint = [](const std::string& s) {
        std::string lower;
        lower.reserve(s.size());
        for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return lower.find("fluorescence") != std::string::npos
            || lower.find("epifluor") != std::string::npos;
    };
    try {
        const ::slideio::Metadata& attrs = scene.getChannelAttributes();
        if (attrs.type() != ::slideio::Metadata::Type::Array) return false;
        const size_t n = attrs.size();
        for (size_t ch = 0; ch < n; ++ch) {
            ::slideio::Metadata chanAttrs = attrs[ch];
            if (chanAttrs.type() != ::slideio::Metadata::Type::Object) continue;
            if (chanAttrs.contains("ContrastMethod")
                && containsFluorescenceHint(chanAttrs["ContrastMethod"].asString())) {
                return true;
            }
            if (chanAttrs.contains("IlluminationType")
                && containsFluorescenceHint(chanAttrs["IlluminationType"].asString())) {
                return true;
            }
        }
    } catch (const std::exception&) {
        // Treat lookup failures as "no fluorescence hint" — fall back to the
        // channel-count heuristic.
    }
    return false;
}

slideio::viewer::core::DataType convertSlideIODataType(::slideio::DataType srcType)
{
    using DT = slideio::viewer::core::DataType;
    switch (srcType) {
        case ::slideio::DataType::DT_Byte:    return DT::Byte;
        case ::slideio::DataType::DT_Int8:     return DT::Int8;
        case ::slideio::DataType::DT_UInt16:   return DT::UInt16;
        case ::slideio::DataType::DT_Int16:    return DT::Int16;
        case ::slideio::DataType::DT_UInt32:   return DT::UInt32;
        case ::slideio::DataType::DT_Int32:    return DT::Int32;
        case ::slideio::DataType::DT_Float16:  return DT::Float16;
        case ::slideio::DataType::DT_Float32:  return DT::Float32;
        case ::slideio::DataType::DT_Float64:  return DT::Float64;
        case ::slideio::DataType::DT_Int64:    return DT::Int64;
        case ::slideio::DataType::DT_UInt64:   return DT::UInt64;
        case ::slideio::DataType::DT_Unknown:  return DT::Unknown;
        case ::slideio::DataType::DT_None:     return DT::None;
        default:                               return DT::Unknown;
    }
}

constexpr int kDefaultTileSize = 256;

// Local mapping of slideio::Compression to a human-readable name. The
// equivalent slideio::compressionToString lives in the slideio-base shared
// library, which the install does not ship an import lib for, so we mirror
// the mapping here. Keep in sync with slideio_enums.hpp.
std::string compressionName(::slideio::Compression c)
{
    using C = ::slideio::Compression;
    switch (c) {
        case C::Unknown:        return "Unknown";
        case C::Uncompressed:   return "Uncompressed";
        case C::Jpeg:           return "JPEG";
        case C::JpegXR:         return "JPEG XR";
        case C::Png:            return "PNG";
        case C::Jpeg2000:       return "JPEG 2000";
        case C::LZW:            return "LZW";
        case C::HuffmanRL:      return "Huffman RLE";
        case C::CCITT_T4:       return "CCITT T.4";
        case C::CCITT_T6:       return "CCITT T.6";
        case C::JpegOld:        return "JPEG (old)";
        case C::Zlib:           return "zlib";
        case C::JBIG85:         return "JBIG-85";
        case C::JBIG43:         return "JBIG-43";
        case C::NextRLE:        return "NeXT RLE";
        case C::PackBits:       return "PackBits";
        case C::ThunderScanRLE: return "ThunderScan RLE";
        case C::RasterPadding:  return "Raster padding";
        case C::RLE_LW:         return "RLE LW";
        case C::RLE_HC:         return "RLE HC";
        case C::RLE_BL:         return "RLE BL";
        case C::PKZIP:          return "PKZIP";
        case C::KodakDCS:       return "Kodak DCS";
        case C::JBIG:           return "JBIG";
        case C::NikonNEF:       return "Nikon NEF";
        case C::JBIG2:          return "JBIG2";
        case C::GIF:            return "GIF";
        case C::BIGGIF:         return "BigGIF";
        case C::RLE:            return "RLE";
        case C::BMP:            return "BMP";
        case C::JpegLossless:   return "JPEG (lossless)";
        case C::VP8:            return "VP8";
    }
    return "Unknown";
}

slideio::viewer::core::MetadataNode convertMetadata(
    const ::slideio::Metadata& meta, const std::string& name = {})
{
    using NodeType = slideio::viewer::core::MetadataNode::Type;
    slideio::viewer::core::MetadataNode node;
    node.name = name;

    try {
        switch (meta.type()) {
        case ::slideio::Metadata::Type::Null:
            node.type = NodeType::Null;
            break;
        case ::slideio::Metadata::Type::Bool:
            node.type = NodeType::Bool;
            node.value = meta.asBool() ? "true" : "false";
            break;
        case ::slideio::Metadata::Type::Int:
            node.type = NodeType::Int;
            node.value = std::to_string(meta.asInt());
            break;
        case ::slideio::Metadata::Type::Double: {
            // %.15g — general format, up to 15 significant digits, no
            // trailing zeros for integer-valued doubles. Matches the
            // precision policy in the design doc.
            node.type = NodeType::Double;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.15g", meta.asDouble());
            node.value = buf;
            break;
        }
        case ::slideio::Metadata::Type::String:
            node.type = NodeType::String;
            node.value = meta.asString();
            break;
        case ::slideio::Metadata::Type::Array: {
            node.type = NodeType::Array;
            const size_t n = meta.size();
            node.children.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                node.children.push_back(
                    convertMetadata(meta[i], "[" + std::to_string(i) + "]"));
            }
            break;
        }
        case ::slideio::Metadata::Type::Object: {
            node.type = NodeType::Object;
            const auto keys = meta.keys();
            node.children.reserve(keys.size());
            for (const auto& key : keys) {
                node.children.push_back(convertMetadata(meta[key], key));
            }
            break;
        }
        }
    } catch (const std::exception& ex) {
        // Per-subtree containment: a bad branch becomes a Null leaf so the
        // rest of the tree still renders. The warning is logged once per
        // bad subtree.
        spdlog::warn("SlideIOAdapter::convertMetadata: failed for '{}': {}",
                     name, ex.what());
        node.type = NodeType::Null;
        node.value.clear();
        node.children.clear();
    }

    return node;
}

// Build the per-channel attribute subtree shown under "Channels" in the
// Metadata pane. Wraps Scene::getChannelAttributes() — an Array of length
// numChannels() where each entry is an Object keyed by attribute name — into
// our layer-neutral MetadataNode form, labelling each child with the channel
// name when known. Returns a Null node (rendered as "(no metadata)" by the
// panel) when no channel has any attribute.
slideio::viewer::core::MetadataNode buildChannelMetadataNode(
    const ::slideio::Scene& scene,
    const std::vector<slideio::viewer::core::ChannelInfo>& channels)
{
    using NodeType = slideio::viewer::core::MetadataNode::Type;
    slideio::viewer::core::MetadataNode root;
    try {
        const ::slideio::Metadata& attrs = scene.getChannelAttributes();
        if (attrs.type() != ::slideio::Metadata::Type::Array || attrs.size() == 0) {
            return root;  // Null
        }

        bool anyAttribute = false;
        for (size_t i = 0; i < attrs.size(); ++i) {
            if (attrs[i].type() == ::slideio::Metadata::Type::Object && attrs[i].size() > 0) {
                anyAttribute = true;
                break;
            }
        }
        if (!anyAttribute) {
            return root;  // Null
        }

        root.type = NodeType::Array;
        const size_t n = attrs.size();
        root.children.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            std::string label;
            if (i < channels.size() && !channels[i].name.empty()) {
                label = "Channel " + std::to_string(i) + ": " + channels[i].name;
            } else {
                label = "Channel " + std::to_string(i);
            }
            root.children.push_back(convertMetadata(attrs[i], label));
        }
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter::buildChannelMetadataNode: {}", ex.what());
        root = {};
    }
    return root;
}

// Box-filter downsample of a packed pixel buffer (rows × cols × channels) by an
// integer factor. srcW/srcH must be exact multiples of dstW/dstH. Used as the
// final step of the finer-level fallback path in readTile (see below): we ask
// SlideIO for an N× larger output that forces it to read from a finer pyramid
// level, then average down to the requested tile size. The channel loop is
// hoisted inside the dy/dx loops so the per-row pointer is computed once per
// row instead of once per channel; for byte data we use an integer accumulator,
// which is the common brightfield path.
template<typename T, typename Accum>
void downsampleBoxAverageT(const uint8_t* srcBytes, int srcW, int srcH, int channels,
                           uint8_t* dstBytes, int dstW, int dstH)
{
    const T* src = reinterpret_cast<const T*>(srcBytes);
    T* dst = reinterpret_cast<T*>(dstBytes);
    const int factorX = srcW / dstW;
    const int factorY = srcH / dstH;
    if (factorX <= 0 || factorY <= 0) return;
    const Accum factor = static_cast<Accum>(factorX) * static_cast<Accum>(factorY);
    const Accum halfFactor = factor / 2;
    constexpr int kMaxChannels = 16;
    Accum sum[kMaxChannels];
    const size_t srcW_z    = static_cast<size_t>(srcW);
    const size_t dstW_z    = static_cast<size_t>(dstW);
    const size_t channels_z = static_cast<size_t>(channels);
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            for (int c = 0; c < channels; ++c) sum[c] = Accum{0};
            const int sy0 = y * factorY;
            const int sx0 = x * factorX;
            for (int dy = 0; dy < factorY; ++dy) {
                const T* row = src + (static_cast<size_t>(sy0 + dy) * srcW_z
                                      + static_cast<size_t>(sx0)) * channels_z;
                for (int dx = 0; dx < factorX; ++dx) {
                    const T* px = row + static_cast<size_t>(dx) * channels_z;
                    for (int c = 0; c < channels; ++c) {
                        sum[c] += static_cast<Accum>(px[c]);
                    }
                }
            }
            T* dstPx = dst + (static_cast<size_t>(y) * dstW_z
                              + static_cast<size_t>(x)) * channels_z;
            for (int c = 0; c < channels; ++c) {
                if constexpr (std::is_integral_v<Accum>) {
                    dstPx[c] = static_cast<T>((sum[c] + halfFactor) / factor);
                } else {
                    dstPx[c] = static_cast<T>(sum[c] / factor);
                }
            }
        }
    }
}

bool downsampleByDataType(const uint8_t* src, int srcW, int srcH,
                          slideio::viewer::core::DataType dataType, int channels,
                          uint8_t* dst, int dstW, int dstH)
{
    using DT = slideio::viewer::core::DataType;
    if (channels > 16) return false;  // sum[] is sized for up to 16 channels
    switch (dataType) {
    case DT::Byte:    downsampleBoxAverageT<uint8_t,  uint32_t>(src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::Int8:    downsampleBoxAverageT<int8_t,   int32_t> (src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::UInt16:  downsampleBoxAverageT<uint16_t, uint64_t>(src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::Int16:   downsampleBoxAverageT<int16_t,  int64_t> (src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::UInt32:  downsampleBoxAverageT<uint32_t, uint64_t>(src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::Int32:   downsampleBoxAverageT<int32_t,  int64_t> (src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::Float32: downsampleBoxAverageT<float,    double>  (src, srcW, srcH, channels, dst, dstW, dstH); return true;
    case DT::Float64: downsampleBoxAverageT<double,   double>  (src, srcW, srcH, channels, dst, dstW, dstH); return true;
    default:
        return false;
    }
}

} // anonymous namespace

namespace slideio::viewer::infra
{

SlideIOAdapter::SlideIOAdapter(const std::string& filePath, int sceneIndex,
                               const std::string& driverId)
    : m_filePath(filePath)
{
    spdlog::info("SlideIOAdapter: opening slide '{}', scene {}, driver '{}'",
                 filePath, sceneIndex, driverId);

    m_slide = ::slideio::openSlide(filePath, driverId);
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    int numScenes = m_slide->getNumScenes();
    if (numScenes <= 0) {
        throw std::runtime_error("SlideIOAdapter: slide '" + filePath + "' has no scenes");
    }

    if (sceneIndex < 0 || sceneIndex >= numScenes) {
        throw std::runtime_error("SlideIOAdapter: scene index " + std::to_string(sceneIndex)
            + " out of range [0, " + std::to_string(numScenes) + ") for slide '" + filePath + "'");
    }

    m_scene = m_slide->getScene(sceneIndex);
    if (!m_scene) {
        throw std::runtime_error("SlideIOAdapter: failed to get scene " + std::to_string(sceneIndex)
            + " from slide '" + filePath + "'");
    }

    // Build cached slide info
    auto rect = m_scene->getRect();
    m_slideInfo.filePath = filePath;
    m_slideInfo.width = std::get<2>(rect);
    m_slideInfo.height = std::get<3>(rect);
    m_slideInfo.numChannels = m_scene->getNumChannels();
    m_slideInfo.channelDataType = (m_slideInfo.numChannels > 0)
        ? convertSlideIODataType(m_scene->getChannelDataType(0))
        : core::DataType::None;
    m_slideInfo.magnification = m_scene->getMagnification();

    auto resolution = m_scene->getResolution();
    m_slideInfo.resolutionX = std::get<0>(resolution);
    m_slideInfo.resolutionY = std::get<1>(resolution);

    m_slideInfo.driverName = m_slide->getDriverId();
    m_slideInfo.driverId = m_slide->getDriverId();
    try {
        m_slideInfo.compression = compressionName(m_scene->getCompression());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getCompression failed: {}", ex.what());
        m_slideInfo.compression.clear();
    }
    m_slideInfo.numZSlices = m_scene->getNumZSlices();
    m_slideInfo.numTFrames = m_scene->getNumTFrames();

    // Read per-channel info (names, data types, default colors)
    m_slideInfo.channels.resize(static_cast<size_t>(m_slideInfo.numChannels));
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        try {
            info.name = m_scene->getChannelName(ch);
        } catch (...) {
            info.name = "Channel " + std::to_string(ch);
        }
        info.dataType = convertSlideIODataType(m_scene->getChannelDataType(ch));
        info.visible = true;
    }

    // Determine if this is a brightfield slide. The channel-count heuristic
    // cannot disambiguate 1-channel grayscale brightfield from 1-channel
    // grayscale fluorescence, so an explicit fluorescence hint in channel
    // metadata overrides it.
    const bool fluorescenceHint = channelsIndicateFluorescence(*m_scene);
    m_slideInfo.isBrightfield = !fluorescenceHint && (
        (m_slideInfo.numChannels == 1) ||
        (m_slideInfo.numChannels == 3 && m_slideInfo.channelDataType == core::DataType::Byte));

    // Assign default channel colors based on image type
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        if (m_slideInfo.isBrightfield) {
            core::assignDefaultBrightfieldColor(info, ch, m_slideInfo.numChannels);
        } else {
            core::assignDefaultFluorescenceColor(info, ch);
        }
    }

    // Override with explicit "Color" attribute from the source file, if present.
    applyMetadataChannelColors(*m_scene, m_slideInfo.channels);

    spdlog::info("SlideIOAdapter: isBrightfield={}, {} channels", m_slideInfo.isBrightfield, m_slideInfo.numChannels);
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        const auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        spdlog::info("  channel {}: name='{}' color=({:.2f},{:.2f},{:.2f})",
                     ch, info.name, info.colorR, info.colorG, info.colorB);
    }

    // Build level info
    int numZoomLevels = m_scene->getNumZoomLevels();
    m_slideInfo.numZoomLevels = numZoomLevels;

    if (numZoomLevels > 0) {
        for (int i = 0; i < numZoomLevels; ++i) {
            const ::slideio::LevelInfo* srcLevel = m_scene->getLevelInfo(i);
            if (!srcLevel) {
                spdlog::warn("SlideIOAdapter: getLevelInfo({}) returned null, skipping", i);
                continue;
            }

            core::LevelInfo lvl;
            lvl.level = srcLevel->getLevel();

            auto levelSize = srcLevel->getSize();
            lvl.width = levelSize.width;
            lvl.height = levelSize.height;
            // Normalize scale to the project's convention:
            //   level.scale = level_pixels / slide_pixels
            // so level 0 is ~1.0 and coarser levels are <1.0.
            // Some backends report the inverse (downsample factor > 1), so we
            // derive from level dimensions first and only fall back to metadata.
            double scaleFromMetadata = srcLevel->getScale();
            double scaleFromSize = 0.0;
            if (m_slideInfo.width > 0 && m_slideInfo.height > 0 && lvl.width > 0 && lvl.height > 0) {
                double sx = static_cast<double>(lvl.width) / static_cast<double>(m_slideInfo.width);
                double sy = static_cast<double>(lvl.height) / static_cast<double>(m_slideInfo.height);
                scaleFromSize = std::min(sx, sy);
            }

            if (scaleFromSize > 0.0) {
                lvl.scale = scaleFromSize;
            } else if (scaleFromMetadata > 1.0) {
                lvl.scale = 1.0 / scaleFromMetadata;
            } else {
                lvl.scale = scaleFromMetadata;
            }

            if (lvl.scale <= 0.0) {
                lvl.scale = 1.0;
            }
            lvl.magnification = srcLevel->getMagnification();

            auto tileSize = srcLevel->getTileSize();
            lvl.tileWidth = tileSize.width;
            lvl.tileHeight = tileSize.height;

            // If tile size is zero or negative, use default
            if (lvl.tileWidth <= 0 || lvl.tileHeight <= 0) {
                lvl.tileWidth = std::min(kDefaultTileSize, lvl.width);
                lvl.tileHeight = std::min(kDefaultTileSize, lvl.height);
            }

            // Compute tile counts
            lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
                ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
                : 1;
            lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
                ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
                : 1;

            m_levels.push_back(lvl);
        }
    }

    // Fallback: create a synthetic single level from full dimensions
    if (m_levels.empty()) {
        spdlog::info("SlideIOAdapter: scene has 0 zoom levels, creating synthetic single level");

        core::LevelInfo lvl;
        lvl.level = 0;
        lvl.width = m_slideInfo.width;
        lvl.height = m_slideInfo.height;
        lvl.scale = 1.0;
        lvl.magnification = m_slideInfo.magnification;
        lvl.tileWidth = std::min(kDefaultTileSize, m_slideInfo.width);
        lvl.tileHeight = std::min(kDefaultTileSize, m_slideInfo.height);
        lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
            ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
            : 1;
        lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
            ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
            : 1;

        m_levels.push_back(lvl);
        m_slideInfo.numZoomLevels = 1;
    }

    m_slideInfo.levels = m_levels;

    try {
        m_slideInfo.slideMetadata = convertMetadata(m_slide->getMetadata());
        m_slideInfo.sceneMetadata = convertMetadata(m_scene->getMetadata());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getMetadata failed: {}", ex.what());
    }
    m_slideInfo.channelMetadata = buildChannelMetadataNode(*m_scene, m_slideInfo.channels);

    spdlog::info("SlideIOAdapter: opened slide {}x{}, {} channels, {} levels, Z={}, T={}",
                 m_slideInfo.width, m_slideInfo.height, m_slideInfo.numChannels,
                 static_cast<int>(m_levels.size()),
                 m_slideInfo.numZSlices, m_slideInfo.numTFrames);
}

SlideIOAdapter::SlideIOAdapter(const std::string& filePath, const std::string& auxImageName,
                               const std::string& driverId)
    : m_filePath(filePath)
{
    spdlog::info("SlideIOAdapter: opening slide '{}', aux image '{}', driver '{}'",
                 filePath, auxImageName, driverId);

    m_slide = ::slideio::openSlide(filePath, driverId);
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    m_scene = m_slide->getAuxImage(auxImageName);
    if (!m_scene) {
        throw std::runtime_error("SlideIOAdapter: failed to get aux image '" + auxImageName
            + "' from slide '" + filePath + "'");
    }

    // Build cached slide info
    auto rect = m_scene->getRect();
    m_slideInfo.filePath = filePath;
    m_slideInfo.width = std::get<2>(rect);
    m_slideInfo.height = std::get<3>(rect);
    m_slideInfo.numChannels = m_scene->getNumChannels();
    m_slideInfo.channelDataType = (m_slideInfo.numChannels > 0)
        ? convertSlideIODataType(m_scene->getChannelDataType(0))
        : core::DataType::None;
    m_slideInfo.magnification = m_scene->getMagnification();

    auto resolution = m_scene->getResolution();
    m_slideInfo.resolutionX = std::get<0>(resolution);
    m_slideInfo.resolutionY = std::get<1>(resolution);

    m_slideInfo.driverName = m_slide->getDriverId();
    m_slideInfo.driverId = m_slide->getDriverId();
    try {
        m_slideInfo.compression = compressionName(m_scene->getCompression());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getCompression failed: {}", ex.what());
        m_slideInfo.compression.clear();
    }
    m_slideInfo.numZSlices = m_scene->getNumZSlices();
    m_slideInfo.numTFrames = m_scene->getNumTFrames();

    // Read per-channel info (names, data types, default colors)
    m_slideInfo.channels.resize(static_cast<size_t>(m_slideInfo.numChannels));
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        try {
            info.name = m_scene->getChannelName(ch);
        } catch (...) {
            info.name = "Channel " + std::to_string(ch);
        }
        info.dataType = convertSlideIODataType(m_scene->getChannelDataType(ch));
        info.visible = true;
    }

    // Determine if this is a brightfield slide. See sibling call site for the
    // disambiguation rationale.
    const bool fluorescenceHint = channelsIndicateFluorescence(*m_scene);
    m_slideInfo.isBrightfield = !fluorescenceHint && (
        (m_slideInfo.numChannels == 1) ||
        (m_slideInfo.numChannels == 3 && m_slideInfo.channelDataType == core::DataType::Byte));

    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        if (m_slideInfo.isBrightfield) {
            core::assignDefaultBrightfieldColor(info, ch, m_slideInfo.numChannels);
        } else {
            core::assignDefaultFluorescenceColor(info, ch);
        }
    }

    // Override with explicit "Color" attribute from the source file, if present.
    applyMetadataChannelColors(*m_scene, m_slideInfo.channels);

    spdlog::info("SlideIOAdapter: isBrightfield={}, {} channels", m_slideInfo.isBrightfield, m_slideInfo.numChannels);

    // Build level info
    int numZoomLevels = m_scene->getNumZoomLevels();
    m_slideInfo.numZoomLevels = numZoomLevels;

    if (numZoomLevels > 0) {
        for (int i = 0; i < numZoomLevels; ++i) {
            const ::slideio::LevelInfo* srcLevel = m_scene->getLevelInfo(i);
            if (!srcLevel) {
                spdlog::warn("SlideIOAdapter: getLevelInfo({}) returned null, skipping", i);
                continue;
            }

            core::LevelInfo lvl;
            lvl.level = srcLevel->getLevel();

            auto levelSize = srcLevel->getSize();
            lvl.width = levelSize.width;
            lvl.height = levelSize.height;

            double scaleFromMetadata = srcLevel->getScale();
            double scaleFromSize = 0.0;
            if (m_slideInfo.width > 0 && m_slideInfo.height > 0 && lvl.width > 0 && lvl.height > 0) {
                double sx = static_cast<double>(lvl.width) / static_cast<double>(m_slideInfo.width);
                double sy = static_cast<double>(lvl.height) / static_cast<double>(m_slideInfo.height);
                scaleFromSize = std::min(sx, sy);
            }

            if (scaleFromSize > 0.0) {
                lvl.scale = scaleFromSize;
            } else if (scaleFromMetadata > 1.0) {
                lvl.scale = 1.0 / scaleFromMetadata;
            } else {
                lvl.scale = scaleFromMetadata;
            }

            if (lvl.scale <= 0.0) {
                lvl.scale = 1.0;
            }
            lvl.magnification = srcLevel->getMagnification();

            auto tileSize = srcLevel->getTileSize();
            lvl.tileWidth = tileSize.width;
            lvl.tileHeight = tileSize.height;

            if (lvl.tileWidth <= 0 || lvl.tileHeight <= 0) {
                lvl.tileWidth = std::min(kDefaultTileSize, lvl.width);
                lvl.tileHeight = std::min(kDefaultTileSize, lvl.height);
            }

            lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
                ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
                : 1;
            lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
                ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
                : 1;

            m_levels.push_back(lvl);
        }
    }

    // Fallback: create a synthetic single level from full dimensions
    if (m_levels.empty()) {
        spdlog::info("SlideIOAdapter: aux image has 0 zoom levels, creating synthetic single level");

        core::LevelInfo lvl;
        lvl.level = 0;
        lvl.width = m_slideInfo.width;
        lvl.height = m_slideInfo.height;
        lvl.scale = 1.0;
        lvl.magnification = m_slideInfo.magnification;
        lvl.tileWidth = std::min(kDefaultTileSize, m_slideInfo.width);
        lvl.tileHeight = std::min(kDefaultTileSize, m_slideInfo.height);
        lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
            ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
            : 1;
        lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
            ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
            : 1;

        m_levels.push_back(lvl);
        m_slideInfo.numZoomLevels = 1;
    }

    m_slideInfo.levels = m_levels;

    try {
        m_slideInfo.slideMetadata = convertMetadata(m_slide->getMetadata());
        m_slideInfo.sceneMetadata = convertMetadata(m_scene->getMetadata());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getMetadata failed: {}", ex.what());
    }
    m_slideInfo.channelMetadata = buildChannelMetadataNode(*m_scene, m_slideInfo.channels);

    spdlog::info("SlideIOAdapter: opened aux image '{}' {}x{}, {} channels, {} levels",
                 auxImageName, m_slideInfo.width, m_slideInfo.height, m_slideInfo.numChannels,
                 static_cast<int>(m_levels.size()));
}

std::pair<std::vector<core::SceneInfo>, std::vector<core::SceneInfo>> SlideIOAdapter::enumerateScenes(
    const std::string& filePath, const std::string& driverId)
{
    std::vector<core::SceneInfo> scenes;
    std::vector<core::SceneInfo> auxImages;

    try {
        auto slide = ::slideio::openSlide(filePath, driverId);
        if (!slide) {
            spdlog::error("SlideIOAdapter::enumerateScenes: failed to open slide '{}'", filePath);
            return {scenes, auxImages};
        }

        // Enumerate main scenes
        int numScenes = slide->getNumScenes();
        for (int i = 0; i < numScenes; ++i) {
            try {
                auto scene = slide->getScene(i);
                if (!scene) {
                    continue;
                }

                core::SceneInfo info;
                info.index = i;
                info.name = scene->getName();
                auto rect = scene->getRect();
                info.width = std::get<2>(rect);
                info.height = std::get<3>(rect);
                info.numChannels = scene->getNumChannels();
                info.isAuxiliary = false;

                scenes.push_back(std::move(info));
            } catch (const std::exception& ex) {
                spdlog::warn("SlideIOAdapter::enumerateScenes: failed to read scene {}: {}", i, ex.what());
            }
        }

        // Enumerate auxiliary images
        auto auxNames = slide->getAuxImageNames();
        for (const auto& auxName : auxNames) {
            try {
                auto auxScene = slide->getAuxImage(auxName);
                if (!auxScene) {
                    continue;
                }

                core::SceneInfo info;
                info.index = -1;
                info.name = auxScene->getName();
                auto rect = auxScene->getRect();
                info.width = std::get<2>(rect);
                info.height = std::get<3>(rect);
                info.numChannels = auxScene->getNumChannels();
                info.isAuxiliary = true;
                info.auxiliaryName = auxName;

                auxImages.push_back(std::move(info));
            } catch (const std::exception& ex) {
                spdlog::warn("SlideIOAdapter::enumerateScenes: failed to read aux image '{}': {}", auxName, ex.what());
            }
        }
    } catch (const std::exception& ex) {
        spdlog::error("SlideIOAdapter::enumerateScenes: exception opening slide '{}': {}", filePath, ex.what());
    }

    spdlog::info("SlideIOAdapter::enumerateScenes: '{}' has {} scenes, {} aux images",
                 filePath, scenes.size(), auxImages.size());

    return {scenes, auxImages};
}

SlideIOAdapter::~SlideIOAdapter()
{
    spdlog::debug("SlideIOAdapter: closing slide '{}'", m_filePath);
}

core::SlideInfo SlideIOAdapter::slideInfo() const
{
    return m_slideInfo;
}

std::vector<core::LevelInfo> SlideIOAdapter::levels() const
{
    return m_levels;
}

bool SlideIOAdapter::isLevelUnreliable(int level) const
{
    std::lock_guard<std::mutex> lock(m_unreliableLevelsMutex);
    return m_unreliableLevels.count(level) > 0;
}

bool SlideIOAdapter::markLevelUnreliable(int level)
{
    bool firstTime = false;
    {
        std::lock_guard<std::mutex> lock(m_unreliableLevelsMutex);
        firstTime = m_unreliableLevels.insert(level).second;
    }
    if (firstTime) {
        spdlog::warn("SlideIOAdapter: level {} marked unreliable; will route reads through finer levels", level);
        if (m_onLevelMarkedUnreliable) {
            try {
                m_onLevelMarkedUnreliable(level);
            } catch (const std::exception& ex) {
                spdlog::warn("SlideIOAdapter: onLevelMarkedUnreliable callback threw: {}", ex.what());
            }
        }
    }
    return firstTime;
}

void SlideIOAdapter::setOnLevelMarkedUnreliable(std::function<void(int)> callback)
{
    m_onLevelMarkedUnreliable = std::move(callback);
}

core::TileData SlideIOAdapter::readTile(const core::TileKey& key)
{
    int level = key.level();
    int col = key.column();
    int row = key.row();

    if (level < 0 || level >= static_cast<int>(m_levels.size())) {
        spdlog::error("SlideIOAdapter::readTile: level {} out of range [0, {})", level, m_levels.size());
        return core::TileData::createError(0, 0);
    }

    const auto& lvl = m_levels[static_cast<size_t>(level)];

    if (col < 0 || col >= lvl.tilesX || row < 0 || row >= lvl.tilesY) {
        spdlog::error("SlideIOAdapter::readTile: tile ({},{}) out of range for level {} ({}x{} tiles)",
                      col, row, level, lvl.tilesX, lvl.tilesY);
        return core::TileData::createError(lvl.tileWidth, lvl.tileHeight);
    }

    // Compute the pixel rectangle in level coordinates
    int tileX = col * lvl.tileWidth;
    int tileY = row * lvl.tileHeight;
    int tileW = std::min(lvl.tileWidth, lvl.width - tileX);
    int tileH = std::min(lvl.tileHeight, lvl.height - tileY);

    if (tileW <= 0 || tileH <= 0) {
        spdlog::error("SlideIOAdapter::readTile: computed tile dimensions {}x{} invalid", tileW, tileH);
        return core::TileData::createError(lvl.tileWidth, lvl.tileHeight);
    }

    // Convert level-coordinate rect to slide-coordinate rect.
    // lvl.scale = level_pixels / slide_pixels, so slide_coord = level_coord / scale.
    double invScale = (lvl.scale > 0.0) ? (1.0 / lvl.scale) : 1.0;
    int slideX = static_cast<int>(std::round(tileX * invScale));
    int slideY = static_cast<int>(std::round(tileY * invScale));
    int slideRight = static_cast<int>(std::round((tileX + tileW) * invScale));
    int slideBottom = static_cast<int>(std::round((tileY + tileH) * invScale));
    int slideW = slideRight - slideX;
    int slideH = slideBottom - slideY;

    // Clamp to slide bounds
    slideW = std::min(slideW, m_slideInfo.width - slideX);
    slideH = std::min(slideH, m_slideInfo.height - slideY);

    if (slideW <= 0 || slideH <= 0) {
        spdlog::error("SlideIOAdapter::readTile: slide rect {}x{} invalid after clamping", slideW, slideH);
        return core::TileData::createError(tileW, tileH);
    }

    std::tuple<int, int, int, int> blockRect(slideX, slideY, slideW, slideH);

    int numChannels = m_slideInfo.numChannels;
    std::vector<int> channels(static_cast<size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch) {
        channels[static_cast<size_t>(ch)] = ch;
    }
    size_t pixelBytes = core::dataTypeSize(m_slideInfo.channelDataType);

    // Some SVS files have a corrupt tile-offset table at one pyramid level: a
    // single tile read throws (TiffTools error), and other "successful" reads
    // at the same level return data shifted from a wrong file offset. Aperio
    // ImageScope shows the artifact as-is; QuPath bypasses the broken level by
    // reading from a finer level and downsampling. We do the same: when a
    // level is known unreliable, ask SlideIO for the same slide region at an
    // N× larger output, which forces it to read from a finer level. Then we
    // box-filter down to the requested tile size.
    //
    // The initial multiplier targets the next reliable finer level by actual
    // scale ratio (not just *2), so on a 4×-step pyramid we go straight from
    // 1× to 4× instead of wasting an exception-throw at 2× that still resolves
    // to the broken level.
    int maxMultiplier = std::max(1, static_cast<int>(std::round(invScale)));
    int multiplier = 1;
    if (isLevelUnreliable(level)) {
        int targetLevel = level - 1;
        while (targetLevel >= 0 && isLevelUnreliable(targetLevel)) --targetLevel;
        if (targetLevel < 0) {
            return core::TileData::createError(tileW, tileH);
        }
        double targetScale = m_levels[static_cast<size_t>(targetLevel)].scale;
        double currentScale = lvl.scale;
        if (currentScale > 0.0) {
            multiplier = std::max(2, static_cast<int>(std::ceil(targetScale / currentScale)));
        } else {
            multiplier = 2;
        }
    }

    while (multiplier <= maxMultiplier) {
        int srcW = tileW * multiplier;
        int srcH = tileH * multiplier;
        srcW = std::min(srcW, slideW);
        srcH = std::min(srcH, slideH);
        // Re-derive to keep an integer factor for the downsample step.
        int factorX = std::max(1, srcW / tileW);
        int factorY = std::max(1, srcH / tileH);
        srcW = factorX * tileW;
        srcH = factorY * tileH;

        std::tuple<int, int> srcBlockSize(srcW, srcH);
        size_t srcBufSize = static_cast<size_t>(srcW) * static_cast<size_t>(srcH)
                          * static_cast<size_t>(numChannels) * pixelBytes;
        std::vector<uint8_t> srcBuffer(srcBufSize);

        try {
            int zIdx = key.zIndex();
            int tIdx = key.tFrame();
            if (m_slideInfo.numZSlices > 1 || m_slideInfo.numTFrames > 1) {
                std::tuple<int, int> zRange(zIdx, zIdx + 1);
                std::tuple<int, int> tRange(tIdx, tIdx + 1);
                m_scene->readResampled4DBlockChannels(blockRect, srcBlockSize, channels,
                                                      zRange, tRange, srcBuffer.data(), srcBufSize);
            } else {
                m_scene->readResampledBlockChannels(blockRect, srcBlockSize, channels,
                                                     srcBuffer.data(), srcBufSize);
            }
        } catch (const std::exception& ex) {
            spdlog::error("SlideIOAdapter::readTile: exception reading tile {} (multiplier={}): {}",
                          key.toString(), multiplier, ex.what());
            // Figure out which level SlideIO most likely used for this read, so
            // we can mark that level (not just the requested one) unreliable.
            double effectiveScale = static_cast<double>(multiplier) * lvl.scale;
            int suspectLevel = level;
            double bestDelta = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < m_levels.size(); ++i) {
                double delta = std::abs(m_levels[i].scale - effectiveScale);
                if (delta < bestDelta) {
                    bestDelta = delta;
                    suspectLevel = static_cast<int>(i);
                }
            }
            markLevelUnreliable(suspectLevel);
            // Jump straight to the next reliable finer level by scale ratio,
            // not just *2 (avoids re-throwing on irregular pyramid steps).
            int targetLevel = suspectLevel - 1;
            while (targetLevel >= 0 && isLevelUnreliable(targetLevel)) --targetLevel;
            if (targetLevel < 0) {
                return core::TileData::createError(tileW, tileH);
            }
            double targetScale = m_levels[static_cast<size_t>(targetLevel)].scale;
            if (lvl.scale > 0.0) {
                int nextMultiplier = std::max(multiplier + 1,
                    static_cast<int>(std::ceil(targetScale / lvl.scale)));
                multiplier = nextMultiplier;
            } else {
                multiplier *= 2;
            }
            continue;
        }

        if (multiplier == 1 && factorX == 1 && factorY == 1) {
            return core::TileData(std::move(srcBuffer), tileW, tileH, numChannels,
                                  m_slideInfo.channelDataType);
        }

        size_t dstBufSize = static_cast<size_t>(tileW) * static_cast<size_t>(tileH)
                          * static_cast<size_t>(numChannels) * pixelBytes;
        std::vector<uint8_t> dstBuffer(dstBufSize);
        if (!downsampleByDataType(srcBuffer.data(), srcW, srcH,
                                  m_slideInfo.channelDataType, numChannels,
                                  dstBuffer.data(), tileW, tileH)) {
            spdlog::error("SlideIOAdapter::readTile: unsupported data type for downsample fallback");
            return core::TileData::createError(tileW, tileH);
        }
        if (multiplier > 1) {
            spdlog::debug("SlideIOAdapter::readTile: tile {} satisfied via finer-level fallback (multiplier={})",
                          key.toString(), multiplier);
        }
        return core::TileData(std::move(dstBuffer), tileW, tileH, numChannels,
                              m_slideInfo.channelDataType);
    }

    return core::TileData::createError(tileW, tileH);
}

std::vector<std::string> SlideIOAdapter::availableDriverIds()
{
    try {
        return ::slideio::getDriverIDs();
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter::availableDriverIds: {}", ex.what());
        return {};
    }
}

core::TileData SlideIOAdapter::readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                                          int targetWidth, int targetHeight,
                                          int zIndex, int tFrame)
{
    if (slideWidth <= 0 || slideHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
        return core::TileData::createError(targetWidth, targetHeight);
    }
    try {
        std::tuple<int, int, int, int> blockRect(slideX, slideY, slideWidth, slideHeight);
        std::tuple<int, int> blockSize(targetWidth, targetHeight);

        int numChannels = m_slideInfo.numChannels;
        std::vector<int> channels(static_cast<size_t>(numChannels));
        for (int ch = 0; ch < numChannels; ++ch) {
            channels[static_cast<size_t>(ch)] = ch;
        }

        size_t pixelBytes = core::dataTypeSize(m_slideInfo.channelDataType);
        size_t bufSize = static_cast<size_t>(targetWidth) * static_cast<size_t>(targetHeight)
                         * static_cast<size_t>(numChannels) * pixelBytes;
        std::vector<uint8_t> buffer(bufSize);

        if (m_slideInfo.numZSlices > 1 || m_slideInfo.numTFrames > 1) {
            std::tuple<int, int> zRange(zIndex, zIndex + 1);
            std::tuple<int, int> tRange(tFrame, tFrame + 1);
            m_scene->readResampled4DBlockChannels(blockRect, blockSize, channels,
                                                   zRange, tRange, buffer.data(), bufSize);
        } else {
            m_scene->readResampledBlockChannels(blockRect, blockSize, channels,
                                                 buffer.data(), bufSize);
        }

        return core::TileData(std::move(buffer), targetWidth, targetHeight,
                               numChannels, m_slideInfo.channelDataType);
    }
    catch (const std::exception& ex) {
        spdlog::error("SlideIOAdapter::readBlock: exception: {}", ex.what());
        return core::TileData::createError(targetWidth, targetHeight);
    }
}

} // namespace slideio::viewer::infra
