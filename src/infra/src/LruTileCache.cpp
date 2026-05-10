#include "slideio/viewer/infra/LruTileCache.h"

#include <spdlog/spdlog.h>

namespace slideio::viewer::infra
{

LruTileCache::LruTileCache(size_t budgetBytes)
    : m_budgetBytes(budgetBytes)
    , m_currentBytes(0)
{
    spdlog::info("LruTileCache: created with budget {} bytes ({:.1f} MB)",
                 budgetBytes, static_cast<double>(budgetBytes) / (1024.0 * 1024.0));
}

LruTileCache::~LruTileCache()
{
    spdlog::debug("LruTileCache: destroying cache with {} entries, {} bytes used",
                  m_map.size(), m_currentBytes);
}

std::shared_ptr<core::TileData> LruTileCache::lookup(const core::TileKey& key) const
{
    // lookup() mutates LRU ordering (move-to-front), which is logically const.
    // The m_lruList, m_map, and m_mutex are declared mutable to support this.
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_map.find(key);
    if (it == m_map.end()) {
        return nullptr;
    }

    // Move accessed entry to front of LRU list
    m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
    return it->second->second;
}

void LruTileCache::insert(const core::TileKey& key, std::shared_ptr<core::TileData> data)
{
    if (!data) {
        return;
    }

    size_t dataBytes = data->byteSize();
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // If key already exists, remove the old entry first
    auto existing = m_map.find(key);
    if (existing != m_map.end()) {
        size_t oldBytes = existing->second->second->byteSize();
        m_currentBytes -= oldBytes;
        m_lruList.erase(existing->second);
        m_map.erase(existing);
    }

    // Evict entries from the back until we have room
    while (m_currentBytes + dataBytes > m_budgetBytes && !m_lruList.empty()) {
        evictLeastRecentlyUsed();
    }

    // Insert at front
    m_lruList.emplace_front(key, data);
    m_map[key] = m_lruList.begin();
    m_currentBytes += dataBytes;

    spdlog::trace("LruTileCache: inserted {} ({} bytes), total {} bytes, {} entries",
                  key.toString(), dataBytes, m_currentBytes, m_map.size());
}

void LruTileCache::evict(const core::TileKey& key)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_map.find(key);
    if (it == m_map.end()) {
        return;
    }

    size_t entryBytes = it->second->second->byteSize();
    m_lruList.erase(it->second);
    m_map.erase(it);
    m_currentBytes -= entryBytes;

    spdlog::trace("LruTileCache: evicted {} ({} bytes), total {} bytes", key.toString(), entryBytes, m_currentBytes);
}

size_t LruTileCache::evictLevel(int level)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    size_t evicted = 0;
    size_t freedBytes = 0;
    for (auto it = m_lruList.begin(); it != m_lruList.end(); ) {
        if (it->first.level() == level) {
            size_t entryBytes = it->second->byteSize();
            m_map.erase(it->first);
            m_currentBytes -= entryBytes;
            freedBytes += entryBytes;
            it = m_lruList.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }

    if (evicted > 0) {
        spdlog::info("LruTileCache: evicted {} entries at level {} ({} bytes)",
                     evicted, level, freedBytes);
    }
    return evicted;
}

void LruTileCache::clear()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    size_t oldSize = m_map.size();
    size_t oldBytes = m_currentBytes;

    m_lruList.clear();
    m_map.clear();
    m_currentBytes = 0;

    spdlog::info("LruTileCache: cleared {} entries, freed {} bytes", oldSize, oldBytes);
}

size_t LruTileCache::currentBytes() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_currentBytes;
}

size_t LruTileCache::budgetBytes() const
{
    // Budget is immutable after construction, no lock needed
    return m_budgetBytes;
}

void LruTileCache::evictLeastRecentlyUsed()
{
    // Caller must hold unique lock
    if (m_lruList.empty()) {
        return;
    }

    auto& back = m_lruList.back();
    size_t entryBytes = back.second->byteSize();
    m_map.erase(back.first);
    m_currentBytes -= entryBytes;
    m_lruList.pop_back();
}

} // namespace slideio::viewer::infra
