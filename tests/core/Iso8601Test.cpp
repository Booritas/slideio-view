#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Iso8601.h"

#include <chrono>
#include <string>

using namespace slideio::viewer::core;

namespace
{

/// The epoch plus a fixed offset, so the expected string is arithmetic rather
/// than a second implementation of the formatter.
std::chrono::system_clock::time_point epochPlus(long long seconds)
{
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(seconds));
}

} // namespace

TEST_CASE("the epoch formats as the ISO 8601 epoch", "[core][Iso8601]")
{
    REQUIRE(formatIso8601Utc(epochPlus(0)) == "1970-01-01T00:00:00Z");
}

TEST_CASE("a known instant formats with zero padding everywhere", "[core][Iso8601]")
{
    // 2026-03-05T04:07:09Z -- single-digit month, day, hour, minute and second,
    // which is where a format string without padding goes wrong.
    REQUIRE(formatIso8601Utc(epochPlus(1772683629LL)) == "2026-03-05T04:07:09Z");
}

TEST_CASE("formatting round-trips through parsing", "[core][Iso8601]")
{
    const auto original = epochPlus(1772683629LL);
    std::chrono::system_clock::time_point parsed{};
    REQUIRE(parseIso8601Utc(formatIso8601Utc(original), parsed));
    REQUIRE(parsed == original);
}

TEST_CASE("sub-second precision is truncated, not rounded", "[core][Iso8601]")
{
    const auto t = epochPlus(1772683629LL) + std::chrono::milliseconds(999);
    REQUIRE(formatIso8601Utc(t) == "2026-03-05T04:07:09Z");
}

TEST_CASE("parsing rejects everything that is not the exact format", "[core][Iso8601]")
{
    std::chrono::system_clock::time_point out{};
    REQUIRE_FALSE(parseIso8601Utc("", out));
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05T04:07:09", out));        // no Z
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05 04:07:09Z", out));       // space, not T
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05T04:07:09+01:00", out));  // offset
    REQUIRE_FALSE(parseIso8601Utc("2026-3-5T4:7:9Z", out));            // unpadded
    REQUIRE_FALSE(parseIso8601Utc("2026-13-05T04:07:09Z", out));       // month 13
    REQUIRE_FALSE(parseIso8601Utc("2026-03-32T04:07:09Z", out));       // day 32
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05T24:07:09Z", out));       // hour 24
    REQUIRE_FALSE(parseIso8601Utc("not-a-timestamp-abcZ", out));       // right length
}

TEST_CASE("a failed parse leaves the output untouched", "[core][Iso8601]")
{
    const auto sentinel = epochPlus(1234);
    auto out = sentinel;
    REQUIRE_FALSE(parseIso8601Utc("garbage", out));
    REQUIRE(out == sentinel);
}

TEST_CASE("instants before 1970 are out of range and are rejected", "[core][Iso8601]")
{
    // _mkgmtime on MSVC cannot represent them, so the format is documented as
    // starting at the epoch and the parser says no rather than inventing a value.
    std::chrono::system_clock::time_point out{};
    REQUIRE_FALSE(parseIso8601Utc("1969-12-31T23:59:59Z", out));
}
