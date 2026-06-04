#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/ReadingSettings.h"

using namespace slideio::viewer::ui;

TEST_CASE("clampThreadPoolSize clamps below the minimum", "[ui][ReadingSettings]")
{
    REQUIRE(clampThreadPoolSize(0) == kMinThreadPoolSize);
    REQUIRE(clampThreadPoolSize(-5) == kMinThreadPoolSize);
}

TEST_CASE("clampThreadPoolSize clamps above the maximum", "[ui][ReadingSettings]")
{
    REQUIRE(clampThreadPoolSize(33) == kMaxThreadPoolSize);
    REQUIRE(clampThreadPoolSize(1000) == kMaxThreadPoolSize);
}

TEST_CASE("clampThreadPoolSize leaves in-range values unchanged", "[ui][ReadingSettings]")
{
    REQUIRE(clampThreadPoolSize(kMinThreadPoolSize) == kMinThreadPoolSize);
    REQUIRE(clampThreadPoolSize(4) == 4);
    REQUIRE(clampThreadPoolSize(kMaxThreadPoolSize) == kMaxThreadPoolSize);
}
