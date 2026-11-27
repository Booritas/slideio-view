#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/BuiltLevelAccumulator.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

template <typename T>
std::vector<uint8_t> bytesOf(const std::vector<T>& v)
{
    std::vector<uint8_t> out(v.size() * sizeof(T));
    std::memcpy(out.data(), v.data(), out.size());
    return out;
}

template <typename T>
std::vector<T> valuesOf(const TileData& t)
{
    std::vector<T> out(t.buffer().size() / sizeof(T));
    std::memcpy(out.data(), t.buffer().data(), t.buffer().size());
    return out;
}

/// Averages one 2x2 box of `T` values and returns the single resulting sample.
template <typename T>
T averageOf(DataType dt, const std::vector<T>& box)
{
    BuiltLevelAccumulator acc(2, 2, 1, dt, {2});
    const auto bytes = bytesOf(box);
    acc.addChunk(0, 0, 2, 2, bytes.data());
    REQUIRE(acc.complete());
    const TileData out = acc.extract(0, 0, 0, 1, 1);
    REQUIRE(out.width() == 1);
    REQUIRE(out.height() == 1);
    return valuesOf<T>(out).at(0);
}

uint16_t toHalf(float f)
{
    // Only the exactly representable values used below.
    if (f == 1.0f) return 0x3C00;
    if (f == 2.0f) return 0x4000;
    if (f == 2.5f) return 0x4100;
    if (f == 3.0f) return 0x4200;
    if (f == 4.0f) return 0x4400;
    return 0;
}

std::vector<uint8_t> pattern(int w, int h, int channels)
{
    std::vector<uint8_t> v(static_cast<size_t>(w) * h * channels);
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<uint8_t>((i * 37 + 11) % 251);
    }
    return v;
}

} // namespace

TEST_CASE("factor-2 box average of a 5x3 byte image", "[core][BuiltLevelAccumulator]")
{
    BuiltLevelAccumulator acc(5, 3, 1, DataType::Byte, {2});
    const std::vector<uint8_t> src = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 21, 22, 23, 24};
    acc.addChunk(0, 0, 5, 3, src.data());

    REQUIRE(acc.levelWidth(0) == 3);
    REQUIRE(acc.levelHeight(0) == 2);
    REQUIRE(acc.complete());
    const TileData out = acc.extract(0, 0, 0, 3, 2);
    REQUIRE(valuesOf<uint8_t>(out) == std::vector<uint8_t>{6, 10, 13, 21, 23, 24});
}

TEST_CASE("partial edge boxes average only real pixels", "[core][BuiltLevelAccumulator]")
{
    BuiltLevelAccumulator acc(3, 3, 1, DataType::Byte, {4});
    const std::vector<uint8_t> src = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    acc.addChunk(0, 0, 3, 3, src.data());

    REQUIRE(acc.levelWidth(0) == 1);
    REQUIRE(acc.levelHeight(0) == 1);
    REQUIRE(valuesOf<uint8_t>(acc.extract(0, 0, 0, 1, 1)) == std::vector<uint8_t>{5});
}

TEST_CASE("rounding is half away from zero", "[core][BuiltLevelAccumulator]")
{
    auto avg = [](int16_t a, int16_t b) {
        BuiltLevelAccumulator acc(2, 1, 1, DataType::Int16, {2});
        const auto bytes = bytesOf(std::vector<int16_t>{a, b});
        acc.addChunk(0, 0, 2, 1, bytes.data());
        return valuesOf<int16_t>(acc.extract(0, 0, 0, 1, 1)).at(0);
    };
    REQUIRE(avg(-1, -2) == -2);
    REQUIRE(avg(1, 2) == 2);
}

