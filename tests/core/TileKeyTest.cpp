#include <catch2/catch_test_macros.hpp>
#include <unordered_map>

#include "slideio/viewer/core/TileKey.h"

using namespace slideio::viewer::core;

TEST_CASE("TileKey equality", "[core][TileKey]")
{
    TileKey a{0, 1, 2};
    TileKey b{0, 1, 2};
    TileKey c{0, 1, 3};

    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE(a != c);
}

TEST_CASE("TileKey hashing", "[core][TileKey]")
{
    std::unordered_map<TileKey, int> map;
    map[TileKey{0, 0, 0}] = 1;
    map[TileKey{1, 2, 3}] = 2;
    map[TileKey{0, 0, 0}] = 3; // overwrite

    REQUIRE(map.size() == 2);
    REQUIRE(map[TileKey{0, 0, 0}] == 3);
    REQUIRE(map[TileKey{1, 2, 3}] == 2);
}

TEST_CASE("TileKey different levels hash differently", "[core][TileKey]")
{
    std::hash<TileKey> hasher;
    // Same col/row but different levels should (very likely) hash differently
    REQUIRE(hasher(TileKey{0, 1, 1}) != hasher(TileKey{1, 1, 1}));
}

TEST_CASE("TileKey toString", "[core][TileKey]")
{
    TileKey key{2, 10, 5};
    auto str = key.toString();
    REQUIRE(str.find("2") != std::string::npos);
    REQUIRE(str.find("10") != std::string::npos);
    REQUIRE(str.find("5") != std::string::npos);
}

TEST_CASE("TileKey with Z and T", "[core][TileKey]")
{
    TileKey a{0, 1, 2, 3, 4};
    TileKey b{0, 1, 2, 3, 4};
    TileKey c{0, 1, 2, 5, 4}; // different Z

    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE(a.zIndex() == 3);
    REQUIRE(a.tFrame() == 4);
}

TEST_CASE("TileKey Z/T default to zero", "[core][TileKey]")
{
    TileKey a{0, 1, 2};
    REQUIRE(a.zIndex() == 0);
    REQUIRE(a.tFrame() == 0);

    // Same as explicitly providing 0,0
    TileKey b{0, 1, 2, 0, 0};
    REQUIRE(a == b);
}

TEST_CASE("TileKey Z/T hashing distinguishes", "[core][TileKey]")
{
    std::unordered_map<TileKey, int> map;
    map[TileKey{0, 0, 0, 0, 0}] = 1;
    map[TileKey{0, 0, 0, 1, 0}] = 2; // different Z
    map[TileKey{0, 0, 0, 0, 1}] = 3; // different T

    REQUIRE(map.size() == 3);
    REQUIRE(map[TileKey{0, 0, 0, 0, 0}] == 1);
    REQUIRE(map[TileKey{0, 0, 0, 1, 0}] == 2);
    REQUIRE(map[TileKey{0, 0, 0, 0, 1}] == 3);
}

TEST_CASE("TileKey defaults to the raw colour mode", "[core][TileKey]")
{
    TileKey key(2, 3, 4);
    REQUIRE(key.colorMode() == ColorMode::Raw);
}

TEST_CASE("TileKeys differing only in colour mode are not equal", "[core][TileKey]")
{
    // This inequality is what keeps a tile read in the old mode from being
    // served after the user toggles: the two renditions are separate cache
    // entries, so a late raw read lands under a key managed rendering never
    // looks up.
    TileKey raw(1, 2, 3, 0, 0, ColorMode::Raw);
    TileKey managed(1, 2, 3, 0, 0, ColorMode::Managed);
    REQUIRE(raw != managed);
    REQUIRE_FALSE(raw == managed);
}

TEST_CASE("TileKeys agreeing on colour mode and coordinates are equal", "[core][TileKey]")
{
    TileKey a(1, 2, 3, 4, 5, ColorMode::Managed);
    TileKey b(1, 2, 3, 4, 5, ColorMode::Managed);
    REQUIRE(a == b);
    REQUIRE(std::hash<TileKey>{}(a) == std::hash<TileKey>{}(b));
}

TEST_CASE("TileKeys differing only in colour mode hash apart", "[core][TileKey]")
{
    TileKey raw(7, 8, 9, 0, 0, ColorMode::Raw);
    TileKey managed(7, 8, 9, 0, 0, ColorMode::Managed);
    REQUIRE(std::hash<TileKey>{}(raw) != std::hash<TileKey>{}(managed));
}

TEST_CASE("TileKey::toString names the colour mode only when managed", "[core][TileKey]")
{
    // Raw is the overwhelmingly common case; naming it on every log line
    // would be noise. Managed is the one worth seeing.
    REQUIRE(TileKey(1, 2, 3).toString().find("managed") == std::string::npos);
    REQUIRE(TileKey(1, 2, 3, 0, 0, ColorMode::Managed).toString().find("managed")
            != std::string::npos);
}
