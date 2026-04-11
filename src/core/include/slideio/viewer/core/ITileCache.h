#pragma once

#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"

#include <cstddef>
#include <memory>

namespace slideio::viewer::core
{

class ITileCache
{
public:
    virtual ~ITileCache() = default;
    virtual std::shared_ptr<TileData> lookup(const TileKey& key) const = 0;
    virtual void insert(const TileKey& key, std::shared_ptr<TileData> data) = 0;
    virtual void evict(const TileKey& key) = 0;
    virtual void clear() = 0;
    virtual size_t currentBytes() const = 0;
    virtual size_t budgetBytes() const = 0;
};

} // namespace slideio::viewer::core