TEST_CASE("every DataType with a size averages exactly", "[core][BuiltLevelAccumulator]")
{
    REQUIRE(averageOf<uint8_t>(DataType::Byte, {10, 20, 30, 40}) == 25);
    REQUIRE(averageOf<int8_t>(DataType::Int8, {-10, -20, -30, -40}) == -25);
    REQUIRE(averageOf<uint16_t>(DataType::UInt16, {1000, 2000, 3000, 4000}) == 2500);
    REQUIRE(averageOf<int16_t>(DataType::Int16, {-1000, -2000, 3000, 4000}) == 1000);
    REQUIRE(averageOf<uint32_t>(DataType::UInt32, {4000000000u, 4000000000u, 4000000000u, 4000000000u})
            == 4000000000u);
    REQUIRE(averageOf<int32_t>(DataType::Int32, {-2000000000, -2000000000, 2000000000, 0}) == -500000000);
    REQUIRE(averageOf<int64_t>(DataType::Int64, {-4000, -8000, 4000, 8000}) == 0);
    REQUIRE(averageOf<float>(DataType::Float32, {1.0f, 2.0f, 3.0f, 4.0f}) == 2.5f);
    REQUIRE(averageOf<double>(DataType::Float64, {1.0, 2.0, 3.0, 4.0}) == 2.5);
    REQUIRE(averageOf<uint16_t>(DataType::Float16, {toHalf(1.0f), toHalf(2.0f), toHalf(3.0f), toHalf(4.0f)})
            == toHalf(2.5f));

    SECTION("UInt64 near the maximum does not overflow")
    {
        const uint64_t base = ~0ULL - 16383; // 2^64 - 16384: every partial double sum stays exact
        const uint64_t mean = averageOf<uint64_t>(DataType::UInt64,
                                                  {base, base - 16384, base - 32768, base - 49152});
        REQUIRE(mean == base - 24576);
    }

    SECTION("channels stay separate")
    {
        BuiltLevelAccumulator acc(2, 2, 3, DataType::UInt16, {2});
        const auto bytes = bytesOf(std::vector<uint16_t>{
            100, 1000, 10000, 200, 2000, 20000, //
            300, 3000, 30000, 400, 4000, 40000});
        acc.addChunk(0, 0, 2, 2, bytes.data());
        const TileData out = acc.extract(0, 0, 0, 1, 1);
        REQUIRE(out.numChannels() == 3);
        REQUIRE(valuesOf<uint16_t>(out) == std::vector<uint16_t>{250, 2500, 25000});
    }
}

TEST_CASE("chunks across the width equal one full-width band", "[core][BuiltLevelAccumulator]")
{
    const int w = 10;
    const int h = 4;
    const auto src = pattern(w, h, 3);

    BuiltLevelAccumulator whole(w, h, 3, DataType::Byte, {2});
    whole.addChunk(0, 0, w, h, src.data());

    BuiltLevelAccumulator pieces(w, h, 3, DataType::Byte, {2});
    const int cuts[][2] = {{0, 3}, {3, 8}, {8, 10}};
    for (const auto& cut : cuts) {
        const int cw = cut[1] - cut[0];
        std::vector<uint8_t> chunk(static_cast<size_t>(cw) * h * 3);
        for (int r = 0; r < h; ++r) {
            std::memcpy(chunk.data() + static_cast<size_t>(r) * cw * 3,
                        src.data() + (static_cast<size_t>(r) * w + cut[0]) * 3, static_cast<size_t>(cw) * 3);
        }
        pieces.addChunk(cut[0], 0, cw, h, chunk.data());
    }

    REQUIRE(pieces.complete());
    REQUIRE(whole.extract(0, 0, 0, 5, 2).buffer() == pieces.extract(0, 0, 0, 5, 2).buffer());
}

TEST_CASE("a coarser level is the 2x2 average of the finer one", "[core][BuiltLevelAccumulator]")
{
    const auto src = pattern(8, 8, 1);
    BuiltLevelAccumulator acc(8, 8, 1, DataType::Byte, {2, 4});
    acc.addChunk(0, 0, 8, 8, src.data());
    REQUIRE(acc.complete());

    const auto fine = valuesOf<uint8_t>(acc.extract(0, 0, 0, 4, 4));
    const auto coarse = valuesOf<uint8_t>(acc.extract(1, 0, 0, 2, 2));
    REQUIRE(coarse.size() == 4);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const int sum = fine[(2 * y) * 4 + 2 * x] + fine[(2 * y) * 4 + 2 * x + 1]
                          + fine[(2 * y + 1) * 4 + 2 * x] + fine[(2 * y + 1) * 4 + 2 * x + 1];
            REQUIRE(coarse[y * 2 + x] == static_cast<uint8_t>((sum + 2) / 4));
        }
    }
}

