#pragma once

#include "slideio/viewer/core/BuiltLevelCache.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace slideio::viewer::core
{

struct BuiltLevelView
{
    int width = 0;
    int height = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

class IBuiltLevelStore
{
public:
    virtual ~IBuiltLevelStore() = default;

    /// Finest first. levelSizes are the expected {width, height}; each raster
    /// must be exactly width * height * pixelBytes. nullopt on any miss (logged
    /// once by the store). Marks the entry used. Polls `cancel` between blocks:
    /// a cancelled load is nullopt, leaves the entry as it was and logs nothing
    /// above debug.
    virtual std::optional<std::vector<std::vector<uint8_t>>> load(
        const BuiltLevelCacheKey& key, const std::vector<std::pair<int, int>>& levelSizes, size_t pixelBytes,
        const std::atomic<bool>& cancel) = 0;

    /// Returns true when an entry now exists for the key. Polls `cancel`
    /// between blocks.
    virtual bool save(const BuiltLevelCacheKey& key, const BuiltLevelCacheDescription& description,
                      const std::vector<BuiltLevelView>& levels, const std::atomic<bool>& cancel) = 0;
};

} // namespace slideio::viewer::core
