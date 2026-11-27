#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <vector>

namespace slideio::viewer::core
{

enum class BuiltRegionState
{
    Ready,
    Pending,
    Failed
};

/// Builds coarser pyramid levels from source pixels that arrive band by band.
///
/// The finest resident level is a box average of the source (`finestFactor()`
/// pixels square, clipped at the right and bottom edges). Every coarser level
/// averages the level before it -- finer AVERAGES, not source pixels -- over
/// 2x2 (or 2x1, 1x2, 1x1 at the edges) neighbours, so a pixel at an edge
/// averages only the pixels that exist.
///
/// Integer types are averaged through double, rounded half away from zero and
/// clamped to the type's range; floating types are the plain mean (Float16
/// through float). Int64 and UInt64 sums use double too, so values beyond 2^53
/// lose precision; that is accepted.
///
/// A plain single-threaded value type. Callers that read while another thread
/// feeds it must provide their own locking; the read methods are const.
class BuiltLevelAccumulator
{
public:
    /// `residentFactors` are ascending powers of two, e.g. {8, 16, 32}.
    BuiltLevelAccumulator(int sourceWidth, int sourceHeight, int numChannels, DataType dataType,
                          std::vector<int> residentFactors);

    int finestFactor() const;

    /// Interleaved pixels, row stride `w * numChannels * dataTypeSize`.
    /// Bands arrive in order: `y` is the current band's top and `h` a multiple
    /// of `finestFactor()` except for the last band. Chunks of one band arrive
    /// left to right and together cover [0, sourceWidth).
    void addChunk(int x, int y, int w, int h, const uint8_t* pixels);

    /// Counts as coverage and marks the band failed.
    void failChunk(int x, int y, int w, int h);

    /// Rows [y0, y1) of resident level `residentIndex` (0 = finest), clamped to
    /// the level. An empty range is Ready.
    BuiltRegionState regionState(int residentIndex, int y0, int y1) const;

    /// Copy of a region of that level, clipped to it. Callers check
    /// `regionState` first; an unfinished region holds whatever is in the buffer.
    TileData extract(int residentIndex, int x, int y, int w, int h) const;

    int levelWidth(int residentIndex) const;
    int levelHeight(int residentIndex) const;

    bool complete() const;

private:
    struct Level
    {
        int factor = 1;
        int width = 0;
        int height = 0;
        int completedRows = 0;
        std::vector<uint8_t> data;
        std::vector<char> rowFailed;
    };

    void beginBand(int y, int h);
    void noteCoverage(int w);
    void finalizeBand();
    void propagate();

    int m_sourceWidth;
    int m_sourceHeight;
    int m_numChannels;
    DataType m_dataType;
    std::vector<Level> m_levels;

    bool m_bandActive = false;
    int m_bandY = 0;
    int m_bandHeight = 0;
    int m_bandCovered = 0;
    bool m_bandFailed = false;
    std::vector<double> m_bandSum;
    std::vector<uint32_t> m_bandCount;
};

} // namespace slideio::viewer::core
