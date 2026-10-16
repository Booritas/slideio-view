#include "slideio/viewer/core/LevelRaster.h"

#include <algorithm>
#include <cstring>

namespace slideio::viewer::core
{

std::vector<LevelRasterTile> sliceLevelRaster(const uint8_t* raster,
                                              int rasterWidth,
                                              int rasterHeight,
                                              int numChannels,
                                              DataType dataType,
                                              int tileWidth,
                                              int tileHeight)
{
    std::vector<LevelRasterTile> tiles;
    if (raster == nullptr || rasterWidth <= 0 || rasterHeight <= 0 || numChannels <= 0
        || tileWidth <= 0 || tileHeight <= 0) {
        return tiles;
    }

    const size_t pixelBytes = dataTypeSize(dataType);
    if (pixelBytes == 0) {
        return tiles;
    }

    const size_t sampleBytes = pixelBytes * static_cast<size_t>(numChannels);
    const size_t rasterStride = sampleBytes * static_cast<size_t>(rasterWidth);

    const int tilesX = (rasterWidth + tileWidth - 1) / tileWidth;
    const int tilesY = (rasterHeight + tileHeight - 1) / tileHeight;
    tiles.reserve(static_cast<size_t>(tilesX) * static_cast<size_t>(tilesY));

    for (int row = 0; row < tilesY; ++row) {
        const int originY = row * tileHeight;
        const int height = std::min(tileHeight, rasterHeight - originY);
        for (int column = 0; column < tilesX; ++column) {
            const int originX = column * tileWidth;
            const int width = std::min(tileWidth, rasterWidth - originX);

            const size_t tileStride = sampleBytes * static_cast<size_t>(width);
            std::vector<uint8_t> buffer(tileStride * static_cast<size_t>(height));
            for (int y = 0; y < height; ++y) {
                const size_t source = (static_cast<size_t>(originY + y) * rasterStride)
                                    + (static_cast<size_t>(originX) * sampleBytes);
                std::memcpy(buffer.data() + tileStride * static_cast<size_t>(y),
                            raster + source, tileStride);
            }

            LevelRasterTile tile;
            tile.column = column;
            tile.row = row;
            tile.data = TileData(std::move(buffer), width, height, numChannels, dataType);
            tiles.push_back(std::move(tile));
        }
    }

    return tiles;
}

} // namespace slideio::viewer::core
