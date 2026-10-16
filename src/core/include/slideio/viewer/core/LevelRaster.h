#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <vector>

namespace slideio::viewer::core
{

/// One tile cut out of a level-sized raster, with the grid position it came from.
struct LevelRasterTile
{
    int column = 0;
    int row = 0;
    TileData data;
};

/// Cuts one whole-level raster into the tiles of that level's grid.
///
/// This exists for levels a slide stores without any tile structure. Reading
/// such a level one tile at a time makes the driver decode the entire level per
/// tile and discard the rest -- on a 17x8 grid that is 136 decodes of one image
/// -- so the level is read once as a single block and divided up here instead.
///
/// Tiles come back in row-major order, rows outer, which is the order the tile
/// grid is walked everywhere else; index order is paint order, and the cache
/// insert that consumes these walks the same way.
///
/// Edge tiles are clipped, not padded: a tile is the part of the level that
/// exists, exactly as SlideIOAdapter::readTile defines it by asking for
/// min(tileWidth, level width - tile origin). A padded edge tile would put
/// invented pixels in the cache under a key the renderer trusts.
///
/// `raster` is read as tightly packed interleaved pixels of `numChannels`
/// components in `dataType`, which is the layout TileData itself carries.
/// `dataType` is used for the stride arithmetic and copied onto every tile.
///
/// Returns an empty vector for any geometry it cannot address -- a null raster,
/// a non-positive dimension, channel count or tile size. The caller is then
/// expected to fall back to reading tiles individually, which is the slow path
/// this avoids rather than a broken one.
[[nodiscard]] std::vector<LevelRasterTile> sliceLevelRaster(const uint8_t* raster,
                                                            int rasterWidth,
                                                            int rasterHeight,
                                                            int numChannels,
                                                            DataType dataType,
                                                            int tileWidth,
                                                            int tileHeight);

} // namespace slideio::viewer::core
