#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "slideio/viewer/core/LevelUnreliableRegistry.h"

using namespace slideio::viewer::core;

TEST_CASE("LevelUnreliableRegistry notifies a listener registered first",
          "[core][LevelUnreliableRegistry]")
{
    LevelUnreliableRegistry registry;
    std::vector<int> seen;
    registry.addListener([&seen](int level) { seen.push_back(level); });

    REQUIRE(registry.mark(2));

    REQUIRE(seen == std::vector<int>{2});
}

TEST_CASE("LevelUnreliableRegistry replays already-marked levels to a late listener",
          "[core][LevelUnreliableRegistry]")
{
    // This is the case the tile scheduler is in: levels can be found unreliable
    // during the coarse-level pass, before the scheduler exists to register its
    // cache-eviction listener. Without the replay it would never learn of them.
    LevelUnreliableRegistry registry;
    REQUIRE(registry.mark(0));
    REQUIRE(registry.mark(3));

    std::vector<int> seen;
    registry.addListener([&seen](int level) { seen.push_back(level); });

    std::sort(seen.begin(), seen.end());
    REQUIRE(seen == std::vector<int>{0, 3});
}

TEST_CASE("LevelUnreliableRegistry tells each listener about a level exactly once",
          "[core][LevelUnreliableRegistry]")
{
    LevelUnreliableRegistry registry;
    REQUIRE(registry.mark(1));

    int calls = 0;
    registry.addListener([&calls](int) { ++calls; });
    REQUIRE(calls == 1); // the replay

    // Re-marking a known level is not news: no listener hears about it again.
    REQUIRE_FALSE(registry.mark(1));
    REQUIRE(calls == 1);
}

TEST_CASE("LevelUnreliableRegistry notifies every registered listener",
          "[core][LevelUnreliableRegistry]")
{
    LevelUnreliableRegistry registry;
    int first = 0;
    int second = 0;
    registry.addListener([&first](int) { ++first; });
    registry.addListener([&second](int) { ++second; });

    REQUIRE(registry.mark(4));

    REQUIRE(first == 1);
    REQUIRE(second == 1);
}

TEST_CASE("LevelUnreliableRegistry reports whether a level is unreliable",
          "[core][LevelUnreliableRegistry]")
{
    LevelUnreliableRegistry registry;
    REQUIRE_FALSE(registry.isUnreliable(0));
    registry.mark(0);
    REQUIRE(registry.isUnreliable(0));
    REQUIRE_FALSE(registry.isUnreliable(1));
}

TEST_CASE("LevelUnreliableRegistry marks a level once under concurrent readers",
          "[core][LevelUnreliableRegistry]")
{
    // Every reader thread can discover the same broken level at the same moment
    // now that one adapter serves all of them. Exactly one mark() must win, so
    // the cache is evicted once rather than once per thread.
    LevelUnreliableRegistry registry;
    std::atomic<int> notifications{0};
    registry.addListener([&notifications](int) { notifications.fetch_add(1); });

    constexpr int kThreads = 8;
    std::atomic<int> winners{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&registry, &winners] {
            if (registry.mark(7)) {
                winners.fetch_add(1);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(winners.load() == 1);
    REQUIRE(notifications.load() == 1);
}

TEST_CASE("Separate registries keep separate ledgers for the raw and managed read paths",
          "[core][LevelUnreliableRegistry]")
{
    // SlideIOAdapter holds one registry per read path. The two scenes read the
    // same file, but a managed read goes through the transform layer on top of
    // it, so a failure on one path says nothing about the other. A single
    // shared ledger let either path condemn a level for both -- and since the
    // registry is never cleared, a condemned level 0 has no finer level to fall
    // back to and would stay blank for the rest of the session in both modes.
    LevelUnreliableRegistry rawPath;
    LevelUnreliableRegistry managedPath;

    REQUIRE(managedPath.mark(0));
    REQUIRE(managedPath.isUnreliable(0));
    REQUIRE_FALSE(rawPath.isUnreliable(0));

    // And the other direction: the path that actually failed is the only one
    // that learns about it.
    REQUIRE(rawPath.mark(2));
    REQUIRE(rawPath.isUnreliable(2));
    REQUIRE_FALSE(managedPath.isUnreliable(2));

    // Marking the same level on the second path is a first mark there, not a
    // repeat: a corrupt tile-offset table fails both paths and must be marked
    // on each as it is met.
    REQUIRE(managedPath.mark(2));
    REQUIRE(managedPath.isUnreliable(2));
}

TEST_CASE("One listener on both path registries hears from each of them",
          "[core][LevelUnreliableRegistry]")
{
    // Why SlideIOAdapter::addOnLevelMarkedUnreliable de-duplicates: a listener
    // cares about the user's experience of the slide, not about which scene hit
    // the bad level, so it is registered on both ledgers. Each registry keeps
    // its own exactly-once promise, which means a level marked on both paths
    // would reach the listener twice without the adapter's own de-duplication.
    LevelUnreliableRegistry rawPath;
    LevelUnreliableRegistry managedPath;

    std::vector<int> heard;
    auto listener = [&heard](int level) { heard.push_back(level); };
    rawPath.addListener(listener);
    managedPath.addListener(listener);

    rawPath.mark(3);
    REQUIRE(heard == std::vector<int>{3});

    managedPath.mark(3);
    REQUIRE(heard == std::vector<int>{3, 3});
}
