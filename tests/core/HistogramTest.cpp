#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "slideio/viewer/core/Histogram.h"
#include "slideio/viewer/core/Types.h"

using namespace slideio::viewer::core;

TEST_CASE("Histogram of a Byte buffer counts every pixel", "[core][Histogram]")
{
    // 4 pixels, single channel, values 0, 64, 128, 255.
    const std::array<uint8_t, 4> data = {0, 64, 128, 255};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(data.data(), data.size(), 1, 0,
                            DataType::Byte, 0.0, 255.0,
                            bins.data(), kNumBins);

    uint64_t sum = 0;
    for (auto v : bins) sum += v;
    REQUIRE(sum == data.size());

    // Byte range maps exactly: 0 -> bin 0, 255 -> bin 255 (clamped).
    REQUIRE(bins[0] == 1);
    REQUIRE(bins[64] == 1);
    REQUIRE(bins[128] == 1);
    REQUIRE(bins[255] == 1);
}

TEST_CASE("Histogram reads only the requested channel for interleaved data",
          "[core][Histogram]")
{
    // 3 pixels, 2 channels interleaved as [ch0,ch1, ch0,ch1, ch0,ch1].
    // Channel 0 values: 10, 20, 30. Channel 1 values: 200, 210, 220.
    const std::array<uint8_t, 6> data = {10, 200, 20, 210, 30, 220};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(data.data(), /*pixelCount=*/3, /*numChannels=*/2,
                            /*channelIndex=*/0,
                            DataType::Byte, 0.0, 255.0,
                            bins.data(), kNumBins);

    REQUIRE(bins[10] == 1);
    REQUIRE(bins[20] == 1);
    REQUIRE(bins[30] == 1);
    REQUIRE(bins[200] == 0);
    REQUIRE(bins[210] == 0);
    REQUIRE(bins[220] == 0);
}

TEST_CASE("Histogram clamps out-of-range Float32 values to edge bins",
          "[core][Histogram]")
{
    // Float32 buffer: -0.5, 0.0, 0.5, 1.0, 2.0. Range is [0, 1].
    const std::array<float, 5> data = {-0.5f, 0.0f, 0.5f, 1.0f, 2.0f};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), 1, 0,
                            DataType::Float32, 0.0, 1.0,
                            bins.data(), kNumBins);

    // -0.5 and 0.0 both clamp to bin 0; 1.0 and 2.0 both clamp to bin 255.
    REQUIRE(bins[0] == 2);
    REQUIRE(bins[kNumBins - 1] == 2);
    REQUIRE(bins[128] == 1);  // 0.5 maps to mid-bin
}

TEST_CASE("Histogram bins UInt16 values across the full type range",
          "[core][Histogram]")
{
    // UInt16 values: 0, 32768, 65535 -> bins 0, 128, 255.
    const std::array<uint16_t, 3> data = {0u, 32768u, 65535u};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), 1, 0,
                            DataType::UInt16, 0.0, 65535.0,
                            bins.data(), kNumBins);

    REQUIRE(bins[0] == 1);
    REQUIRE(bins[128] == 1);
    REQUIRE(bins[kNumBins - 1] == 1);
}

TEST_CASE("Histogram of an empty buffer leaves bins untouched", "[core][Histogram]")
{
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(nullptr, 0, 1, 0,
                            DataType::Byte, 0.0, 255.0,
                            bins.data(), kNumBins);

    for (auto v : bins) {
        REQUIRE(v == 0);
    }
}

TEST_CASE("Histogram is a no-op when rangeMax <= rangeMin", "[core][Histogram]")
{
    const std::array<uint8_t, 3> data = {1, 2, 3};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(data.data(), data.size(), 1, 0,
                            DataType::Byte, 5.0, 5.0,
                            bins.data(), kNumBins);

    for (auto v : bins) {
        REQUIRE(v == 0);
    }
}