TEST_CASE("regionState follows completed bands", "[core][BuiltLevelAccumulator]")
{
    BuiltLevelAccumulator acc(16, 16, 1, DataType::Byte, {2, 4});
    const auto band = pattern(16, 4, 1);

    REQUIRE(acc.regionState(0, 0, 2) == BuiltRegionState::Pending);
    acc.addChunk(0, 0, 16, 4, band.data());
    REQUIRE(acc.regionState(0, 0, 2) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(0, 0, 3) == BuiltRegionState::Pending);
    REQUIRE(acc.regionState(1, 0, 1) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 0, 2) == BuiltRegionState::Pending);
    REQUIRE(acc.regionState(0, 5, 5) == BuiltRegionState::Ready);
    REQUIRE_FALSE(acc.complete());

    for (int y = 4; y < 16; y += 4) {
        acc.addChunk(0, y, 16, 4, band.data());
    }
    REQUIRE(acc.complete());
    REQUIRE(acc.regionState(0, 0, 8) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 0, 4) == BuiltRegionState::Ready);
}

TEST_CASE("a failed band marks its rows failed and leaves neighbours ready", "[core][BuiltLevelAccumulator]")
{
    // Three bands of six rows: level 0 has nine rows (3 per band), level 1 has five.
    BuiltLevelAccumulator acc(16, 18, 1, DataType::Byte, {2, 4});
    const auto band = pattern(16, 6, 1);
    acc.addChunk(0, 0, 16, 6, band.data());
    acc.failChunk(0, 6, 16, 6);
    acc.addChunk(0, 12, 16, 6, band.data());

    REQUIRE(acc.complete());
    REQUIRE(acc.regionState(0, 0, 3) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(0, 3, 6) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(0, 6, 9) == BuiltRegionState::Ready);

    REQUIRE(acc.regionState(1, 0, 1) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 1, 2) == BuiltRegionState::Failed); // mixes band 0 and the failed band
    REQUIRE(acc.regionState(1, 2, 3) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(1, 3, 5) == BuiltRegionState::Ready);
}

namespace
{

/// Columns [x0, x1) of a `w`-wide single-channel byte band of height `h`.
std::vector<uint8_t> columnsOf(const std::vector<uint8_t>& band, int w, int h, int x0, int x1)
{
    std::vector<uint8_t> out;
    for (int r = 0; r < h; ++r) {
        out.insert(out.end(), band.begin() + r * w + x0, band.begin() + r * w + x1);
    }
    return out;
}

} // namespace

TEST_CASE("a failed chunk fails only its own columns", "[core][BuiltLevelAccumulator]")
{
    // One band of 8 rows, 16 wide: level 0 is 8x4, level 1 is 4x2.
    const auto band = pattern(16, 8, 1);
    BuiltLevelAccumulator acc(16, 8, 1, DataType::Byte, {2, 4});
    const auto left = columnsOf(band, 16, 8, 0, 4);
    const auto right = columnsOf(band, 16, 8, 10, 16);
    acc.addChunk(0, 0, 4, 8, left.data());
    acc.failChunk(4, 0, 6, 8); // source columns [4, 10): level-0 boxes [2, 5), level-1 boxes [1, 3)
    acc.addChunk(10, 0, 6, 8, right.data());
    REQUIRE(acc.complete());

    REQUIRE(acc.regionState(0, 0, 0, 2, 4) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(0, 2, 0, 3, 4) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(0, 4, 0, 5, 4) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(0, 5, 0, 8, 4) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 0, 0, 1, 2) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 1, 0, 3, 2) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(1, 3, 0, 4, 2) == BuiltRegionState::Ready);

    SECTION("the full-width overload still reports the failure")
    {
        REQUIRE(acc.regionState(0, 0, 4) == BuiltRegionState::Failed);
        REQUIRE(acc.regionState(1, 0, 2) == BuiltRegionState::Failed);
    }

    SECTION("boxes beside the failed chunk hold the same values as an unbroken build")
    {
        BuiltLevelAccumulator whole(16, 8, 1, DataType::Byte, {2, 4});
        whole.addChunk(0, 0, 16, 8, band.data());
        REQUIRE(acc.extract(0, 0, 0, 2, 4).buffer() == whole.extract(0, 0, 0, 2, 4).buffer());
        REQUIRE(acc.extract(0, 5, 0, 3, 4).buffer() == whole.extract(0, 5, 0, 3, 4).buffer());
        REQUIRE(acc.extract(1, 3, 0, 1, 2).buffer() == whole.extract(1, 3, 0, 1, 2).buffer());
    }
}

