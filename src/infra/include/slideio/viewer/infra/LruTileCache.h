#pragma once

#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/TileKey.h"

#include <cstddef>
#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace slideio::viewer::infra
{

class LruTileCache : public core::ITileCache
{
public:
    static constexpr size_t kDefaultBudgetBytes = 2ULL * 1024 * 1024 * 1024;

    explicit LruTileCache(size_t budgetBytes = kDefaultBudgetBytes);
    ~LruTileCache() override;

    LruTileCache(const LruTileCache&) = delete;
    LruTileCache& operator=(const LruTileCache&) = delete;

    std::shared_ptr<core::TileData> lookup(const core::TileKey& key) const override;
    void insert(const core::TileKey& key, std::shared_ptr<core::TileData> data) override;
    void evict(const core::TileKey& key) override;
    void clear() override;
    size_t currentBytes() const override;
    size_t budgetBytes() const override;

private:
    using Entry = std::pair<core::TileKey, std::shared_ptr<core::TileData>>;
    using ListIterator = std::list<Entry>::iterator;

    void evictLeastRecentlyUsed();

    size_t m_budgetBytes;
    size_t m_currentBytes;
    mutable std::list<Entry> m_lruList;
    mutable std::unordered_map<core::TileKey, ListIterator> m_map;
    mutable std::shared_mutex m_mutex;
};

} // namespace slideio::viewer::infra
