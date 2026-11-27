#include "slideio/viewer/core/BuiltLevelCache.h"

#include <array>

namespace slideio::viewer::core
{

namespace
{

constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr uint64_t kFnvOffsetA = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvOffsetB = 0x9ae16a3b2f90404fULL;

uint64_t fnv1a(const std::string& bytes, uint64_t hash)
{
    for (unsigned char c : bytes)
    {
        hash ^= c;
        hash *= kFnvPrime;
    }
    return hash;
}

void appendU64(std::string& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void appendInt(std::string& out, int64_t v)
{
    appendU64(out, static_cast<uint64_t>(v));
}

void appendString(std::string& out, const std::string& s)
{
    appendU64(out, s.size());
    out += s;
}

std::string hex16(uint64_t v)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[static_cast<size_t>(i)] = kDigits[v & 0xF];
        v >>= 4;
    }
    return out;
}

bool isLowerHexChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

constexpr size_t kKeyNameLength = 32;
constexpr std::string_view kTempMarker = ".tmp-";

const std::array<uint32_t, 256>& crcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

} // namespace

bool operator==(const BuiltLevelCacheKey& a, const BuiltLevelCacheKey& b)
{
    return a.slideId == b.slideId && a.driverId == b.driverId && a.sceneIndex == b.sceneIndex &&
           a.fileSize == b.fileSize && a.fileModifiedMs == b.fileModifiedMs && a.sourceLevel == b.sourceLevel &&
           a.sourceWidth == b.sourceWidth && a.sourceHeight == b.sourceHeight && a.numChannels == b.numChannels &&
           a.dataType == b.dataType && a.residentFactors == b.residentFactors && a.colorMode == b.colorMode &&
           a.profileFingerprint == b.profileFingerprint && a.renditionTag == b.renditionTag &&
           a.slideioVersion == b.slideioVersion && a.formatVersion == b.formatVersion;
}

bool operator!=(const BuiltLevelCacheKey& a, const BuiltLevelCacheKey& b)
{
    return !(a == b);
}

std::optional<BuiltLevelCacheKey> builtLevelCacheKeyFor(const BuiltLevelCacheIdentity& identity, int sourceLevel,
                                                        const LevelInfo& source, int numChannels, DataType dataType,
                                                        const std::vector<int>& residentFactors, ColorMode mode)
{
    if (mode == ColorMode::Managed && identity.managedProfileFingerprint.empty())
    {
        return std::nullopt;
    }
    BuiltLevelCacheKey key;
    key.slideId = identity.slideId;
    key.driverId = identity.driverId;
    key.sceneIndex = identity.sceneIndex;
    key.fileSize = identity.fileSize;
    key.fileModifiedMs = identity.fileModifiedMs;
    key.sourceLevel = sourceLevel;
    key.sourceWidth = source.width;
    key.sourceHeight = source.height;
    key.numChannels = numChannels;
    key.dataType = dataType;
    key.residentFactors = residentFactors;
    key.colorMode = mode;
    if (mode == ColorMode::Managed)
    {
        key.profileFingerprint = identity.managedProfileFingerprint;
        key.renditionTag = identity.managedRenditionTag;
    }
    key.slideioVersion = identity.slideioVersion;
    return key;
}

std::string builtLevelCacheKeyName(const BuiltLevelCacheKey& key)
{
    std::string s;
    appendString(s, key.slideId);
    appendString(s, key.driverId);
    appendInt(s, key.sceneIndex);
    appendU64(s, key.fileSize);
    appendInt(s, key.fileModifiedMs);
    appendInt(s, key.sourceLevel);
    appendInt(s, key.sourceWidth);
    appendInt(s, key.sourceHeight);
    appendInt(s, key.numChannels);
    appendInt(s, static_cast<int>(key.dataType));
    appendU64(s, key.residentFactors.size());
    for (int f : key.residentFactors)
    {
        appendInt(s, f);
    }
    appendInt(s, static_cast<int>(key.colorMode));
    appendString(s, key.profileFingerprint);
    appendString(s, key.renditionTag);
    appendString(s, key.slideioVersion);
    appendInt(s, key.formatVersion);
    return hex16(fnv1a(s, kFnvOffsetA)) + hex16(fnv1a(s, kFnvOffsetB));
}

std::string colorProfileFingerprint(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
    {
        return {};
    }
    uint64_t hash = kFnvOffsetA;
    for (uint8_t b : bytes)
    {
        hash ^= b;
        hash *= kFnvPrime;
    }
    return hex16(hash);
}

uint32_t crc32(const uint8_t* data, size_t size)
{
    const auto& table = crcTable();
    uint32_t c = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; ++i)
    {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

bool isCacheKeyName(std::string_view name)
{
    if (name.size() != kKeyNameLength)
    {
        return false;
    }
    for (char c : name)
    {
        if (!isLowerHexChar(c))
        {
            return false;
        }
    }
    return true;
}

bool isCacheTemporaryName(std::string_view name)
{
    const size_t prefix = kKeyNameLength + kTempMarker.size();
    if (name.size() <= prefix)
    {
        return false;
    }
    return isCacheKeyName(name.substr(0, kKeyNameLength)) && name.substr(kKeyNameLength, kTempMarker.size()) == kTempMarker;
}

std::vector<std::string> expiredCacheEntries(const std::vector<CacheEntryFacts>& entries, int64_t nowSeconds,
                                             int retentionDays)
{
    std::vector<std::string> out;
    const int64_t keyMaxAge = static_cast<int64_t>(retentionDays) * 86400;
    for (const auto& e : entries)
    {
        const int64_t age = nowSeconds - e.lastUsedSeconds;
        if (isCacheKeyName(e.name))
        {
            if (age > keyMaxAge)
            {
                out.push_back(e.name);
            }
        }
        else if (isCacheTemporaryName(e.name))
        {
            if (age > kCacheTemporaryMaxAgeSeconds)
            {
                out.push_back(e.name);
            }
        }
    }
    return out;
}

bool isSingleFileDriver(std::string_view driverId)
{
    // Not OMETIFF: its TiffData elements can name other files by
    // UUID/@FileName, and the key's size and modification time describe only
    // the file opened, so a change to a companion would serve stale levels.
    static constexpr std::string_view kDrivers[] = {"GDAL", "SVS", "PHTIFF", "NDPI", "QPTIFF", "SCN",
                                                    "BIF",  "CZI", "ZVI",    "LIF",  "ND2"};
    for (auto d : kDrivers)
    {
        if (d == driverId)
        {
            return true;
        }
    }
    return false;
}

bool builtLevelCacheApplies(const BuiltLevelCacheEligibility& facts)
{
    return facts.enabled && facts.regularFile && isSingleFileDriver(facts.driverId) && !facts.slideId.empty() &&
           !facts.hadOpenTimeOverview;
}

} // namespace slideio::viewer::core
