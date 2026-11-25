#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/ResultsPanelText.h"

#include <cstdint>
#include <limits>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

TEST_CASE("groupedCount separates thousands with a space", "[ResultsPanelText]")
{
    CHECK(groupedCount(0) == "0");
    CHECK(groupedCount(7) == "7");
    CHECK(groupedCount(999) == "999");
    CHECK(groupedCount(1000) == "1 000");
    CHECK(groupedCount(12345) == "12 345");
    CHECK(groupedCount(48211) == "48 211");
    CHECK(groupedCount(1000000) == "1 000 000");
    CHECK(groupedCount(-12345) == "-12 345");
    CHECK(groupedCount(std::numeric_limits<int64_t>::min()) == "-9 223 372 036 854 775 808");
}

TEST_CASE("inViewText is the over-budget line", "[ResultsPanelText]")
{
    CHECK(inViewText(12345) == "12 345 in view \xE2\x80\x94 overview; zoom in for outlines");
    CHECK(inViewText(20001) == "20 001 in view \xE2\x80\x94 overview; zoom in for outlines");
    CHECK(inViewText(0) == "0 in view \xE2\x80\x94 overview; zoom in for outlines");
}

TEST_CASE("buildingText shows an integer percentage", "[ResultsPanelText]")
{
    CHECK(buildingText(0.43) == "building overview\xE2\x80\xA6 43 %");
    CHECK(buildingText(0.0) == "building overview\xE2\x80\xA6 0 %");
    CHECK(buildingText(1.0) == "building overview\xE2\x80\xA6 100 %");
    // Never claims 100 % before the scan has finished.
    CHECK(buildingText(0.999) == "building overview\xE2\x80\xA6 99 %");
    CHECK(buildingText(-0.5) == "building overview\xE2\x80\xA6 0 %");
    CHECK(buildingText(2.0) == "building overview\xE2\x80\xA6 100 %");
    CHECK(buildingText(std::numeric_limits<double>::quiet_NaN()) == "building overview\xE2\x80\xA6 0 %");
}

TEST_CASE("measurementLine is name, value and unit", "[ResultsPanelText]")
{
    core::ResultMeasurement area{"area", 41.0, "px\xC2\xB2", ""};
    CHECK(measurementLine(area) == "area  41.0 px\xC2\xB2");

    core::ResultMeasurement mean{"intensity.0.mean", 143.66, "", ""};
    CHECK(measurementLine(mean) == "intensity.0.mean  143.7");

    core::ResultMeasurement asserted{"area", 41.0, "px\xC2\xB2", "classify"};
    CHECK(measurementLine(asserted) == "area  41.0 px\xC2\xB2 (classify)");

    core::ResultMeasurement small{"ratio", 0.00345, "", ""};
    CHECK(measurementLine(small) == "ratio  0.00345");

    core::ResultMeasurement zero{"count", 0.0, "", ""};
    CHECK(measurementLine(zero) == "count  0.0");

    core::ResultMeasurement negative{"offset", -2.26, "px", ""};
    CHECK(measurementLine(negative) == "offset  -2.3 px");
}

TEST_CASE("stageStateBadge names only stale and failed", "[ResultsPanelText]")
{
    CHECK(stageStateBadge("").empty());
    CHECK(stageStateBadge("stale") == "stale");
    CHECK(stageStateBadge("failed") == "failed");
    CHECK(stageStateBadge("something else").empty());
}
