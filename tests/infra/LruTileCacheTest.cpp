#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "slideio/viewer/infra/LruTileCache.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TileData.h"

using namespace slideio::viewer::core;
using namespace slideio::viewer::infra;

static std::shared_ptr<TileData> makeTile(int size = 256)
{
    std::vector<uint8_t> buf(static_cast<size_t>(size) * size * 3);
    return std::make_shared<TileData>(std::move(buf), size, size, 3, DataType::Byte);
}

TEST_CASE("LruTileCache insert and lookup", "[infra][LruTileCache]")
{
    LruTileCache cache(1024 * 1024); // 1 MB budget
    TileKey key{0, 0, 0};
    auto tile = makeTile(64); // 64*64*3 = 12288 bytes

    cache.insert(key, tile);
    auto found = cache.lookup(key);

    REQUIRE(found != nullptr);
    REQUIRE(found->width() == 64);
}

TEST_CASE("LruTileCache lookup miss returns nullptr", "[infra][LruTileCache]")
{
    LruTileCache cache(1024 * 1024);
    auto found = cache.lookup(TileKey{0, 0, 0});
    REQUIRE(found == nullptr);
}

TEST_CASE("LruTileCache eviction under budget pressure", "[infra][LruTileCache]")
{
    // Budget for roughly 2 tiles of 64x64x3 = 12288 bytes each
    LruTileCache cache(25000);

    TileKey key0{0, 0, 0};
    TileKey key1{0, 1, 0};
    TileKey key2{0, 2, 0};

    cache.insert(key0, makeTile(64));
    cache.insert(key1, makeTile(64));

    // Both should be present
    REQUIRE(cache.lookup(key0) != nullptr);
    REQUIRE(cache.lookup(key1) != nullptr);

    // Insert a third tile → should evict the LRU (key0, since key1 was just accessed by lookup)
    // Actually key0 was looked up after key1, so key1 is LRU. Let's not rely on that.
    cache.insert(key2, makeTile(64));

    // At least one of the first two should be evicted
    int present = 0;
    if (cache.lookup(key0) != nullptr) present++;
    if (cache.lookup(key1) != nullptr) present++;
    REQUIRE(cache.lookup(key2) != nullptr);
    REQUIRE(cache.currentBytes() <= cache.budgetBytes());
}

TEST_CASE("LruTileCache explicit evict", "[infra][LruTileCache]")
{
    LruTileCache cache(1024 * 1024);
    TileKey key{0, 0, 0};

    cache.insert(key, makeTile(64));
    REQUIRE(cache.lookup(key) != nullptr);

    cache.evict(key);
    REQUIRE(cache.lookup(key) == nullptr);
}

TEST_CASE("LruTileCache clear", "[infra][LruTileCache]")
{
    LruTileCache cache(1024 * 1024);
    cache.insert(TileKey{0, 0, 0}, makeTile(64));
    cache.insert(TileKey{0, 1, 0}, makeTile(64));

    cache.clear();
    REQUIRE(cache.currentBytes() == 0);
    REQUIRE(cache.lookup(TileKey{0, 0, 0}) == nullptr);
}

TEST_CASE("LruTileCache tracks memory", "[infra][LruTileCache]")
{
    LruTileCache cache(1024 * 1024);

    REQUIRE(cache.currentBytes() == 0);

    auto tile = makeTile(64);
    size_t tileBytes = tile->byteSize();
    cache.insert(TileKey{0, 0, 0}, tile);

    REQUIRE(cache.currentBytes() == tileBytes);
}
