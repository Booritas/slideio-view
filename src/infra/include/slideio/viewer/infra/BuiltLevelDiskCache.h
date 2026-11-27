#pragma once

#include "slideio/viewer/core/IBuiltLevelStore.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace slideio::viewer::infra
{

/// Stores built pyramid levels as one directory per key name under a folder:
/// `header.json`, `level-<i>.bin` (finest is 0) and `last-used`. Every method is
/// safe to call from a worker thread; a failure is a miss or a false return and
/// never an exception.
class BuiltLevelDiskCache : public core::IBuiltLevelStore
{
public:
    static constexpr size_t kDefaultBlockBytes = 256 * 1024;

    explicit BuiltLevelDiskCache(std::string folderUtf8, size_t blockBytes = kDefaultBlockBytes);

    const std::string& folder() const { return m_folder; }

    std::optional<std::vector<std::vector<uint8_t>>> load(const core::BuiltLevelCacheKey& key,
                                                          const std::vector<std::pair<int, int>>& levelSizes,
                                                          size_t pixelBytes) override;

    bool save(const core::BuiltLevelCacheKey& key, const core::BuiltLevelCacheDescription& description,
              const std::vector<core::BuiltLevelView>& levels, const std::atomic<bool>& cancel) override;

    /// Deletes expired entries and stale temporaries; returns how many were deleted.
    static int sweep(const std::string& folderUtf8, int retentionDays, int64_t nowSeconds);

private:
    std::string m_folder;
    size_t m_blockBytes;
};

} // namespace slideio::viewer::infra
