#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/LogSettings.h"

#include <spdlog/common.h>

#include <optional>
#include <string>

using namespace slideio::viewer::ui;

TEST_CASE("level string round-trips for all exposed levels", "[ui][LogSettings]")
{
    const spdlog::level::level_enum levels[] = {
        spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
        spdlog::level::warn,  spdlog::level::err,   spdlog::level::off};
    for (auto lvl : levels) {
        REQUIRE(levelFromString(levelToString(lvl)) == lvl);
    }
}

TEST_CASE("unknown or empty level string defaults to debug", "[ui][LogSettings]")
{
    REQUIRE(levelFromString("") == spdlog::level::debug);
    REQUIRE(levelFromString("bogus") == spdlog::level::debug);
}

TEST_CASE("warning/error aliases parse", "[ui][LogSettings]")
{
    REQUIRE(levelFromString("warning") == spdlog::level::warn);
    REQUIRE(levelFromString("err") == spdlog::level::err);
}

TEST_CASE("startup perf resolution: env wins when set and non-empty", "[ui][LogSettings]")
{
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{"1"}, false) == true);
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{"0"}, true) == false);
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{"yes"}, false) == true);
}

TEST_CASE("startup perf resolution: falls back to setting when env unset/empty", "[ui][LogSettings]")
{
    REQUIRE(resolveStartupPerfEnabled(std::nullopt, true) == true);
    REQUIRE(resolveStartupPerfEnabled(std::nullopt, false) == false);
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{""}, true) == true);
}
