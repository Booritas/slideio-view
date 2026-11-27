#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/BuiltLevelCache.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

BuiltLevelCacheKey baseKey()
{
    BuiltLevelCacheKey k;
    k.slideId = "0123456789abcdef";
    k.driverId = "GDAL";
    k.sceneIndex = 1;
    k.fileSize = 123456789;
    k.fileModifiedMs = 1700000000000;
    k.sourceLevel = 0;
    k.sourceWidth = 63803;
    k.sourceHeight = 44997;
    k.numChannels = 3;
    k.dataType = DataType::Byte;
    k.residentFactors = {8, 16};
    k.colorMode = ColorMode::Managed;
    k.profileFingerprint = "aaaaaaaaaaaaaaaa";
    k.renditionTag = "srgb/perceptual";
    k.slideioVersion = "2.0.0";
    return k;
}

bool isLowerHex(const std::string& s)
{
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

const std::string kKey = "0123456789abcdef0123456789abcdef";

} // namespace

TEST_CASE("builtLevelCacheKeyName is stable and 32 lowercase hex", "[BuiltLevelCache]")
{
    const std::string a = builtLevelCacheKeyName(baseKey());
    const std::string b = builtLevelCacheKeyName(baseKey());
    REQUIRE(a == b);
    REQUIRE(a.size() == 32);
    REQUIRE(isLowerHex(a));
}

TEST_CASE("builtLevelCacheKeyName changes with every key field", "[BuiltLevelCache]")
{
    const BuiltLevelCacheKey base = baseKey();
    const std::string baseName = builtLevelCacheKeyName(base);

    auto check = [&](const char* field, auto mutate) {
        INFO(field);
        BuiltLevelCacheKey k = base;
        mutate(k);
        CHECK(builtLevelCacheKeyName(k) != baseName);
        CHECK(k != base);
        CHECK(!(k == base));
    };
    check("slideId", [](auto& k) { k.slideId = "fedcba9876543210"; });
    check("driverId", [](auto& k) { k.driverId = "SVS"; });
    check("sceneIndex", [](auto& k) { k.sceneIndex = 2; });
    check("fileSize", [](auto& k) { k.fileSize += 1; });
    check("fileModifiedMs", [](auto& k) { k.fileModifiedMs += 1; });
    check("sourceLevel", [](auto& k) { k.sourceLevel = 1; });
    check("sourceWidth", [](auto& k) { k.sourceWidth += 1; });
    check("sourceHeight", [](auto& k) { k.sourceHeight += 1; });
    check("numChannels", [](auto& k) { k.numChannels = 4; });
    check("dataType", [](auto& k) { k.dataType = DataType::UInt16; });
    check("residentFactors", [](auto& k) { k.residentFactors = {8, 16, 32}; });
    check("colorMode", [](auto& k) { k.colorMode = ColorMode::Raw; });
    check("profileFingerprint", [](auto& k) { k.profileFingerprint = "bbbbbbbbbbbbbbbb"; });
    check("renditionTag", [](auto& k) { k.renditionTag = "srgb/relative"; });
    check("slideioVersion", [](auto& k) { k.slideioVersion = "2.0.1"; });
    check("formatVersion", [](auto& k) { k.formatVersion += 1; });

    CHECK(baseKey() == base);
}

TEST_CASE("builtLevelCacheKeyName does not confuse shifted string boundaries", "[BuiltLevelCache]")
{
    BuiltLevelCacheKey a = baseKey();
    BuiltLevelCacheKey b = baseKey();
    a.profileFingerprint = "ab";
    a.renditionTag = "c";
    b.profileFingerprint = "a";
    b.renditionTag = "bc";
    CHECK(builtLevelCacheKeyName(a) != builtLevelCacheKeyName(b));
}

TEST_CASE("builtLevelCacheKeyFor fills the key from the identity", "[BuiltLevelCache]")
{
    BuiltLevelCacheIdentity id;
    id.slideId = "slide";
    id.driverId = "OMETIFF";
    id.sceneIndex = 2;
    id.fileSize = 99;
    id.fileModifiedMs = 77;
    id.managedProfileFingerprint = "fp";
    id.managedRenditionTag = "tag";
    id.slideioVersion = "v";
    LevelInfo source;
    source.width = 4096;
    source.height = 3000;

    SECTION("Raw clears fingerprint and tag")
    {
        auto key = builtLevelCacheKeyFor(id, 0, source, 3, DataType::Byte, {2, 4}, ColorMode::Raw);
        REQUIRE(key.has_value());
        CHECK(key->profileFingerprint.empty());
        CHECK(key->renditionTag.empty());
        CHECK(key->colorMode == ColorMode::Raw);
        CHECK(key->slideId == "slide");
        CHECK(key->driverId == "OMETIFF");
        CHECK(key->sceneIndex == 2);
        CHECK(key->fileSize == 99);
        CHECK(key->fileModifiedMs == 77);
        CHECK(key->sourceLevel == 0);
        CHECK(key->sourceWidth == 4096);
        CHECK(key->sourceHeight == 3000);
        CHECK(key->numChannels == 3);
        CHECK(key->dataType == DataType::Byte);
        CHECK(key->residentFactors == std::vector<int>{2, 4});
        CHECK(key->slideioVersion == "v");
        CHECK(key->formatVersion == kBuiltLevelCacheFormatVersion);
    }
    SECTION("Managed without a fingerprint is not cacheable")
    {
        id.managedProfileFingerprint.clear();
        CHECK(!builtLevelCacheKeyFor(id, 0, source, 3, DataType::Byte, {2}, ColorMode::Managed).has_value());
        CHECK(builtLevelCacheKeyFor(id, 0, source, 3, DataType::Byte, {2}, ColorMode::Raw).has_value());
    }
    SECTION("Managed copies both")
    {
        auto key = builtLevelCacheKeyFor(id, 0, source, 3, DataType::Byte, {2}, ColorMode::Managed);
        REQUIRE(key.has_value());
        CHECK(key->profileFingerprint == "fp");
        CHECK(key->renditionTag == "tag");
    }
}

