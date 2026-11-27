#pragma once

#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slideio::viewer::core
{

constexpr int kBuiltLevelCacheFormatVersion = 1;
constexpr int64_t kCacheTemporaryMaxAgeSeconds = 86400;

/// Everything that decides whether a stored build may stand in for a fresh one.
/// Every field is stored in the entry's header and compared field by field on
/// load; the directory name is only a hash of them.
struct BuiltLevelCacheKey
{
    std::string slideId;
    int sceneIndex = 0;
    uint64_t fileSize = 0;
    int64_t fileModifiedMs = 0;
    int sourceLevel = -1;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int numChannels = 0;
    DataType dataType = DataType::None;
    std::vector<int> residentFactors;
    ColorMode colorMode = ColorMode::Raw;
    std::string profileFingerprint; // empty for Raw
    std::string renditionTag;       // empty for Raw
    std::string slideioVersion;
    int formatVersion = kBuiltLevelCacheFormatVersion;
};

bool operator==(const BuiltLevelCacheKey& a, const BuiltLevelCacheKey& b);
bool operator!=(const BuiltLevelCacheKey& a, const BuiltLevelCacheKey& b);

/// Everything about the slide the source cannot know: filled by ui at open.
struct BuiltLevelCacheIdentity
{
    std::string slideId;
    int sceneIndex = 0;
    uint64_t fileSize = 0;
    int64_t fileModifiedMs = 0;
    std::string managedProfileFingerprint; // empty: no managed rendition can be cached
    std::string managedRenditionTag;
    std::string slideioVersion;
};

/// Descriptive only. Never part of the key and never compared.
struct BuiltLevelCacheDescription
{
    std::string path;
    std::string fileName;
    std::string driverId;
    std::string sceneName;
    int sceneIndex = 0;
    int width = 0;
    int height = 0;
    double magnification = 0.0;
    double pixelSizeXUm = 0.0;
    double pixelSizeYUm = 0.0;
    uint64_t fileSize = 0;
    int64_t fileModifiedMs = 0;
};

/// The key for one build; nullopt for a Managed build whose identity carries no
/// profile fingerprint. A Raw key carries neither fingerprint nor rendition tag
/// even when the identity has them.
std::optional<BuiltLevelCacheKey> builtLevelCacheKeyFor(const BuiltLevelCacheIdentity& identity, int sourceLevel,
                                                        const LevelInfo& source, int numChannels, DataType dataType,
                                                        const std::vector<int>& residentFactors, ColorMode mode);

/// 32 lowercase hex: two FNV-1a 64-bit passes, from different offsets, over a
/// canonical serialisation of every key field.
std::string builtLevelCacheKeyName(const BuiltLevelCacheKey& key);

/// 16 lowercase hex FNV-1a 64 of a profile's bytes; empty for no bytes.
std::string colorProfileFingerprint(const std::vector<uint8_t>& bytes);

/// IEEE 802.3 CRC-32 (reflected, polynomial 0xEDB88320).
uint32_t crc32(const uint8_t* data, size_t size);

/// Exactly 32 characters of [0-9a-f].
bool isCacheKeyName(std::string_view name);
/// "<key name>.tmp-<one or more characters>".
bool isCacheTemporaryName(std::string_view name);

struct CacheEntryFacts
{
    std::string name;
    int64_t lastUsedSeconds = 0;
};

/// Names to delete: key names whose last use is more than retentionDays * 86400
/// seconds ago; temporaries older than kCacheTemporaryMaxAgeSeconds. A future
/// timestamp never expires. Any other name is never returned.
std::vector<std::string> expiredCacheEntries(const std::vector<CacheEntryFacts>& entries, int64_t nowSeconds,
                                             int retentionDays);

/// The allowlist of drivers whose slide is one file, exact case.
bool isSingleFileDriver(std::string_view driverId);

struct BuiltLevelCacheEligibility
{
    bool enabled = false;
    bool regularFile = false;
    std::string driverId;
    std::string slideId;
    bool hadOpenTimeOverview = false;
};

bool builtLevelCacheApplies(const BuiltLevelCacheEligibility& facts);

} // namespace slideio::viewer::core
