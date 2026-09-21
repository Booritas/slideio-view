#include "slideio/viewer/core/LevelUnreliableRegistry.h"

namespace slideio::viewer::core
{

void LevelUnreliableRegistry::addListener(std::function<void(int)> listener)
{
    if (!listener) {
        return;
    }

    std::unordered_set<int> known;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Snapshot and register under one lock acquisition. That is what makes
        // "exactly once" hold: a level in the snapshot is already in m_levels,
        // so a concurrent mark() of it returns false and notifies nobody, while
        // a level marked after this point finds the listener already in the
        // list and notifies it directly.
        known = m_levels;
        m_listeners.push_back(listener);
    }

    // Replay outside the lock — listeners evict from the tile cache and post to
    // the Qt event loop, neither of which belongs in our critical section. Use
    // the local copy, not m_listeners.back(): a concurrent addListener may
    // reallocate the vector and leave that reference dangling.
    for (int level : known) {
        listener(level);
    }
}

bool LevelUnreliableRegistry::mark(int level)
{
    std::vector<std::function<void(int)>> listeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_levels.insert(level).second) {
            return false;
        }
        listeners = m_listeners;
    }

    for (const auto& listener : listeners) {
        listener(level);
    }
    return true;
}

bool LevelUnreliableRegistry::isUnreliable(int level) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_levels.count(level) > 0;
}

} // namespace slideio::viewer::core
