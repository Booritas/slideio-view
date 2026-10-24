#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationMeasurementExport.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

namespace
{

const std::string kHeader =
    "id,type,label,classification,center_x_px,center_y_px,area_px2,perimeter_px,length_px,"
    "area_um2,perimeter_um,length_um,area_note,pixel_width_um,pixel_height_um,calibration_source,"
    "slide,scene,slide_id,exported_at";

const auto kExportedAt = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(1792576800LL));

using Row = std::vector<std::string>;

/// RFC 4180, enough for these tests: rows end at CRLF outside quotes, fields
/// at commas outside quotes, and a doubled quote inside quotes is one quote.
/// The byte-order mark is stripped first.
std::vector<Row> parseCsv(std::string text)
{
    const std::string bom = "\xEF\xBB\xBF";
    if (text.compare(0, bom.size(), bom) == 0) {
        text.erase(0, bom.size());
    }
    std::vector<Row> rows;
    Row row;
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                cell += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                cell += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            row.push_back(cell);
            cell.clear();
        } else if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            row.push_back(cell);
            cell.clear();
            rows.push_back(row);
            row.clear();
            ++i;
        } else {
            cell += c;
        }
    }
    return rows;
}

MeasurementExportContext calibrated(double width = 0.25, double height = 0.25)
{
    MeasurementExportContext context;
    context.slidePath = "D:/slides/Philips-1.tiff";
    context.slideId = "ec83fc2ebbfbc515";
    context.sceneIndex = 0;
    context.pixelWidthMicrons = width;
    context.pixelHeightMicrons = height;
    return context;
}

/// The file for `annotations`, required to succeed, parsed.
std::vector<Row> exportRows(const std::vector<core::Annotation>& annotations,
                            const MeasurementExportContext& context = calibrated())
{
    const MeasurementExportResult result = annotationMeasurementCsv(annotations, context, kExportedAt);
    REQUIRE(result.error.empty());
    return parseCsv(result.text);
}

/// The cell under `column` in data row `index` (0 = first annotation).
std::string cell(const std::vector<Row>& rows, std::size_t index, const std::string& column)
{
    REQUIRE(rows.size() > index + 1);
    const Row& header = rows.front();
    for (std::size_t c = 0; c < header.size(); ++c) {
        if (header[c] == column) {
            REQUIRE(rows[index + 1].size() == header.size());
            return rows[index + 1][c];
        }
    }
    FAIL("no column " << column);
    return {};
}

core::Annotation shape(const std::string& id, core::AnnotationGeometry geometry, const std::string& label = "",
                       const std::string& classification = "")
{
    core::AnnotationProperties properties;
    properties.label = label;
    properties.classification = classification;
    return core::Annotation(id, std::move(geometry), properties, core::AnnotationMetadata{});
}

core::Annotation rectangle(const std::string& id, double x0, double y0, double x1, double y1)
{
    return shape(id, core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}});
}

core::Annotation line(const std::string& id, std::vector<core::PointF> points)
{
    return shape(id, core::PolygonGeometry{std::move(points), false});
}

const std::vector<std::string> kMeasurementColumns = {"area_px2", "perimeter_px", "length_px", "area_um2",
                                                      "perimeter_um", "length_um", "area_note"};
const std::vector<std::string> kMicronColumns = {"area_um2", "perimeter_um", "length_um", "pixel_width_um",
                                                 "pixel_height_um"};

} // namespace

TEST_CASE("the header is fixed whatever the calibration", "[ui][AnnotationMeasurementExport]")
{
    for (const MeasurementExportContext& context : {calibrated(), calibrated(0.0, 0.0)}) {
        const std::vector<Row> rows = exportRows({}, context);
        REQUIRE(rows.size() == 1);
        std::string header;
        for (std::size_t i = 0; i < rows[0].size(); ++i) {
            header += (i == 0 ? "" : ",") + rows[0][i];
        }
        CHECK(header == kHeader);
    }
}

TEST_CASE("one row per annotation, in model order", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<Row> rows = exportRows({rectangle("a1", 0, 0, 1, 1), rectangle("a2", 0, 0, 2, 2),
                                              shape("a3", core::PointGeometry{core::PointF{1, 1}})});
    REQUIRE(rows.size() == 4);
    CHECK(cell(rows, 0, "id") == "a1");
    CHECK(cell(rows, 1, "id") == "a2");
    CHECK(cell(rows, 2, "id") == "a3");
    CHECK(cell(rows, 2, "type") == "point");
}

