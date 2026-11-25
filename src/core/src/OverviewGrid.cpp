#include "slideio/viewer/core/OverviewGrid.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace slideio::viewer::core
{

OverviewGrid::OverviewGrid(const Rect<double>& sceneRect, const std::vector<int64_t>& classCounts)
    : m_scene(sceneRect), m_classCount(classCounts.size())
{
    const double longer = std::max(sceneRect.width, sceneRect.height);
    m_squareSize = std::max(1.0, std::ceil(longer / kMaxSquaresLongSide));
    m_columns = std::max(1, static_cast<int>(std::ceil(sceneRect.width / m_squareSize)));
    m_rows = std::max(1, static_cast<int>(std::ceil(sceneRect.height / m_squareSize)));

    std::vector<int> order(classCounts.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return classCounts[a] > classCounts[b]; });
    const std::size_t tracked = std::min<std::size_t>(order.size(), kTrackedClasses);
    m_tracked.assign(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(tracked));
    std::sort(m_tracked.begin(), m_tracked.end());

    m_bucketOfClass.assign(classCounts.size(), static_cast<int>(tracked));
    for (std::size_t i = 0; i < m_tracked.size(); ++i)
        m_bucketOfClass[static_cast<std::size_t>(m_tracked[i])] = static_cast<int>(i);
}

void OverviewGrid::add(const Rect<double>& bounds, const std::vector<int>& classes)
{
    const double cx = bounds.x + bounds.width / 2.0;
    const double cy = bounds.y + bounds.height / 2.0;
    const int col = std::clamp(static_cast<int>(std::floor((cx - m_scene.x) / m_squareSize)), 0, m_columns - 1);
    const int row = std::clamp(static_cast<int>(std::floor((cy - m_scene.y) / m_squareSize)), 0, m_rows - 1);

    Square& sq = m_squares[static_cast<int64_t>(row) * m_columns + col];
    if (sq.buckets.empty())
        sq.buckets.assign(m_tracked.size() + 2, 0);
    ++sq.objects;

    std::vector<int> seen;
    for (int c : classes)
    {
        if (c < 0 || static_cast<std::size_t>(c) >= m_classCount)
            continue;
        const int b = m_bucketOfClass[static_cast<std::size_t>(c)];
        // A bucket is carried once per object even when two member classes map to it.
        if (std::find(seen.begin(), seen.end(), b) != seen.end())
            continue;
        seen.push_back(b);
        ++sq.buckets[static_cast<std::size_t>(b)];
    }
    if (seen.empty())
        ++sq.buckets[m_tracked.size() + 1];
}

void OverviewGrid::finish()
{
    std::vector<uint32_t> counts;
    counts.reserve(m_squares.size());
    for (const auto& kv : m_squares)
        counts.push_back(kv.second.objects);
    if (counts.empty())
    {
        m_p99 = 1.0;
        return;
    }
    std::sort(counts.begin(), counts.end());
    // Nearest rank.
    const auto rank = static_cast<std::size_t>(std::ceil(0.99 * static_cast<double>(counts.size())));
    m_p99 = std::max<double>(1.0, counts[std::max<std::size_t>(rank, 1) - 1]);
}

std::vector<OverviewGrid::Cell> OverviewGrid::cells(const Rect<double>& view,
                                                    const std::vector<bool>& classVisible,
                                                    bool unclassifiedVisible) const
{
    const std::size_t T = m_tracked.size();
    std::vector<bool> bucketVisible(T + 2, false);
    for (std::size_t i = 0; i < T; ++i)
    {
        const auto c = static_cast<std::size_t>(m_tracked[i]);
        bucketVisible[i] = c < classVisible.size() && classVisible[c];
    }
    for (std::size_t c = 0; c < m_classCount; ++c)
        if (m_bucketOfClass[c] == static_cast<int>(T) && c < classVisible.size() && classVisible[c])
            bucketVisible[T] = true;
    bucketVisible[T + 1] = unclassifiedVisible;

    std::vector<Cell> out;
    for (const auto& kv : m_squares)
    {
        const int row = static_cast<int>(kv.first / m_columns);
        const int col = static_cast<int>(kv.first % m_columns);
        const double x0 = m_scene.x + col * m_squareSize;
        const double y0 = m_scene.y + row * m_squareSize;
        if (x0 >= view.x + view.width || x0 + m_squareSize <= view.x ||
            y0 >= view.y + view.height || y0 + m_squareSize <= view.y)
            continue;

        uint64_t sum = 0;
        std::size_t best = T + 2;
        uint32_t bestCount = 0;
        for (std::size_t b = 0; b < T + 2; ++b)
        {
            if (!bucketVisible[b])
                continue;
            const uint32_t n = kv.second.buckets[b];
            sum += n;
            if (n > bestCount) // strict: the first bucket wins a tie
            {
                bestCount = n;
                best = b;
            }
        }
        const uint64_t visible = std::min<uint64_t>(kv.second.objects, sum);
        if (visible == 0 || best == T + 2)
            continue;

        Cell cell;
        cell.column = col;
        cell.row = row;
        cell.dominant = best < T ? m_tracked[best] : (best == T ? kOtherClasses : kUnclassified);
        cell.opacity = 0.15 + 0.60 * std::min(1.0, static_cast<double>(visible) / m_p99);
        out.push_back(cell);
    }
    std::sort(out.begin(), out.end(), [](const Cell& a, const Cell& b) {
        return a.row != b.row ? a.row < b.row : a.column < b.column;
    });
    return out;
}

} // namespace slideio::viewer::core
