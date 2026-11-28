#include "slideio/viewer/ui/OverviewRendering.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::ui
{

using DataType = core::DataType;

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

OverviewRenderer::OverviewRenderer(const core::SlideInfo& info, double globalMin, double globalMax)
    : m_numCh(info.numChannels)
    , m_dt(info.channelDataType)
    , m_isBrightfield(info.isBrightfield)
    , m_globalMin(globalMin)
    , m_globalMax(globalMax)
{
    auto channelMinFor = [&](int ch) {
        const size_t ch_z = static_cast<size_t>(ch);
        if (m_numCh > 1 && ch < static_cast<int>(info.channels.size())
            && info.channels[ch_z].displayRange.autoDetected) {
            return info.channels[ch_z].displayRange.displayMin;
        }
        return globalMin;
    };
    auto channelMaxFor = [&](int ch) {
        const size_t ch_z = static_cast<size_t>(ch);
        if (m_numCh > 1 && ch < static_cast<int>(info.channels.size())
            && info.channels[ch_z].displayRange.autoDetected) {
            return info.channels[ch_z].displayRange.displayMax;
        }
        return globalMax;
    };
    m_rMin = channelMinFor(0); m_rMax = channelMaxFor(0);
    m_gMin = channelMinFor(1); m_gMax = channelMaxFor(1);
    m_bMin = channelMinFor(2); m_bMax = channelMaxFor(2);

    m_useChannelMix = !(info.isBrightfield && m_numCh <= 1) && m_numCh >= 1;
    if (m_useChannelMix) {
        m_mix.resize(static_cast<size_t>(m_numCh));
        for (int ch = 0; ch < m_numCh; ++ch) {
            const size_t ch_z = static_cast<size_t>(ch);
            ChannelMix& m = m_mix[ch_z];
            m.minVal = channelMinFor(ch);
            m.maxVal = channelMaxFor(ch);
            if (ch < static_cast<int>(info.channels.size())) {
                const auto& chInfo = info.channels[ch_z];
                if (!chInfo.visible) continue;
                float intensity = std::clamp(chInfo.intensity, 0.0f, 4.0f);
                m.r = chInfo.colorR * intensity;
                m.g = chInfo.colorG * intensity;
                m.b = chInfo.colorB * intensity;
                m.active = true;
            }
        }
    }
}

QImage::Format OverviewRenderer::format() const
{
    return (m_useChannelMix || m_numCh >= 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
}

void OverviewRenderer::writePixel(uint8_t* row, int x, const uint8_t* src, size_t srcElem, int srcCh) const
{
    if (m_useChannelMix) {
        uint8_t* dst = row + x * 3;
        float accR = 0.0f, accG = 0.0f, accB = 0.0f;
        const int upTo = std::min(m_numCh, srcCh);
        for (int ch = 0; ch < upTo; ++ch) {
            const ChannelMix& m = m_mix[static_cast<size_t>(ch)];
            if (!m.active) continue;
            float v = static_cast<float>(mapPixelNormalized(
                src, srcElem + static_cast<size_t>(ch), m_dt, m.minVal, m.maxVal));
            accR += v * m.r;
            accG += v * m.g;
            accB += v * m.b;
        }
        dst[0] = static_cast<uint8_t>(std::clamp(accR * 255.0f, 0.0f, 255.0f));
        dst[1] = static_cast<uint8_t>(std::clamp(accG * 255.0f, 0.0f, 255.0f));
        dst[2] = static_cast<uint8_t>(std::clamp(accB * 255.0f, 0.0f, 255.0f));
    } else if (m_numCh >= 3) {
        uint8_t* dst = row + x * 3;
        dst[0] = mapPixelToUint8(src, srcElem + 0, m_dt, m_rMin, m_rMax);
        dst[1] = mapPixelToUint8(src, srcElem + 1, m_dt, m_gMin, m_gMax);
        dst[2] = mapPixelToUint8(src, srcElem + 2, m_dt, m_bMin, m_bMax);
    } else {
        row[x] = mapPixelToUint8(src, srcElem, m_dt, m_globalMin, m_globalMax);
    }
}

QImage OverviewRenderer::render(const core::TileData& block) const
{
    if (block.isEmpty() || block.isError()) return {};
    const int w = block.width();
    const int h = block.height();
    if (w <= 0 || h <= 0) return {};
    QImage img(w, h, format());
    img.fill(m_isBrightfield ? Qt::white : Qt::black);
    const uint8_t* src = block.buffer().data();
    const int srcCh = block.numChannels();
    for (int y = 0; y < h; ++y) {
        uint8_t* row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            writePixel(row, x, src, static_cast<size_t>((y * w + x) * srcCh), srcCh);
        }
    }
    return img;
}

QImage renderOverviewImage(const core::TileData& block, const core::SlideInfo& info)
{
    return OverviewRenderer(info, info.displayRange.displayMin, info.displayRange.displayMax).render(block);
}

bool shouldPublishBuiltOverview(bool openTimeOverviewAvailable, bool builtLevelEmpty)
{
    return !openTimeOverviewAvailable && !builtLevelEmpty;
}

bool shouldShowOverviewBuilding(bool builtOverviewPending, bool openTimeOverviewAvailable)
{
    return builtOverviewPending && !openTimeOverviewAvailable;
}

} // namespace slideio::viewer::ui
