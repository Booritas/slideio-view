#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/PerfLog.h"

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

#include <memory>

using namespace slideio::viewer::infra;

TEST_CASE("perfLog returns a usable logger when none is registered", "[infra][PerfLog]")
{
    spdlog::drop("perf"); // ensure unregistered
    // Must not crash and must report disabled (fallback level is off).
    perfLog().trace("noop"); // no sink, no crash
    REQUIRE_FALSE(perfLogEnabled());
}

TEST_CASE("perfLogEnabled follows the registered logger level", "[infra][PerfLog]")
{
    spdlog::drop("perf");
    auto perf = std::make_shared<spdlog::logger>("perf"); // no sinks
    perf->set_level(spdlog::level::trace);
    spdlog::register_logger(perf);

    REQUIRE(perfLogEnabled());

    perf->set_level(spdlog::level::off);
    REQUIRE_FALSE(perfLogEnabled());

    spdlog::drop("perf"); // cleanup
}
