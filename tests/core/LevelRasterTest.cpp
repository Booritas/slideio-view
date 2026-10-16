#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/LevelRaster.h"

#include <cstdint>
#include <vector>

using namespace slideio::viewer;

namespace
{

/// A raster whose every pixel encodes its own position, so a tile can be checked
/// against where it was cut from rather than merely against a size.
std::vector<uint8_t> positionRaster(int width, int height, int numChannels)
{
    std::vector<uint8_t> raster(static_cast<size_t>(width) * height * numChannels);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int ch = 0; ch < numChannels; ++ch) {
                const size_t i = (static_cast<size_t>(y) * width + x) * numChannels + ch;
                raster[i] = static_cast<uint8_t>((x * 7 + y * 13 + ch * 31) % 251);
            }
        }
    }
    return raster;
}

uint8_t expectedAt(int x, int y, int ch)
{
    return static_cast<uint8_t>((x * 7 + y * 13 + ch * 31) % 251);
}

} // namespace

TEST_CASE("sliceLevelRaster cuts an evenly divisible raster into its tile grid", "[levelraster]")
{
    const int width = 8;
    const int height = 6;
    const int channels = 3;
    const auto raster = positionRaster(width, height, channels);

    const auto tiles = core::sliceLevelRaster(raster.data(), width, height, channels,
                                              core::DataType::Byte, 4, 3);

    // 2x2 grid, row-major: (0,0) (1,0) (0,1) (1,1).
    REQUIRE(tiles.size() == 4);
    CHECK(tiles[0].column == 0);
    CHECK(tiles[0].row == 0);
    CHECK(tiles[1].column == 1);
    CHECK(tiles[1].row == 0);
    CHECK(tiles[2].column == 0);
    CHECK(tiles[2].row == 1);
    CHECK(tiles[3].column == 1);
    CHECK(tiles[3].row == 1);

    for (const auto& tile : tiles) {
        CHECK(tile.data.width() == 4);
        CHECK(tile.data.height() == 3);
        CHECK(tile.data.numChannels() == channels);
        CHECK(tile.data.dataType() == core::DataType::Byte);
        CHECK_FALSE(tile.data.isError());
        REQUIRE(tile.data.buffer().size() == static_cast<size_t>(4) * 3 * channels);
    }

    // The bottom-right tile must hold the bottom-right of the raster, not a
    // repeat of the first tile: that is the difference between slicing and
    // copying the same sub-rect four times.
    const auto& last = tiles[3].data;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            for (int ch = 0; ch < channels; ++ch) {
                const size_t i = (static_cast<size_t>(y) * 4 + x) * channels + ch;
                CHECK(last.buffer()[i] == expectedAt(x + 4, y + 3, ch));
            }
        }
    }
}

TEST_CASE("sliceLevelRaster clips the tiles at the right and bottom edges", "[levelraster]")
{
    // 5x4 with 3x3 tiles: the grid is 2x2 but only the top-left tile is full.
    // Clipping matches SlideIOAdapter::readTile, which asks for
    // min(tileWidth, level width - tile origin) -- an edge tile is the part of
    // the level that exists, not a padded full tile.
    const int width = 5;
    const int height = 4;
    const int channels = 1;
    const auto raster = positionRaster(width, height, channels);

    const auto tiles = core::sliceLevelRaster(raster.data(), width, height, channels,
                                              core::DataType::Byte, 3, 3);

    REQUIRE(tiles.size() == 4);
    CHECK(tiles[0].data.width() == 3);
    CHECK(tiles[0].data.height() == 3);
    CHECK(tiles[1].data.width() == 2);   // 5 - 3
    CHECK(tiles[1].data.height() == 3);
    CHECK(tiles[2].data.width() == 3);
    CHECK(tiles[2].data.height() == 1);  // 4 - 3
    CHECK(tiles[3].data.width() == 2);
    CHECK(tiles[3].data.height() == 1);

    // The clipped corner tile reads from (3,3), the raster's last row.
    const auto& corner = tiles[3].data;
    REQUIRE(corner.buffer().size() == 2);
    CHECK(corner.buffer()[0] == expectedAt(3, 3, 0));
    CHECK(corner.buffer()[1] == expectedAt(4, 3, 0));
}

TEST_CASE("sliceLevelRaster carries a wider pixel type through by byte width", "[levelraster]")
{
    // UInt16 doubles every pixel's byte width. The function copies bytes and is
    // told the type only so the tiles can report it, so this is the check that
    // the stride arithmetic uses the type's size and not a hardcoded 1.
    const int width = 4;
    const int height = 2;
    const int channels = 2;
    std::vector<uint8_t> raster(static_cast<size_t>(width) * height * channels * 2);
    for (size_t i = 0; i < raster.size(); ++i) {
        raster[i] = static_cast<uint8_t>(i % 251);
    }

    const auto tiles = core::sliceLevelRaster(raster.data(), width, height, channels,
                                              core::DataType::UInt16, 2, 2);

    REQUIRE(tiles.size() == 2);
    CHECK(tiles[0].data.dataType() == core::DataType::UInt16);
    REQUIRE(tiles[0].data.buffer().size() == static_cast<size_t>(2) * 2 * channels * 2);

    // Second tile, first pixel: row 0, x = 2 -> byte offset (0*4 + 2) * 2 * 2.
    const size_t sourceOffset = (static_cast<size_t>(0) * width + 2) * channels * 2;
    CHECK(tiles[1].data.buffer()[0] == raster[sourceOffset]);
    CHECK(tiles[1].data.buffer()[1] == raster[sourceOffset + 1]);
}

TEST_CASE("sliceLevelRaster refuses a raster it cannot address", "[levelraster]")
{
    const std::vector<uint8_t> raster(16, 0);
    CHECK(core::sliceLevelRaster(raster.data(), 0, 4, 1, core::DataType::Byte, 2, 2).empty());
    CHECK(core::sliceLevelRaster(raster.data(), 4, 0, 1, core::DataType::Byte, 2, 2).empty());
    CHECK(core::sliceLevelRaster(raster.data(), 4, 4, 0, core::DataType::Byte, 2, 2).empty());
    CHECK(core::sliceLevelRaster(raster.data(), 4, 4, 1, core::DataType::Byte, 0, 2).empty());
    CHECK(core::sliceLevelRaster(nullptr, 4, 4, 1, core::DataType::Byte, 2, 2).empty());
}
