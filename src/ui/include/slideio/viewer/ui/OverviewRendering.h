#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/Types.h"

#include <QImage>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace slideio::viewer::ui
{

/// Map one sample of the given native type through [minVal, maxVal] to [0, 1].
/// A non-positive range maps to 0.
double mapPixelNormalized(const uint8_t* buffer, size_t elementIndex, core::DataType dataType,
                          double minVal, double maxVal);

/// The same mapping rounded to [0, 255].
uint8_t mapPixelToUint8(const uint8_t* buffer, size_t elementIndex, core::DataType dataType,
                        double minVal, double maxVal);

/// Converts native-type pixels to the minimap's 8-bit image: the per-channel mix /
/// RGB / grayscale rules shared by the open-time thumbnail and the overview built
/// from the viewer-built pyramid levels, so the two cannot drift.
///
/// Uses the same per-channel display ranges as the viewport's multi-channel render
/// path (each channel stretched independently), falling back to the global range
/// for single-channel rendering or when per-channel ranges weren't detected. All
/// visible channels are mixed with ChannelInfo::colorR/G/B * intensity to mirror
/// the viewport's channelShader path; a naive src[0]->R / src[1]->G / src[2]->B
/// copy would swap R and B on 3-channel brightfield CZIs whose BGR planes SlideIO
/// labels blue/green/red.
class OverviewRenderer
{
public:
    OverviewRenderer(const core::SlideInfo& info, double globalMin, double globalMax);

    QImage::Format format() const;

    /// Writes the pixel at column x of scanline `row`; srcElem is the element
    /// index of the pixel's first channel in `src`, srcCh its channel count.
    void writePixel(uint8_t* row, int x, const uint8_t* src, size_t srcElem, int srcCh) const;

    /// Renders a whole block; a null image for an empty or error block.
    QImage render(const core::TileData& block) const;

private:
    struct ChannelMix
    {
        double minVal = 0.0;
        double maxVal = 1.0;
        float r = 1.0f, g = 1.0f, b = 1.0f; // colorR/G/B * intensity, premultiplied
        bool active = false;
    };

    int m_numCh;
    core::DataType m_dt;
    bool m_isBrightfield;
    double m_globalMin, m_globalMax;
    double m_rMin = 0, m_rMax = 0, m_gMin = 0, m_gMax = 0, m_bMin = 0, m_bMax = 0;
    bool m_useChannelMix = false;
    std::vector<ChannelMix> m_mix;
};

/// The overview of a block as the minimap shows it, from SlideInfo alone: the
/// global range is the slide's display range.
QImage renderOverviewImage(const core::TileData& block, const core::SlideInfo& info);

/// Whether a completed build publishes its smallest level as the minimap's
/// overview: only when the open produced no overview of its own -- an open-time
/// thumbnail is never replaced -- and the build left one to publish (spec
/// 2026-11-27 section 5.5).
bool shouldPublishBuiltOverview(bool openTimeOverviewAvailable, bool builtLevelEmpty);

/// Whether the minimap says an overview is being built: a build is pending and
/// the open produced no overview, so the built one is the only one coming.
bool shouldShowOverviewBuilding(bool builtOverviewPending, bool openTimeOverviewAvailable);

} // namespace slideio::viewer::ui