TEST_CASE("a box a failed chunk only partly covers is failed", "[core][BuiltLevelAccumulator]")
{
    const auto band = pattern(16, 4, 1);
    BuiltLevelAccumulator acc(16, 4, 1, DataType::Byte, {2});
    const auto left = columnsOf(band, 16, 4, 0, 5);
    const auto right = columnsOf(band, 16, 4, 7, 16);
    acc.addChunk(0, 0, 5, 4, left.data());
    acc.failChunk(5, 0, 2, 4); // source columns 5 and 6 straddle boxes 2 and 3
    acc.addChunk(7, 0, 9, 4, right.data());

    REQUIRE(acc.regionState(0, 1, 0, 2, 2) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(0, 2, 0, 3, 2) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(0, 3, 0, 4, 2) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(0, 4, 0, 8, 2) == BuiltRegionState::Ready);
}

TEST_CASE("failed chunks of later bands stay in their own columns", "[core][BuiltLevelAccumulator]")
{
    // Two bands of 4 rows; band 0 fails on the left, band 1 on the right. Each
    // band is two level-0 rows and one level-1 row.
    const auto band = pattern(16, 4, 1);
    BuiltLevelAccumulator acc(16, 8, 1, DataType::Byte, {2, 4});
    const auto rightOf0 = columnsOf(band, 16, 4, 8, 16);
    const auto leftOf1 = columnsOf(band, 16, 4, 0, 8);
    acc.failChunk(0, 0, 8, 4);
    acc.addChunk(8, 0, 8, 4, rightOf0.data());
    acc.addChunk(0, 4, 8, 4, leftOf1.data());
    acc.failChunk(8, 4, 8, 4);
    REQUIRE(acc.complete());

    REQUIRE(acc.regionState(1, 0, 0, 2, 1) == BuiltRegionState::Failed);
    REQUIRE(acc.regionState(1, 2, 0, 4, 1) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 0, 1, 2, 2) == BuiltRegionState::Ready);
    REQUIRE(acc.regionState(1, 2, 1, 4, 2) == BuiltRegionState::Failed);
}

TEST_CASE("the constructor rejects an out-of-contract configuration", "[core][BuiltLevelAccumulator]")
{
    using Acc = BuiltLevelAccumulator;
    REQUIRE_THROWS_AS(Acc(0, 4, 1, DataType::Byte, {2}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, -1, 1, DataType::Byte, {2}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 0, DataType::Byte, {2}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 1, DataType::Byte, {}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 1, DataType::Byte, {0}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 1, DataType::Byte, {-2}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 1, DataType::Byte, {3}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 1, DataType::Byte, {4, 2}), std::invalid_argument);
    REQUIRE_THROWS_AS(Acc(4, 4, 1, DataType::Byte, {2, 2}), std::invalid_argument);
    REQUIRE_NOTHROW(Acc(4, 4, 1, DataType::Byte, {2, 8}));
}

TEST_CASE("a data type with no codec is rejected", "[core][BuiltLevelAccumulator]")
{
    REQUIRE_THROWS_AS(BuiltLevelAccumulator(4, 4, 1, DataType::Unknown, {2}), std::invalid_argument);
    REQUIRE_THROWS_AS(BuiltLevelAccumulator(4, 4, 1, DataType::None, {2}), std::invalid_argument);
}