TEST_CASE("a rectangle is measured in pixels and microns", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<Row> rows = exportRows({rectangle("r", 0, 0, 300, 200)});
    CHECK(cell(rows, 0, "type") == "rectangle");
    CHECK(cell(rows, 0, "area_px2") == "60000");
    CHECK(cell(rows, 0, "perimeter_px") == "1000");
    CHECK(cell(rows, 0, "area_um2") == "3750");
    CHECK(cell(rows, 0, "perimeter_um") == "250");
    CHECK(cell(rows, 0, "length_px").empty());
    CHECK(cell(rows, 0, "length_um").empty());
    CHECK(cell(rows, 0, "area_note").empty());
    CHECK(cell(rows, 0, "center_x_px") == "150");
    CHECK(cell(rows, 0, "center_y_px") == "100");
    CHECK(cell(rows, 0, "pixel_width_um") == "0.25");
    CHECK(cell(rows, 0, "pixel_height_um") == "0.25");
    CHECK(cell(rows, 0, "slide") == "D:/slides/Philips-1.tiff");
    CHECK(cell(rows, 0, "scene") == "0");
    CHECK(cell(rows, 0, "slide_id") == "ec83fc2ebbfbc515");
    CHECK(cell(rows, 0, "exported_at") == "2026-10-21T10:00:00Z");
}

TEST_CASE("non-square pixels scale each direction separately", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<Row> rows = exportRows(
        {line("h", {{0, 0}, {100, 0}}), line("v", {{0, 0}, {0, 100}})}, calibrated(0.25, 0.5));
    CHECK(cell(rows, 0, "length_px") == "100");
    CHECK(cell(rows, 1, "length_px") == "100");
    CHECK(cell(rows, 0, "length_um") == "25");
    CHECK(cell(rows, 1, "length_um") == "50");
}

TEST_CASE("an ellipse's area is measure()'s, not pi a b", "[ui][AnnotationMeasurementExport]")
{
    const core::EllipseGeometry ellipse{core::PointF{0, 0}, core::PointF{400, 200}};
    const std::vector<Row> rows = exportRows({shape("e", ellipse)});
    const core::ShapeMetrics expected = core::measure(ellipse, core::PixelSize{1.0, 1.0});
    REQUIRE(expected.area.has_value());
    CHECK(std::stod(cell(rows, 0, "area_px2")) == *expected.area);
    CHECK(std::stod(cell(rows, 0, "area_px2")) != 3.14159265358979323846 * 200 * 100);
}

TEST_CASE("a self-intersecting polygon has no area and says why", "[ui][AnnotationMeasurementExport]")
{
    const core::PolygonGeometry bowTie{{{0, 0}, {10, 10}, {10, 0}, {0, 10}}, true};
    const std::vector<Row> rows = exportRows({shape("b", bowTie)});
    CHECK(cell(rows, 0, "area_px2").empty());
    CHECK(cell(rows, 0, "area_um2").empty());
    CHECK(cell(rows, 0, "area_note") == "self-intersecting");
    CHECK_FALSE(cell(rows, 0, "perimeter_px").empty());
    CHECK_FALSE(cell(rows, 0, "perimeter_um").empty());
}

TEST_CASE("open shapes have a length and no area or perimeter", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<Row> rows = exportRows(
        {line("l", {{0, 0}, {30, 40}}), shape("a", core::ArrowGeometry{core::PointF{0, 0}, core::PointF{30, 40}})});
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(cell(rows, i, "length_px") == "50");
        CHECK(cell(rows, i, "length_um") == "12.5");
        CHECK(cell(rows, i, "area_px2").empty());
        CHECK(cell(rows, i, "perimeter_px").empty());
        CHECK(cell(rows, i, "area_note").empty());
    }
    CHECK(cell(rows, 0, "type") == "polyline");
    CHECK(cell(rows, 1, "type") == "arrow");
}

