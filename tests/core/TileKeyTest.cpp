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