TEST_CASE("chunks outside the contract are rejected", "[core][BuiltLevelAccumulator]")
{
    const auto px = pattern(8, 8, 1);
    BuiltLevelAccumulator acc(8, 8, 1, DataType::Byte, {2});

    SECTION("null pixels") { REQUIRE_THROWS_AS(acc.addChunk(0, 0, 8, 4, nullptr), std::invalid_argument); }
    SECTION("empty size")
    {
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 0, 4, px.data()), std::invalid_argument);
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 8, 0, px.data()), std::invalid_argument);
    }
    SECTION("beyond the source")
    {
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 9, 4, px.data()), std::invalid_argument);
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 8, 10, px.data()), std::invalid_argument);
    }
    SECTION("band not starting at the expected row")
    {
        REQUIRE_THROWS_AS(acc.addChunk(0, 2, 8, 4, px.data()), std::invalid_argument);
    }
    SECTION("non-last band height not a multiple of the factor")
    {
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 8, 3, px.data()), std::invalid_argument);
    }
    SECTION("chunk placement and band consistency")
    {
        acc.addChunk(0, 0, 4, 4, px.data());
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 4, 4, px.data()), std::invalid_argument);  // overlap
        REQUIRE_THROWS_AS(acc.addChunk(6, 0, 2, 4, px.data()), std::invalid_argument);  // gap
        REQUIRE_THROWS_AS(acc.addChunk(4, 0, 4, 6, px.data()), std::invalid_argument);  // different h
        REQUIRE_THROWS_AS(acc.addChunk(4, 2, 4, 4, px.data()), std::invalid_argument);  // different y
        REQUIRE_THROWS_AS(acc.addChunk(4, 0, 5, 4, px.data()), std::invalid_argument);  // past the width
        REQUIRE_THROWS_AS(acc.failChunk(0, 0, 4, 4), std::invalid_argument);            // overlap, failChunk too
    }
    SECTION("a chunk after complete()")
    {
        acc.addChunk(0, 0, 8, 8, px.data());
        REQUIRE(acc.complete());
        REQUIRE_THROWS_AS(acc.addChunk(0, 0, 8, 2, px.data()), std::invalid_argument);
        REQUIRE_THROWS_AS(acc.failChunk(0, 8, 8, 2), std::invalid_argument);
    }
}

TEST_CASE("a rejected chunk leaves the accumulator unchanged", "[core][BuiltLevelAccumulator]")
{
    const auto px = pattern(8, 8, 1);
    BuiltLevelAccumulator clean(8, 8, 1, DataType::Byte, {2, 4});
    clean.addChunk(0, 0, 8, 8, px.data());

    BuiltLevelAccumulator acc(8, 8, 1, DataType::Byte, {2, 4});
    acc.addChunk(0, 0, 4, 4, px.data());
    REQUIRE_THROWS_AS(acc.addChunk(0, 0, 4, 4, px.data()), std::invalid_argument);
    REQUIRE_THROWS_AS(acc.addChunk(4, 0, 4, 8, px.data()), std::invalid_argument);
    REQUIRE_THROWS_AS(acc.addChunk(4, 0, 6, 4, px.data()), std::invalid_argument);

    // Finish band 0 with the right half of its rows, then band 1: same data as one 8x8 chunk.
    std::vector<uint8_t> right(4 * 4), bottom(8 * 4);
    for (int r = 0; r < 4; ++r) {
        std::memcpy(right.data() + r * 4, px.data() + r * 8 + 4, 4);
    }
    // The first chunk above read px as a 4-wide chunk; rebuild the expected result the same way.
    BuiltLevelAccumulator expected(8, 8, 1, DataType::Byte, {2, 4});
    expected.addChunk(0, 0, 4, 4, px.data());
    expected.addChunk(4, 0, 4, 4, right.data());
    acc.addChunk(4, 0, 4, 4, right.data());
    std::memcpy(bottom.data(), px.data() + 32, bottom.size());
    expected.addChunk(0, 4, 8, 4, bottom.data());
    acc.addChunk(0, 4, 8, 4, bottom.data());

    REQUIRE(acc.complete());
    REQUIRE(acc.extract(0, 0, 0, 4, 4).buffer() == expected.extract(0, 0, 0, 4, 4).buffer());
    REQUIRE(acc.extract(1, 0, 0, 2, 2).buffer() == expected.extract(1, 0, 0, 2, 2).buffer());
}
