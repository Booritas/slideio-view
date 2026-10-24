#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Calibration.h"

#include <cstring>
#include <limits>
#include <optional>

using namespace slideio::viewer::core;

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE("metadata on both axes is the calibration", "[core][Calibration]")
{
    const Calibration c = effectiveCalibration(0.25e-6, 0.26e-6, std::nullopt);
    CHECK(c.source == CalibrationSource::Metadata);
    CHECK(c.micronsPerPixel.x == Catch::Approx(0.25));
    CHECK(c.micronsPerPixel.y == Catch::Approx(0.26));
}

TEST_CASE("a valid override wins over metadata", "[core][Calibration]")
{
    const Calibration c = effectiveCalibration(0.25e-6, 0.25e-6, PixelSize{0.5, 0.5});
    CHECK(c.source == CalibrationSource::Manual);
    CHECK(c.micronsPerPixel.x == 0.5);
    CHECK(c.micronsPerPixel.y == 0.5);
}

TEST_CASE("an override equal to the metadata is still manual", "[core][Calibration]")
{
    // The user decided; the badge must say so even when the numbers agree.
    CHECK(effectiveCalibration(0.5e-6, 0.5e-6, PixelSize{0.5, 0.5}).source == CalibrationSource::Manual);
}

TEST_CASE("one good metadata axis is not a calibration", "[core][Calibration]")
{
    for (const double y : {0.0, -1.0, kNaN, kInf}) {
        const Calibration c = effectiveCalibration(0.25e-6, y, std::nullopt);
        CHECK(c.source == CalibrationSource::None);
        CHECK(c.micronsPerPixel.x == 0.0);
        CHECK(c.micronsPerPixel.y == 0.0);
    }
}

TEST_CASE("an invalid stored override is ignored", "[core][Calibration]")
{
    const PixelSize invalid[] = {{0.0, 0.5}, {-1.0, -1.0}, {kNaN, 0.5}, {kInf, 1.0}, {100.001, 1.0}, {0.0099, 1.0}};
    for (const PixelSize& value : invalid) {
        CHECK(effectiveCalibration(0.25e-6, 0.25e-6, value).source == CalibrationSource::Metadata);
        CHECK(effectiveCalibration(0.0, 0.0, value).source == CalibrationSource::None);
    }
}

TEST_CASE("the override range is inclusive", "[core][Calibration]")
{
    CHECK(isValidCalibrationOverride(PixelSize{0.01, 100.0}));
    CHECK(isValidCalibrationOverride(PixelSize{kMinCalibrationMicronsPerPixel, kMaxCalibrationMicronsPerPixel}));
    CHECK_FALSE(isValidCalibrationOverride(PixelSize{0.01, 100.0000001}));
    CHECK_FALSE(isValidCalibrationOverride(PixelSize{0.0099999, 1.0}));
}

TEST_CASE("calibration sources have stable names", "[core][Calibration]")
{
    CHECK(std::strcmp(calibrationSourceName(CalibrationSource::Metadata), "metadata") == 0);
    CHECK(std::strcmp(calibrationSourceName(CalibrationSource::Manual), "manual") == 0);
    CHECK(std::strcmp(calibrationSourceName(CalibrationSource::None), "none") == 0);
}