TEST_CASE("a point has a position and no measurement", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<Row> rows = exportRows({shape("p", core::PointGeometry{core::PointF{1000, 1500}})});
    CHECK(cell(rows, 0, "center_x_px") == "1000");
    CHECK(cell(rows, 0, "center_y_px") == "1500");
    for (const std::string& column : kMeasurementColumns) {
        INFO(column);
        CHECK(cell(rows, 0, column).empty());
    }
}

TEST_CASE("an uncalibrated slide leaves every micron column empty", "[ui][AnnotationMeasurementExport]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto& [w, h] : std::vector<std::pair<double, double>>{{0, 0}, {0.25, 0}, {nan, 0.25}, {inf, 0.25}}) {
        INFO(w << " x " << h);
        const std::vector<Row> rows = exportRows({rectangle("r", 0, 0, 300, 200)}, calibrated(w, h));
        for (const std::string& column : kMicronColumns) {
            INFO(column);
            CHECK(cell(rows, 0, column).empty());
        }
        CHECK(cell(rows, 0, "area_px2") == "60000");
        CHECK(cell(rows, 0, "perimeter_px") == "1000");
    }
}

TEST_CASE("classification is trimmed and unclassified is empty", "[ui][AnnotationMeasurementExport]")
{
    const core::PointGeometry here{core::PointF{1, 1}};
    const std::vector<Row> rows = exportRows(
        {shape("t", here, "", " Tumor\t"), shape("u", here, "", "UNCLASSIFIED"), shape("b", here, "", "  ")});
    CHECK(cell(rows, 0, "classification") == "Tumor");
    CHECK(cell(rows, 1, "classification").empty());
    CHECK(cell(rows, 2, "classification").empty());
}

TEST_CASE("text fields are guarded and quoted as in the counts file", "[ui][AnnotationMeasurementExport]")
{
    const core::PointGeometry here{core::PointF{1, 1}};
    const MeasurementExportResult result = annotationMeasurementCsv(
        {shape("f", here, "=1+1"), shape("c", here, "a,b"), shape("n", here, "two\nlines"), shape("t", here, "\tx")},
        calibrated(), kExportedAt);
    REQUIRE(result.error.empty());
    const std::vector<Row> rows = parseCsv(result.text);
    REQUIRE(rows.size() == 5);
    CHECK(cell(rows, 0, "label") == "'=1+1");
    CHECK(cell(rows, 1, "label") == "a,b");
    CHECK(cell(rows, 2, "label") == "two\nlines");
    CHECK(cell(rows, 3, "label") == "'\tx");
    CHECK(result.text.find("\"a,b\"") != std::string::npos);
}

TEST_CASE("negative numbers are written as numbers", "[ui][AnnotationMeasurementExport]")
{
    // Drawing past the slide's edge is allowed. The formula guard would turn
    // -5 into the text '-5 if a number went through it.
    const std::vector<Row> rows = exportRows({shape("p", core::PointGeometry{core::PointF{-5, -7.5}})});
    CHECK(cell(rows, 0, "center_x_px") == "-5");
    CHECK(cell(rows, 0, "center_y_px") == "-7.5");
}

TEST_CASE("numbers have no exponent and round-trip", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<core::Annotation> annotations = {rectangle("tiny", 0, 0, 1e-3, 1e-4),
                                                       rectangle("huge", 0, 0, 1.5e5, 1e5)};
    const std::vector<Row> rows = exportRows(annotations, calibrated(1.0, 1.0));
    for (std::size_t i = 0; i < annotations.size(); ++i) {
        for (const std::string& column : {"area_px2", "perimeter_px", "area_um2", "perimeter_um"}) {
            const std::string value = cell(rows, i, column);
            INFO(column << " = " << value);
            CHECK(value.find_first_of("eE") == std::string::npos);
        }
        const core::ShapeMetrics metrics = core::measure(annotations[i].geometry(), core::PixelSize{1.0, 1.0});
        CHECK(std::stod(cell(rows, i, "area_px2")) == *metrics.area);
        CHECK(std::stod(cell(rows, i, "perimeter_px")) == metrics.outlineLength);
    }
    CHECK(cell(rows, 1, "area_px2") == "15000000000");
}