TEST_CASE("colorProfileFingerprint", "[BuiltLevelCache]")
{
    CHECK(colorProfileFingerprint({}).empty());
    const std::string a = colorProfileFingerprint({1, 2, 3});
    CHECK(a.size() == 16);
    CHECK(isLowerHex(a));
    CHECK(a != colorProfileFingerprint({1, 2, 4}));
    CHECK(a == colorProfileFingerprint({1, 2, 3}));
}

TEST_CASE("crc32 matches the IEEE check value", "[BuiltLevelCache]")
{
    const std::string s = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(s.data()), s.size()) == 0xCBF43926u);
    CHECK(crc32(nullptr, 0) == 0u);
}

TEST_CASE("cache name predicates", "[BuiltLevelCache]")
{
    CHECK(isCacheKeyName(kKey));
    CHECK(!isCacheKeyName(kKey.substr(1)));
    CHECK(!isCacheKeyName("0123456789ABCDEF0123456789abcdef"));
    CHECK(!isCacheKeyName("g123456789abcdef0123456789abcdef"));
    CHECK(!isCacheKeyName("notes.txt"));

    CHECK(isCacheTemporaryName(kKey + ".tmp-ab12"));
    CHECK(!isCacheTemporaryName(kKey + ".tmp-"));
    CHECK(!isCacheTemporaryName(kKey));
    CHECK(!isCacheTemporaryName("0123456789ABCDEF0123456789abcdef.tmp-x"));
    CHECK(!isCacheTemporaryName("notes.txt"));
}

TEST_CASE("expiredCacheEntries", "[BuiltLevelCache]")
{
    const int64_t now = 10000000;
    const int64_t day = 86400;
    const std::string k1 = kKey;
    const std::string k2 = "fedcba9876543210fedcba9876543210";
    const std::string k3 = "00000000000000000000000000000001";
    const std::string t1 = kKey + ".tmp-aa";
    const std::string t2 = k2 + ".tmp-bb";

    SECTION("key entries")
    {
        std::vector<CacheEntryFacts> e = {{k1, now - 30 * day}, {k2, now - 30 * day - 1}, {k3, now + 1000}};
        auto out = expiredCacheEntries(e, now, 30);
        REQUIRE(out.size() == 1);
        CHECK(out[0] == k2);
    }
    SECTION("temporaries")
    {
        std::vector<CacheEntryFacts> e = {{t1, now - day}, {t2, now - day - 1}};
        auto out = expiredCacheEntries(e, now, 30);
        REQUIRE(out.size() == 1);
        CHECK(out[0] == t2);
    }
    SECTION("other names are never returned")
    {
        std::vector<CacheEntryFacts> e = {{"notes.txt", 0}, {"", 0}};
        CHECK(expiredCacheEntries(e, now, 30).empty());
    }
}

TEST_CASE("builtLevelCacheApplies", "[BuiltLevelCache]")
{
    BuiltLevelCacheEligibility ok{true, true, "GDAL", "abc", false};
    CHECK(builtLevelCacheApplies(ok));

    auto flipped = [&](auto mutate) {
        auto f = ok;
        mutate(f);
        return builtLevelCacheApplies(f);
    };
    CHECK(!flipped([](auto& f) { f.enabled = false; }));
    CHECK(!flipped([](auto& f) { f.regularFile = false; }));
    CHECK(!flipped([](auto& f) { f.driverId = "DCM"; }));
    CHECK(!flipped([](auto& f) { f.slideId.clear(); }));
    CHECK(!flipped([](auto& f) { f.hadOpenTimeOverview = true; }));

    for (const char* d : {"GDAL", "SVS", "PHTIFF", "NDPI", "QPTIFF", "SCN", "BIF", "CZI", "ZVI", "LIF", "ND2"})
    {
        INFO(d);
        CHECK(isSingleFileDriver(d));
    }
    // An OME-TIFF can reference companion files the key does not describe.
    CHECK(!isSingleFileDriver("OMETIFF"));
    CHECK(!isSingleFileDriver("gdal"));
    CHECK(!isSingleFileDriver("DCM"));
    CHECK(!isSingleFileDriver(""));
}
