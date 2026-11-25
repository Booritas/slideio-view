#pragma once

#include "slideio/viewer/core/ResultsSource.h"

#include <cstdint>
#include <string>

namespace slideio::viewer::ui
{

/// The Results tab's words, kept out of the widget so a suite can read them.
/// Every function returns UTF-8 and reads no locale.

/// `n` with its thousands separated by a space: "12 345", "-1 000".
[[nodiscard]] std::string groupedCount(int64_t n);

/// A layer over the draw budget whose overview map is drawn:
/// "12 345 in view — overview; zoom in for outlines".
[[nodiscard]] std::string inViewText(int64_t n);

/// A layer over the draw budget whose overview map is still being built:
/// "building overview… 43 %". The percentage is floor(fraction × 100), clamped
/// to [0, 100], so it reads 100 % only once the scan is done.
[[nodiscard]] std::string buildingText(double fraction);

/// One line of the selected result's details: "area  41.0 px²", with
/// " (<stage>)" appended for a measurement another stage asserted. Values of
/// magnitude 1 or more (and 0) show one decimal; smaller ones three
/// significant digits.
[[nodiscard]] std::string measurementLine(const core::ResultMeasurement& measurement);

/// The layer's stage-state badge: "stale" or "failed", else "".
[[nodiscard]] std::string stageStateBadge(const std::string& stageState);

} // namespace slideio::viewer::ui
