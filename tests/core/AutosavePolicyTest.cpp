#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AutosavePolicy.h"

#include <chrono>

using namespace slideio::viewer::core;
using namespace std::chrono_literals;

namespace
{

constexpr auto kDebounce = 2000ms;
constexpr auto kBackstop = 30000ms;

std::chrono::steady_clock::time_point base()
{
    return std::chrono::steady_clock::time_point{} + 1h;
}

} // namespace

TEST_CASE("a clean model is never saved", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(false, t0, t0, t0 + 60s, kDebounce, kBackstop));
}

TEST_CASE("a dirty model waits out the debounce", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    // Still being edited: a save now would write mid-drag and churn the disk.
    REQUIRE_FALSE(shouldAutosave(true, t0 + 1900ms, t0, t0 + 1900ms, kDebounce, kBackstop));
}

TEST_CASE("a dirty model saves once the debounce elapses", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE(shouldAutosave(true, t0, t0, t0 + 2000ms, kDebounce, kBackstop));
}

TEST_CASE("the debounce boundary is inclusive", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(true, t0, t0, t0 + 1999ms, kDebounce, kBackstop));
    REQUIRE(shouldAutosave(true, t0, t0, t0 + 2000ms, kDebounce, kBackstop));
}

TEST_CASE("continuous editing still saves at the backstop interval",
          "[core][AutosavePolicy]")
{
    // Someone dragging annotations without pause for half a minute: the
    // debounce never elapses, so without the backstop nothing would ever be
    // written. This is the case the backstop exists for.
    const auto t0 = base();
    const auto now = t0 + 30s;
    REQUIRE(shouldAutosave(true, now - 100ms, t0, now, kDebounce, kBackstop));
}

TEST_CASE("the backstop is measured from the last save, not the last mutation",
          "[core][AutosavePolicy]")
{
    const auto t0 = base();
    const auto lastSave = t0 + 25s;
    const auto now = t0 + 40s;   // 40s since t0, but only 15s since the save
    REQUIRE_FALSE(shouldAutosave(true, now - 100ms, lastSave, now, kDebounce, kBackstop));
}

TEST_CASE("a clean model is not saved even past the backstop", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(false, t0, t0, t0 + 10min, kDebounce, kBackstop));
}

TEST_CASE("a clock that has not advanced never triggers a save", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(true, t0, t0, t0, kDebounce, kBackstop));
}
