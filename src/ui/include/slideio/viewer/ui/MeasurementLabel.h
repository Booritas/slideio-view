#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Calibration.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <optional>
#include <string>

namespace slideio::viewer::ui
{

/// Screen pixels between a measurement's line and the nearest edge of its
/// label, so the number never sits on the thing it measures.
inline constexpr double kMeasurementLabelOffsetScreenPixels = 10.0;

/// Where a ruler's label goes, in screen pixels: beside the midpoint, pushed
/// off the line along its normal by the offset plus the label's own extent in
/// that direction. Above the line (smaller y) unless the line is vertical,
/// then to its right -- whichever way it was drawn, so the label does not jump
/// sides with the drawing direction.
[[nodiscard]] QRectF rulerLabelRect(QPointF startScreen, QPointF endScreen, QSizeF labelSize);

/// Where an angle's label goes: at the vertex, pushed OUTSIDE the angle --
/// against the bisector of its two rays -- by the offset plus half the label's
/// diagonal, which clears both rays at any opening. A straight angle has no
/// inside, and its label goes above the vertex.
[[nodiscard]] QRectF angleLabelRect(QPointF firstScreen, QPointF vertexScreen, QPointF secondScreen,
                                    QSizeF labelSize);

/// A ruler's label: its length through core::measure() -- the panel's own
/// function, so the two cannot disagree -- in physical units when calibrated
/// and pixels when not, plus " (manual)" for an override. With no calibration
/// the warning is an icon beside the label (measurementLabelWarns), not text.
[[nodiscard]] std::string rulerLabelText(const core::RulerGeometry& ruler, const core::Calibration& calibration);

/// An angle's label: degrees to one decimal, plus " (manual)" for an override.
/// Empty for an absent angle, which is then not labelled at all.
[[nodiscard]] std::string angleLabelText(std::optional<double> degrees, const core::Calibration& calibration);

/// Whether the label carries a warning icon: on a slide with no calibration,
/// where the ruler reads in pixels and an angle in pixel space.
[[nodiscard]] bool measurementLabelWarns(core::CalibrationSource source);

} // namespace slideio::viewer::ui