TEST_CASE("non-ASCII text, BOM and CRLF", "[ui][AnnotationMeasurementExport]")
{
    const std::string greek = "\xCE\x9D\xCE\xAD\xCE\xBA\xCF\x81\xCF\x89\xCF\x83\xCE\xB7";
    const MeasurementExportResult result = annotationMeasurementCsv(
        {shape("g", core::PointGeometry{core::PointF{1, 1}}, greek, greek)}, calibrated(), kExportedAt);
    REQUIRE(result.error.empty());
    CHECK(result.text.compare(0, 3, "\xEF\xBB\xBF") == 0);
    CHECK(result.text.size() >= 2);
    CHECK(result.text.compare(result.text.size() - 2, 2, "\r\n") == 0);
    std::size_t lines = 0;
    for (std::size_t at = result.text.find('\n'); at != std::string::npos; at = result.text.find('\n', at + 1)) {
        CHECK(result.text[at - 1] == '\r');
        ++lines;
    }
    CHECK(lines == 2);
    const std::vector<Row> rows = parseCsv(result.text);
    CHECK(cell(rows, 0, "label") == greek);
    CHECK(cell(rows, 0, "classification") == greek);
}

TEST_CASE("a long closed trace is measured", "[ui][AnnotationMeasurementExport]")
{
    std::vector<core::PointF> ring;
    for (int i = 0; i < 3000; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * i / 3000.0;
        ring.push_back(core::PointF{500.0 + 400.0 * std::cos(angle), 500.0 + 300.0 * std::sin(angle)});
    }
    const std::vector<Row> rows = exportRows({shape("trace", core::PolygonGeometry{ring, true})});
    CHECK_FALSE(cell(rows, 0, "area_px2").empty());
    CHECK_FALSE(cell(rows, 0, "area_um2").empty());
    CHECK(cell(rows, 0, "area_note").empty());
}

TEST_CASE("a non-finite measurement fails the export and names the annotation", "[ui][AnnotationMeasurementExport]")
{
    const core::PolygonGeometry vast{{{1e308, 0}, {-1e308, 0}, {0, 1e308}}, true};
    const MeasurementExportResult result =
        annotationMeasurementCsv({rectangle("ok", 0, 0, 1, 1), shape("vast", vast)}, calibrated(), kExportedAt);
    CHECK(result.text.empty());
    CHECK(result.error.find("vast") != std::string::npos);
}

TEST_CASE("measurementExportFileStem keys the name by scene", "[ui][AnnotationMeasurementExport]")
{
    CHECK(measurementExportFileStem("Philips-1.tiff", 1) == "Philips-1.tiff.s1.measurements");
}

TEST_CASE("a text is a row with its words and no measurement", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<Row> rows =
        exportRows({shape("t", core::TextGeometry{core::PointF{3, 4}}, "a,\"b\"")});
    CHECK(cell(rows, 0, "type") == "text");
    CHECK(cell(rows, 0, "label") == "a,\"b\"");
    CHECK(cell(rows, 0, "center_x_px") == "3");
    CHECK(cell(rows, 0, "center_y_px") == "4");
    for (const std::string& column : kMeasurementColumns) {
        INFO(column);
        CHECK(cell(rows, 0, column).empty());
    }
}

TEST_CASE("each row says where its calibration came from", "[ui][AnnotationMeasurementExport]")
{
    const std::vector<core::Annotation> annotations = {rectangle("r", 0, 0, 10, 10)};

    MeasurementExportContext manual = calibrated(0.5, 0.5);
    manual.calibrationSource = core::CalibrationSource::Manual;
    const std::vector<Row> manualRows = exportRows(annotations, manual);
    CHECK(cell(manualRows, 0, "calibration_source") == "manual");
    CHECK(cell(manualRows, 0, "pixel_width_um") == "0.5");
    CHECK(cell(manualRows, 0, "area_um2") == "25");

    MeasurementExportContext metadata = calibrated();
    metadata.calibrationSource = core::CalibrationSource::Metadata;
    CHECK(cell(exportRows(annotations, metadata), 0, "calibration_source") == "metadata");

    MeasurementExportContext none = calibrated(0.0, 0.0);
    none.calibrationSource = core::CalibrationSource::None;
    const std::vector<Row> noneRows = exportRows(annotations, none);
    CHECK(cell(noneRows, 0, "calibration_source") == "none");
    CHECK(cell(noneRows, 0, "area_um2").empty());
    CHECK(cell(noneRows, 0, "pixel_width_um").empty());
}
