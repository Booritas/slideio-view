#pragma once

#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace slideio::viewer::core
{

/// Tracks which pyramid levels a slide source has found unreliable (e.g. a
/// corrupt tile-offset table) and tells interested parties about them.
///
/// Listeners register at different points in a slide's life: the loading
/// overlay's status callback is installed as soon as the source opens, while
/// the tile scheduler's cache-eviction listener only appears once the scheduler
/// is built — after the coarse-level pass has already read, and cached, every
/// tile of the coarsest level. A listener is therefore invoked for levels
/// already marked when it registers, as well as for levels marked afterwards.
///
/// The guarantee is that every listener hears about every unreliable level
/// exactly once. The order is unspecified: a replay may interleave with a
/// concurrent mark.
class LevelUnreliableRegistry
{
public:
    /// Registers a listener and immediately invokes it once for every level
    /// already marked unreliable.
    void addListener(std::function<void(int)> listener);

    /// Marks a level unreliable. Returns true the first time a given level is
    /// marked, notifying every listener; returns false for a level already
    /// known, notifying nobody.
    bool mark(int level);

    bool isUnreliable(int level) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_set<int> m_levels;
    std::vector<std::function<void(int)>> m_listeners;
};

} // namespace slideio::viewer::core
