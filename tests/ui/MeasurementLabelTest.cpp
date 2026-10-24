#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/MeasurementLabel.h"

#include <QLineF>

#include <optional>
#include <string>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

namespace
{

const QSizeF kLabel(40.0, 14.0);
const core::RulerGeometry kRuler100{core::PointF{0.0, 0.0}, core::PointF{100.0, 0.0}};

/// Whether the rectangle touches the segment, sampled finely enough for these
/// sizes: a label on the line is the failure being tested.
bool touchesSegment(const QRectF& rect, QPointF a, QPointF b)
{
    for (int i = 0; i <= 200; ++i) {
        const double t = i / 200.0;
        if (rect.contains(a + (b - a) * t)) {
            return true;
        }
    }
    return false;
}

core::Calibration calibration(core::CalibrationSource source, double micronsPerPixel)
{
    return core::Calibration{core::PixelSize{micronsPerPixel, micronsPerPixel}, source};
}

} // namespace

TEST_CASE("a horizontal ruler's label sits above its midpoint, either way it was drawn", "[ui][MeasurementLabel]")
{
    const QRectF forward = rulerLabelRect(QPointF(0, 100), QPointF(100, 100), kLabel);
    CHECK(forward.center().x() == Catch::Approx(50.0));
    CHECK(forward.center().y() == Catch::Approx(100.0 - kMeasurementLabelOffsetScreenPixels - 7.0));
    const QRectF backward = rulerLabelRect(QPointF(100, 100), QPointF(0, 100), kLabel);
    CHECK(backward == forward);
}

TEST_CASE("a vertical ruler's label sits to its right", "[ui][MeasurementLabel]")
{
    const QRectF rect = rulerLabelRect(QPointF(100, 0), QPointF(100, 100), kLabel);
    CHECK(rect.center().x() == Catch::Approx(100.0 + kMeasurementLabelOffsetScreenPixels + 20.0));
    CHECK(rect.center().y() == Catch::Approx(50.0));
    CHECK(rulerLabelRect(QPointF(100, 100), QPointF(100, 0), kLabel) == rect);
}

TEST_CASE("a diagonal ruler's label never covers the line", "[ui][MeasurementLabel]")
{
    for (const QLineF& line : {QLineF(0, 0, 100, 100), QLineF(0, 100, 100, 0), QLineF(100, 100, 0, 0),
                               QLineF(10, 0, 30, 200), QLineF(0, 0, 200, 15)}) {
        const QRectF rect = rulerLabelRect(line.p1(), line.p2(), kLabel);
        CHECK_FALSE(touchesSegment(rect, line.p1(), line.p2()));
    }
}

TEST_CASE("an angle's label sits outside the angle at its vertex", "[ui][MeasurementLabel]")
{
    // Rays along +x and +y from (100, 100): the inside is down-right, so the
    // label is up-left.
    const QRectF right = angleLabelRect(QPointF(200, 100), QPointF(100, 100), QPointF(100, 200), kLabel);
    CHECK(right.center().x() < 100.0);
    CHECK(right.center().y() < 100.0);
    CHECK_FALSE(touchesSegment(right, QPointF(100, 100), QPointF(200, 100)));
    CHECK_FALSE(touchesSegment(right, QPointF(100, 100), QPointF(100, 200)));

    // A straight angle has no inside: the label goes above.
    const QRectF straight = angleLabelRect(QPointF(200, 100), QPointF(100, 100), QPointF(0, 100), kLabel);
    CHECK(straight.center().y() < 100.0);
    CHECK_FALSE(touchesSegment(straight, QPointF(0, 100), QPointF(200, 100)));
}

TEST_CASE("a ruler's label reads the calibrated length and says where it came from", "[ui][MeasurementLabel]")
{
    const std::string metadata = rulerLabelText(kRuler100, calibration(core::CalibrationSource::Metadata, 0.5));
    CHECK(metadata == core::formatLength(50e-6, core::MeasurementUnits::Physical));
    CHECK(rulerLabelText(kRuler100, calibration(core::CalibrationSource::Manual, 0.5)) == metadata + " (manual)");
    CHECK(rulerLabelText(kRuler100, core::Calibration{}) == core::formatLength(100.0, core::MeasurementUnits::Pixels));
    CHECK(measurementLabelWarns(core::CalibrationSource::None));
    CHECK_FALSE(measurementLabelWarns(core::CalibrationSource::Manual));
    CHECK_FALSE(measurementLabelWarns(core::CalibrationSource::Metadata));
}

TEST_CASE("an angle's label is degrees to one decimal, or nothing", "[ui][MeasurementLabel]")
{
    CHECK(angleLabelText(47.25, calibration(core::CalibrationSource::Metadata, 0.5)) == "47.3\xC2\xB0");
    CHECK(angleLabelText(47.25, calibration(core::CalibrationSource::Manual, 0.5)) == "47.3\xC2\xB0 (manual)");
    CHECK(angleLabelText(90.0, core::Calibration{}) == "90.0\xC2\xB0");
    CHECK(angleLabelText(std::nullopt, core::Calibration{}).empty());
}

TEST_CASE("a ruler's label follows both pixel axes", "[ui][MeasurementLabel]")
{
    // A vertical ruler on 0.5 x 2 um/px pixels is 100 px x 2 um = 200 um.
    const core::Calibration nonSquare{core::PixelSize{0.5, 2.0}, core::CalibrationSource::Manual};
    const std::string text =
        rulerLabelText(core::RulerGeometry{core::PointF{0.0, 0.0}, core::PointF{0.0, 100.0}}, nonSquare);
    CHECK(text == core::formatLength(200e-6, core::MeasurementUnits::Physical) + " (manual)");
}
