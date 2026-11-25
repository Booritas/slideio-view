#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace slideio::viewer::core
{

/// Coarse density grid over a results layer that has too many objects to
/// outline. Fed every object's bounds and class indices from a background
/// scan; cells() yields the coloured squares to draw.
class OverviewGrid
{
public:
    static constexpr int kMaxSquaresLongSide = 1024;
    static constexpr int kTrackedClasses = 15;
    static constexpr int kOtherClasses = -2;
    static constexpr int kUnclassified = -1;

    /// sceneRect: level-0 scene rectangle; classCounts: the layer's per-class
    /// object counts (legend order).
    OverviewGrid(const Rect<double>& sceneRect, const std::vector<int64_t>& classCounts);

    /// Counts an object in the square holding the centre of `bounds`.
    void add(const Rect<double>& bounds, const std::vector<int>& classes);
    /// Computes the 99th-percentile square count; call after the last add().
    void finish();

    double squareSize() const { return m_squareSize; }
    int columns() const { return m_columns; }
    int rows() const { return m_rows; }

    struct Cell
    {
        int column = 0;
        int row = 0;
        int dominant = kUnclassified; ///< class index, kOtherClasses or kUnclassified
        double opacity = 0.0;
    };

    /// Non-empty squares intersecting `view` with at least one visible object.
    /// classVisible is indexed by the layer's class index.
    std::vector<Cell> cells(const Rect<double>& view, const std::vector<bool>& classVisible,
                            bool unclassifiedVisible) const;

private:
    struct Square
    {
        uint32_t objects = 0;
        std::vector<uint32_t> buckets; ///< tracked classes, Other classes, Unclassified
    };

    Rect<double> m_scene;
    double m_squareSize = 1.0;
    int m_columns = 1;
    int m_rows = 1;
    std::size_t m_classCount = 0;
    std::vector<int> m_tracked;          ///< class indices of the tracked classes, ascending
    std::vector<int> m_bucketOfClass;    ///< class index -> bucket
    double m_p99 = 1.0;
    std::unordered_map<int64_t, Square> m_squares;
};

} // namespace slideio::viewer::core
