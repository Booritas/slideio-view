#include "slideio/viewer/ui/AnnotationMeasurementExport.h"

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationMeasurement.h"
#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"
#include "slideio/viewer/ui/AnnotationCountExport.h"
#include "slideio/viewer/ui/CsvFormat.h"

#include <QLocale>
#include <QString>

#include <cmath>
#include <optional>
#include <string>

namespace slideio::viewer::ui
{

namespace
{

constexpr const char* kHeader =
    "id,type,label,classification,center_x_px,center_y_px,area_px2,perimeter_px,length_px,"
    "area_um2,perimeter_um,length_um,area_note,pixel_width_um,pixel_height_um,"
    "slide,scene,slide_id,exported_at";

/// Appends one number cell, or nothing for an absent value. Fixed notation at
/// the shortest precision that reads back as the same double: no exponent a
/// spreadsheet might misread, no thousands separator, no display rounding.
/// QString::number with QLocale::FloatingPointShortest, which is locale-free:
/// the floating-point std::to_chars is unavailable below macOS 13.3, which the
/// macOS builds target, and a stream or printf reads the C locale and could
/// write a decimal comma. False when the value is not finite -- a spreadsheet
/// given "inf" would read text.
bool appendNumber(std::string& row, std::optional<double> value)
{
    if (!value.has_value()) {
        return true;
    }
    if (!std::isfinite(*value)) {
        return false;
    }
    row += QString::number(*value, 'f', QLocale::FloatingPointShortest).toStdString();
    return true;
}

/// What a measurement column holds for one shape: area for a closed shape when
/// measure() gives one, perimeter for a closed shape, length for an open one.
struct Columns
{
    std::optional<double> area;
    std::optional<double> perimeter;
    std::optional<double> length;
};

Columns columnsOf(const core::ShapeMetrics& metrics, bool measured)
{
    Columns columns;
    if (!measured) {
        return columns;
    }
    if (metrics.closed) {
        columns.area = metrics.area;
        columns.perimeter = metrics.outlineLength;
    } else {
        columns.length = metrics.outlineLength;
    }
    return columns;
}

} // namespace

MeasurementExportResult annotationMeasurementCsv(const std::vector<core::Annotation>& annotations,
                                                 const MeasurementExportContext& context,
                                                 std::chrono::system_clock::time_point exportedAt)
{
    const auto known = [](double size) { return std::isfinite(size) && size > 0.0; };
    const bool calibrated = known(context.pixelWidthMicrons) && known(context.pixelHeightMicrons);
    // Formatted once: every row carries the same instant.
    const std::string provenance = csvProvenance(context.slidePath, context.sceneIndex, context.slideId,
                                                 core::formatIso8601Utc(exportedAt));
    const auto failure = [](const core::Annotation& annotation) {
        return MeasurementExportResult{
            {}, "annotation " + annotation.id() + " has a measurement that is not a finite number"};
    };

    std::string csv = kCsvUtf8ByteOrderMark;
    csv += kHeader;
    csv += kCsvLineEnding;

    for (const core::Annotation& annotation : annotations) {
        // A point is counted, not measured: its outline has no length or area
        // to report, and a zero in those columns would read as a finding.
        const bool measured = annotation.type() != core::AnnotationType::Point;
        const core::ShapeMetrics pixels = core::measure(annotation.geometry(), core::PixelSize{1.0, 1.0});
        const Columns inPixels = columnsOf(pixels, measured);
        Columns inMicrons;
        if (calibrated) {
            // Scaling the vertices before measuring is the only order correct
            // on non-square pixels, so this is a second measure(), not the
            // pixel figures multiplied. Self-intersection does not change
            // with scale, so the first call's O(n^2) answer is reused.
            inMicrons = columnsOf(core::measureKnown(annotation.geometry(),
                                                     core::PixelSize{context.pixelWidthMicrons,
                                                                     context.pixelHeightMicrons},
                                                     pixels.selfIntersecting),
                                  measured);
        }

        const core::RectF box = annotation.boundingBox();
        const core::AnnotationProperties& properties = annotation.properties();
        const std::string classification = core::trimAsciiWhitespace(properties.classification);
        const bool unclassified = core::isBlankOrUnclassified(classification);

        std::string row = csvField(annotation.id());
        row += ',';
        row += core::annotationTypeName(annotation.geometry());
        row += ',';
        row += csvField(properties.label);
        row += ',';
        row += unclassified ? std::string() : csvField(classification);
        bool finite = true;
        for (const std::optional<double>& value :
             {std::optional<double>(box.x + box.width * 0.5), std::optional<double>(box.y + box.height * 0.5),
              inPixels.area, inPixels.perimeter, inPixels.length, inMicrons.area, inMicrons.perimeter,
              inMicrons.length}) {
            row += ',';
            finite = appendNumber(row, value) && finite;
        }
        row += ',';
        // Why an area is missing, for the one case that is a failure rather
        // than the shape's nature: an open path or a point has no area at all.
        if (measured && pixels.closed && !pixels.area.has_value() && pixels.selfIntersecting) {
            row += "self-intersecting";
        }
        for (const double size : {context.pixelWidthMicrons, context.pixelHeightMicrons}) {
            row += ',';
            finite = appendNumber(row, calibrated ? std::optional<double>(size) : std::nullopt) && finite;
        }
        if (!finite) {
            return failure(annotation);
        }
        row += ',';
        row += provenance;
        row += kCsvLineEnding;
        csv += row;
    }
    return {csv, {}};
}

std::string measurementExportFileStem(const std::string& slideFileName, int sceneIndex)
{
    return sceneExportFileStem(slideFileName, sceneIndex) + ".measurements";
}

} // namespace slideio::viewer::ui
