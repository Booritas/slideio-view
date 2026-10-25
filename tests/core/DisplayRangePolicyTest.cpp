#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/DisplayRangePolicy.h"

using namespace slideio::viewer::core;

namespace
{

// A slide as autodetection leaves it: every range stretched to what the
// sampled tiles held, the Auto snapshot taken from those same values.
SlideInfo detectedSlide(bool isBrightfield, int numChannels, DataType dt)
{
    SlideInfo info;
    info.isBrightfield = isBrightfield;
    info.numChannels = numChannels;
    info.channelDataType = dt;
    info.displayRange = DisplayRange{38.0, 250.0, true};
    for (int ch = 0; ch < numChannels; ++ch) {
        ChannelInfo channel;
        channel.dataType = dt;
        channel.displayRange = DisplayRange{38.0 + ch * 11.0, 250.0 - ch * 3.0, true};
        channel.autoDisplayRange = channel.displayRange;
        info.channels.push_back(channel);
    }
    return info;
}

} // namespace

TEST_CASE("only 8-bit RGB brightfield renders at its native range", "[core][DisplayRangePolicy]")
{
    CHECK(rendersNativeRange(true, 3, DataType::Byte));
    // Grayscale brightfield keeps its detected range: 16-bit would render near black.
    CHECK_FALSE(rendersNativeRange(true, 1, DataType::Byte));
    CHECK_FALSE(rendersNativeRange(true, 1, DataType::UInt16));
    // 3x8-bit fluorescence is not brightfield, and needs its stretch.
    CHECK_FALSE(rendersNativeRange(false, 3, DataType::Byte));
    CHECK_FALSE(rendersNativeRange(true, 3, DataType::UInt16));
}

TEST_CASE("an RGB brightfield slide opens at 0..255 on every channel", "[core][DisplayRangePolicy]")
{
    SlideInfo info = detectedSlide(true, 3, DataType::Byte);
    applyNativeDisplayRange(info);

    // The slide-wide range matters too: a channel not marked detected falls
    // back to it in the renderer and the minimap.
    CHECK(info.displayRange.displayMin == 0.0);
    CHECK(info.displayRange.displayMax == 255.0);
    CHECK_FALSE(info.displayRange.autoDetected);
    for (const auto& channel : info.channels) {
        CHECK(channel.displayRange.displayMin == 0.0);
        CHECK(channel.displayRange.displayMax == 255.0);
        CHECK_FALSE(channel.displayRange.autoDetected);
        CHECK_FALSE(channel.userOverrideRange);
    }
}

TEST_CASE("the Auto snapshot keeps what detection found", "[core][DisplayRangePolicy]")
{
    SlideInfo info = detectedSlide(true, 3, DataType::Byte);
    applyNativeDisplayRange(info);

    CHECK(info.channels[0].autoDisplayRange.displayMin == 38.0);
    CHECK(info.channels[2].autoDisplayRange.displayMin == 60.0);
    CHECK(info.channels[2].autoDisplayRange.autoDetected);
}

TEST_CASE("fluorescence and grayscale keep their detected ranges", "[core][DisplayRangePolicy]")
{
    SlideInfo fluorescence = detectedSlide(false, 3, DataType::Byte);
    applyNativeDisplayRange(fluorescence);
    CHECK(fluorescence.channels[1].displayRange.displayMin == 49.0);
    CHECK(fluorescence.channels[1].displayRange.autoDetected);

    SlideInfo grayscale = detectedSlide(true, 1, DataType::UInt16);
    applyNativeDisplayRange(grayscale);
    CHECK(grayscale.displayRange.displayMin == 38.0);
    CHECK(grayscale.displayRange.autoDetected);
}

TEST_CASE("full ranges follow the data type", "[core][DisplayRangePolicy]")
{
    CHECK(dataTypeFullRange(DataType::Byte) == std::pair<double, double>{0.0, 255.0});
    CHECK(dataTypeFullRange(DataType::UInt16) == std::pair<double, double>{0.0, 65535.0});
    CHECK(dataTypeFullRange(DataType::Int8) == std::pair<double, double>{-128.0, 127.0});
    CHECK(dataTypeFullRange(DataType::Float32) == std::pair<double, double>{0.0, 1.0});
}

TEST_CASE("a one-channel slide's channel takes the slide-wide range", "[core][DisplayRangePolicy]")
{
    // As detection leaves a one-channel slide: only the slide-wide range found.
    SlideInfo info;
    info.isBrightfield = true;
    info.numChannels = 1;
    info.channelDataType = DataType::UInt16;
    info.displayRange = DisplayRange{312.0, 4100.0, true};
    info.channels.emplace_back();

    adoptSlideRangeForSingleChannel(info);

    // Detected, so the panel shows the range the image is drawn at and Auto
    // has something to restore once the snapshot is taken.
    CHECK(info.channels[0].displayRange.displayMin == 312.0);
    CHECK(info.channels[0].displayRange.displayMax == 4100.0);
    CHECK(info.channels[0].displayRange.autoDetected);
    CHECK_FALSE(info.channels[0].userOverrideRange);
}

TEST_CASE("a fallback range is adopted but not marked detected", "[core][DisplayRangePolicy]")
{
    SlideInfo info;
    info.numChannels = 1;
    info.channelDataType = DataType::UInt16;
    info.displayRange = DisplayRange{0.0, 65535.0, false};
    info.channels.emplace_back();

    adoptSlideRangeForSingleChannel(info);

    CHECK(info.channels[0].displayRange.displayMax == 65535.0);
    CHECK_FALSE(info.channels[0].displayRange.autoDetected);
}

TEST_CASE("multi-channel slides and user ranges are left alone", "[core][DisplayRangePolicy]")
{
    SlideInfo multi = detectedSlide(false, 3, DataType::UInt16);
    multi.displayRange = DisplayRange{1.0, 2.0, true};
    adoptSlideRangeForSingleChannel(multi);
    CHECK(multi.channels[1].displayRange.displayMin == 49.0);

    SlideInfo edited = detectedSlide(true, 1, DataType::UInt16);
    edited.channels[0].displayRange = DisplayRange{100.0, 150.0, false};
    edited.channels[0].userOverrideRange = true;
    adoptSlideRangeForSingleChannel(edited);
    CHECK(edited.channels[0].displayRange.displayMin == 100.0);
}

TEST_CASE("a channel is drawn at its own range once set or detected", "[core][DisplayRangePolicy]")
{
    const DisplayRange slide{10.0, 20.0, true};
    ChannelInfo channel;
    channel.displayRange = DisplayRange{100.0, 150.0, false};

    // Neither set nor detected: the default 0..255 must not reach the shader.
    CHECK(channelDisplayRange(channel, slide) == std::pair<double, double>{10.0, 20.0});

    channel.userOverrideRange = true;
    CHECK(channelDisplayRange(channel, slide) == std::pair<double, double>{100.0, 150.0});

    channel.userOverrideRange = false;
    channel.displayRange.autoDetected = true;
    CHECK(channelDisplayRange(channel, slide) == std::pair<double, double>{100.0, 150.0});
}
