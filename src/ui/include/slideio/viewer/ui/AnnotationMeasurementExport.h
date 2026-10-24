#pragma once

#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/Calibration.h"

#include <chrono>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// Where a measurements file's numbers came from, written into every row.
struct MeasurementExportContext
{
    /// The slide as the user opened it, verbatim.
    std::string slidePath;
    /// core::computeSlideId; empty for a slide that could not be identified.
    std::string slideId;
    int sceneIndex = 0;
    /// Microns per pixel -- SlideInfo::resolutionX/Y, which are metres, times
    /// 1e6. The slide counts as calibrated only when BOTH are finite and
    /// greater than zero, the properties panel's test; otherwise every micron
    /// column is left empty rather than half-filled.
    double pixelWidthMicrons = 0.0;
    double pixelHeightMicrons = 0.0;
    /// Where those two came from, written as `calibration_source` beside them.
    /// When it is Manual they are the user's override, not the metadata.
    core::CalibrationSource calibrationSource = core::CalibrationSource::None;
};

struct MeasurementExportResult
{
    /// The whole file: UTF-8 with a byte-order mark, CRLF, RFC 4180. Empty on
    /// failure.
    std::string text;
    /// Empty on success. On failure, says what could not be written and names
    /// the annotation.
    std::string error;
};

/// One CSV row per annotation with its area, perimeter or length, in pixels
/// and -- on a calibrated slide -- in microns (FR-ANN-34). See
/// docs/superpowers/specs/2026-10-23-annotation-measurements-export-design.md.
///
/// Every annotation is a row, in the order given, never filtered: a point is
/// counted, not measured, and still appears with its position. Every number
/// comes from core::measure(), the function the properties panel calls, so the
/// file cannot disagree with the panel. A measurement that is missing --
/// a self-intersecting polygon's area -- is an EMPTY cell with the reason in
/// `area_note`, never a zero that reads as a finding.
///
/// Numbers are plain fixed-notation decimals at full precision, never through
/// the text-field quoting, which would turn a negative number into text.
///
/// Refuses -- empty text, a message in `error` -- rather than write a value
/// that is not a finite number.
[[nodiscard]] MeasurementExportResult annotationMeasurementCsv(
    const std::vector<core::Annotation>& annotations, const MeasurementExportContext& context,
    std::chrono::system_clock::time_point exportedAt);

/// sceneExportFileStem(...) + ".measurements", so the three exports of one
/// scene sort together.
[[nodiscard]] std::string measurementExportFileStem(const std::string& slideFileName, int sceneIndex);

} // namespace slideio::viewer::ui
