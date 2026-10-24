#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/CalibrationPolicy.h"

#include <optional>
#include <string>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

TEST_CASE("the badge names a manual or missing calibration", "[ui][CalibrationPolicy]")
{
    CHECK(calibrationBadgeText(core::CalibrationSource::Metadata).empty());
    CHECK(calibrationBadgeText(core::CalibrationSource::Manual) == "manual calibration");
    CHECK(calibrationBadgeText(core::CalibrationSource::None) == "uncalibrated");
}

TEST_CASE("the scale bar marks a manual calibration", "[ui][CalibrationPolicy]")
{
    CHECK(scaleBarSuffix(core::CalibrationSource::Manual) == " (manual)");
    CHECK(scaleBarSuffix(core::CalibrationSource::Metadata).empty());
    CHECK(scaleBarSuffix(core::CalibrationSource::None).empty());
}

TEST_CASE("the dialog prefills the override, then the metadata, then nothing", "[ui][CalibrationPolicy]")
{
    const CalibrationPrefill fromOverride = calibrationPrefill(core::PixelSize{0.5, 0.5}, 0.25e-6, 0.25e-6);
    REQUIRE(fromOverride.value.has_value());
    CHECK(fromOverride.value->x == 0.5);
    CHECK(fromOverride.squarePixels);

    const CalibrationPrefill fromMetadata = calibrationPrefill(std::nullopt, 0.25e-6, 0.26e-6);
    REQUIRE(fromMetadata.value.has_value());
    CHECK(fromMetadata.value->x == Catch::Approx(0.25));
    CHECK(fromMetadata.value->y == Catch::Approx(0.26));
    CHECK_FALSE(fromMetadata.squarePixels);

    const CalibrationPrefill empty = calibrationPrefill(std::nullopt, 0.0, 0.0);
    CHECK_FALSE(empty.value.has_value());
    CHECK(empty.squarePixels);
}

TEST_CASE("an out-of-range entry names the range", "[ui][CalibrationPolicy]")
{
    for (const auto& [x, y] : {std::pair{250.0, 250.0}, std::pair{0.0, 0.5}}) {
        const std::string message = calibrationInputError(x, y);
        CHECK(message.find("0.01") != std::string::npos);
        CHECK(message.find("100") != std::string::npos);
    }
    CHECK(calibrationInputError(0.5, 0.5).empty());
}

TEST_CASE("the scale bar label never starts outside the widget", "[ui][CalibrationPolicy]")
{
    // A short bar near the left edge with a long "(manual)" label: centring on
    // the bar alone started it at a negative x and clipped the leading digit.
    CHECK(scaleBarLabelLeft(40, 88, 200) == 0);
    // Room on both sides: centred under the bar.
    CHECK(scaleBarLabelLeft(100, 40, 200) == 80);
    // Never past the right edge either.
    CHECK(scaleBarLabelLeft(190, 40, 200) == 160);
    // Wider than the widget: start at the left, where the number is.
    CHECK(scaleBarLabelLeft(100, 260, 200) == 0);
}
