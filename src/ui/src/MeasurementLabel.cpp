#include "slideio/viewer/ui/MeasurementLabel.h"

#include "slideio/viewer/core/AnnotationMeasurement.h"
#include "slideio/viewer/ui/CalibrationPolicy.h"

#include <QString>

#include <cmath>

namespace slideio::viewer::ui
{

namespace
{

/// Below this, a direction is taken as no direction at all.
constexpr double kTiny = 1e-9;

QPointF unit(QPointF v)
{
    const double length = std::hypot(v.x(), v.y());
    return length < kTiny ? QPointF() : v / length;
}

QRectF centredAt(QPointF centre, QSizeF size)
{
    return QRectF(centre.x() - size.width() / 2.0, centre.y() - size.height() / 2.0, size.width(), size.height());
}

} // namespace

QRectF rulerLabelRect(QPointF startScreen, QPointF endScreen, QSizeF labelSize)
{
    const QPointF midpoint = (startScreen + endScreen) / 2.0;
    const QPointF direction = unit(endScreen - startScreen);
    QPointF normal(-direction.y(), direction.x());
    // Above, or right for a vertical line -- independent of drawing direction.
    if (std::abs(normal.y()) > kTiny) {
        if (normal.y() > 0.0) {
            normal = -normal;
        }
    } else if (normal.x() < 0.0) {
        normal = -normal;
    }
    if (std::hypot(normal.x(), normal.y()) < kTiny) {
        normal = QPointF(0.0, -1.0);  // a zero-length ruler: above its point
    }
    // The label's half-extent along the normal, so its nearest edge -- not its
    // centre -- is the offset away from the line.
    const double extent =
        std::abs(normal.x()) * labelSize.width() / 2.0 + std::abs(normal.y()) * labelSize.height() / 2.0;
    return centredAt(midpoint + normal * (kMeasurementLabelOffsetScreenPixels + extent), labelSize);
}

QRectF angleLabelRect(QPointF firstScreen, QPointF vertexScreen, QPointF secondScreen, QSizeF labelSize)
{
    const QPointF first = unit(firstScreen - vertexScreen);
    const QPointF second = unit(secondScreen - vertexScreen);
    QPointF outward = -unit(first + second);
    if (std::hypot(outward.x(), outward.y()) < kTiny) {
        // A straight (or degenerate) angle has no inside: above the vertex,
        // perpendicular to the first ray when there is one.
        outward = std::hypot(first.x(), first.y()) < kTiny ? QPointF(0.0, -1.0) : QPointF(-first.y(), first.x());
        if (outward.y() > 0.0) {
            outward = -outward;
        }
    }
    const double halfDiagonal = std::hypot(labelSize.width(), labelSize.height()) / 2.0;
    return centredAt(vertexScreen + outward * (kMeasurementLabelOffsetScreenPixels + halfDiagonal), labelSize);
}

std::string rulerLabelText(const core::RulerGeometry& ruler, const core::Calibration& calibration)
{
    if (calibration.source == core::CalibrationSource::None) {
        const double pixels = core::measure(ruler, core::PixelSize{1.0, 1.0}).outlineLength;
        return core::formatLength(pixels, core::MeasurementUnits::Pixels);
    }
    // core::measure works in metres, as the properties panel feeds it.
    const core::PixelSize metres{calibration.micronsPerPixel.x * 1e-6, calibration.micronsPerPixel.y * 1e-6};
    return core::formatLength(core::measure(ruler, metres).outlineLength, core::MeasurementUnits::Physical)
           + scaleBarSuffix(calibration.source);
}

std::string angleLabelText(std::optional<double> degrees, const core::Calibration& calibration)
{
    if (!degrees.has_value() || !std::isfinite(*degrees)) {
        return {};
    }
    // \xC2\xB0 is the degree sign: MSVC compiles without /utf-8.
    return QString::number(*degrees, 'f', 1).toStdString() + "\xC2\xB0" + scaleBarSuffix(calibration.source);
}

bool measurementLabelWarns(core::CalibrationSource source)
{
    return source == core::CalibrationSource::None;
}

} // namespace slideio::viewer::ui
