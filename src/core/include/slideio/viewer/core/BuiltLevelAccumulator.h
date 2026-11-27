#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <utility>
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
    /// `residentFactors` are strictly ascending powers of two, each a multiple of
    /// the previous, e.g. {8, 16, 32}. Throws std::invalid_argument for a
    /// non-positive size or channel count, a data type with no sample size
    /// (Unknown, None), or an empty or malformed factor list.
    BuiltLevelAccumulator(int sourceWidth, int sourceHeight, int numChannels, DataType dataType,
                          std::vector<int> residentFactors);

    int finestFactor() const;

    /// Interleaved pixels, row stride `w * numChannels * dataTypeSize`.
    /// Bands arrive in order: `y` is the current band's top and `h` a multiple
    /// of `finestFactor()` except for the last band. Chunks of one band arrive
    /// left to right (x equals the width covered so far) and together cover
    /// [0, sourceWidth); the first band starts at y = 0 and each next one where
    /// the last ended; y/h are identical for every chunk of a band; no chunk
    /// follows complete(). Throws std::invalid_argument, before any state is
    /// touched, for a null pointer, w/h <= 0, a chunk outside the source, a
    /// band whose h is not a multiple of finestFactor() unless it ends at
    /// sourceHeight, or any ordering violation above.
    void addChunk(int x, int y, int w, int h, const uint8_t* pixels);

    /// Counts as coverage and marks the chunk's columns of the band failed --
    /// only those columns: one unreadable chunk must not blank the full width
    /// of the slide. A box a failed chunk only partly covers is failed too.
    /// Same contract and exceptions as addChunk.
    void failChunk(int x, int y, int w, int h);

    /// Rows [y0, y1) of resident level `residentIndex` (0 = finest), across the
    /// whole width, clamped to the level. An empty range is Ready. Failed if any
    /// box in the range failed.
    BuiltRegionState regionState(int residentIndex, int y0, int y1) const;

    /// The box [x0, x1) x [y0, y1) of that level, clamped to it. Failed only if
    /// a failed box lies inside it, so a tile beside a failed chunk is Ready.
    BuiltRegionState regionState(int residentIndex, int x0, int y0, int x1, int y1) const;

    /// Copy of a region of that level, clipped to it. Callers check
    /// `regionState` first; an unfinished region holds whatever is in the buffer.
    TileData extract(int residentIndex, int x, int y, int w, int h) const;

    int levelWidth(int residentIndex) const;
    int levelHeight(int residentIndex) const;

    bool complete() const;

private:
    /// Half-open column range [begin, end) of failed boxes in one row.
    using Span = std::pair<int, int>;
    /// Sorted, disjoint, non-adjacent spans.
    using Spans = std::vector<Span>;

    struct Level
    {
        int factor = 1;
        int width = 0;
        int height = 0;
        int completedRows = 0;
        std::vector<uint8_t> data;
        std::vector<Spans> failedSpans; // one per row; empty for a row with no failure
    };

    static void addSpan(Spans& spans, int begin, int end);
    static bool spansIntersect(const Spans& spans, int begin, int end);

    void beginBand(int y, int h);
    void noteCoverage(int w);
    void validateChunk(int x, int y, int w, int h) const;
    void finalizeBand();
    void propagate();

    int m_sourceWidth;
    int m_sourceHeight;
    int m_numChannels;
    DataType m_dataType;
    std::vector<Level> m_levels;

    int m_nextBandY = 0;
    bool m_bandActive = false;
    int m_bandY = 0;
    int m_bandHeight = 0;
    int m_bandCovered = 0;
    Spans m_bandFailedSpans; // finest-level box columns of the active band's failed chunks
    std::vector<double> m_bandSum;
    std::vector<uint32_t> m_bandCount;
};

} // namespace slideio::viewer::core
