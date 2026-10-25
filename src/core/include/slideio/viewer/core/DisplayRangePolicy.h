#pragma once

#include "slideio/viewer/core/Types.h"

#include <utility>

namespace slideio::viewer::core
{

/// Every value a pixel of this type can hold, as {min, max}. What Reset puts
/// a channel back to, and what a slide rendered natively is drawn at.
std::pair<double, double> dataTypeFullRange(DataType dt);

/// True when a slide's pixel values are already the colours to show: 8-bit
/// RGB brightfield. Stretching R, G and B each to its own detected range
/// changes the stain's colour balance, so such a slide opens at the full
/// 0..255 on every channel and is stretched only when the user asks.
///
/// Spelled out rather than read from `isBrightfield` alone, which is also
/// true for one-channel grayscale -- a slide that keeps its detected range,
/// because a 16-bit one would otherwise render nearly black.
bool rendersNativeRange(bool isBrightfield, int numChannels, DataType dt);

/// Puts every channel's display range and the slide-wide one at the data
/// type's full range, not marked as detected. `autoDisplayRange` -- the Auto
/// button's snapshot of what detection found -- is left as it is, so the
/// stretch stays one click away. Does nothing unless `rendersNativeRange`.
void applyNativeDisplayRange(SlideInfo& info);

/// Gives a one-channel slide's channel the slide-wide range, with its detected
/// flag. Detection computes per-channel ranges only for several channels, and
/// for one channel the two are the same range -- without this the channel
/// keeps the default 0..255, so the Channels pane shows numbers the image is
/// not drawn at and Auto stays disabled. Leaves a range the user set alone.
void adoptSlideRangeForSingleChannel(SlideInfo& info);

/// The range a channel is drawn at: its own once the user set it or detection
/// found it, otherwise the slide-wide range. The one rule for every render
/// path, so a Min/Max typed in the Channels pane cannot reach one and not
/// another.
std::pair<double, double> channelDisplayRange(const ChannelInfo& channel, const DisplayRange& slideRange);

} // namespace slideio::viewer::core
