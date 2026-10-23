#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationGeoJsonImport.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <QString>

#include <string>
#include <vector>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

namespace
{

core::Annotation point(double x, double y)
{
    return core::Annotation{"p", core::PointGeometry{core::PointF{x, y}}};
}

core::Annotation box(double x0, double y0, double x1, double y1)
{
    return core::Annotation{"r", core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}}};
}

/// A summary with nothing but `toAdd`, to be adjusted by each test.
struct Fixture
{
    core::AnnotationImportResult result;
    GeoJsonImportSummaryInput input;

    Fixture()
    {
        input.fileName = QStringLiteral("x.geojson");
        input.result = &result;
        input.toAdd = 27;
        input.openSlideId = "def";
        input.openScene = 0;
    }

    [[nodiscard]] std::string text() const
    {
        return geoJsonImportSummary(input).toStdString();
    }
};

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("countOutsideScene counts only shapes wholly outside", "[ui][GeoJsonImport]")
{
    const std::vector<core::Annotation> annotations = {
        point(50, 50),             // inside
        point(150, 50),            // outside
        box(-10, -10, 5, 5),       // overhangs the corner: inside
        box(100, 0, 120, 10),      // touches the right edge: inside
        point(-1, -1),             // outside
    };
    REQUIRE(countOutsideScene(annotations, 100, 100) == 2);
}

TEST_CASE("the summary names the file and the count to add", "[ui][GeoJsonImport]")
{
    Fixture f;
    const std::string text = f.text();
    CHECK(contains(text, "x.geojson"));
    CHECK(contains(text, "27 annotations will be added"));

    f.input.toAdd = 1;
    CHECK(contains(f.text(), "1 annotation will be added"));
}

TEST_CASE("zero-count lines are omitted", "[ui][GeoJsonImport]")
{
    Fixture f;
    const std::string text = f.text();
    CHECK_FALSE(contains(text, "skipped"));
    CHECK_FALSE(contains(text, "\xE2\x9A\xA0"));
}

TEST_CASE("every skip reason has its line", "[ui][GeoJsonImport]")
{
    Fixture f;
    f.input.alreadyPresent = 3;
    f.result.skippedObjectTypes["detection"] = 112;
    f.result.withHoles = 1;
    f.result.unsupportedGeometry = 5;
    f.result.invalidGeometry = 2;
    f.result.nestedObjects = 4;
    const std::string text = f.text();
    CHECK(contains(text, "3 already in this scene"));
    CHECK(contains(text, "112"));
    CHECK(contains(text, "detection"));
    CHECK(contains(text, "1 polygon with holes"));
    CHECK(contains(text, "5 with an unsupported geometry"));
    CHECK(contains(text, "2 with invalid coordinates"));
    CHECK(contains(text, "4 nested objects"));
}

TEST_CASE("nothing to add says so", "[ui][GeoJsonImport]")
{
    Fixture f;
    f.input.toAdd = 0;
    f.input.alreadyPresent = 27;
    const std::string text = f.text();
    CHECK_FALSE(contains(text, "will be added"));
    CHECK(contains(text, "Nothing"));
    CHECK(contains(text, "27 already in this scene"));
}

TEST_CASE("a different scene or slide is warned about", "[ui][GeoJsonImport]")
{
    Fixture f;
    f.result.provenance.present = true;
    f.result.provenance.slide = "D:/slides/Philips-1.tiff";
    f.result.provenance.slideId = "def";
    f.result.provenance.scene = 1;
    std::string text = f.text();
    CHECK(contains(text, "scene 1"));
    CHECK(contains(text, "Philips-1.tiff"));
    CHECK_FALSE(contains(text, "D:/slides"));

    f.result.provenance.scene = 0;
    f.result.provenance.slideId = "abc";
    text = f.text();
    CHECK(contains(text, "\xE2\x9A\xA0"));
    CHECK(contains(text, "different slide"));

    f.result.provenance.slideId = "def";
    CHECK_FALSE(contains(f.text(), "\xE2\x9A\xA0"));

    f.result.provenance.slideId = "";
    CHECK_FALSE(contains(f.text(), "\xE2\x9A\xA0"));

    f.result.provenance.present = false;
    f.result.provenance.scene = 3;
    CHECK_FALSE(contains(f.text(), "\xE2\x9A\xA0"));
}

TEST_CASE("a slide name with percent signs is shown as it is", "[ui][GeoJsonImport]")
{
    // The name comes from the file. Chained .arg() calls would substitute a
    // %3 in it with the open scene's number.
    Fixture f;
    f.result.provenance.present = true;
    f.result.provenance.slide = "D:/slides/a%3b%1.tiff";
    f.result.provenance.slideId = "def";
    f.result.provenance.scene = 1;
    const std::string text = f.text();
    CHECK(contains(text, "\"a%3b%1.tiff\""));
    CHECK(contains(text, "the open scene is scene 0"));
}

TEST_CASE("approximated splines are said so", "[ui][GeoJsonImport]")
{
    Fixture f;
    f.result.splinesApproximated = 2;
    CHECK(contains(f.text(), "2 splines imported as polygons through their control points"));
    f.result.splinesApproximated = 1;
    CHECK(contains(f.text(), "1 spline imported as a polygon through its control points"));
    f.result.splinesApproximated = 0;
    CHECK_FALSE(contains(f.text(), "spline"));
}

TEST_CASE("an unsupported type is named in its skip line", "[ui][GeoJsonImport]")
{
    Fixture f;
    f.result.unsupportedTypes["Ellipse"] = 3;
    CHECK(contains(f.text(), "3 annotations of type \"Ellipse\" \xE2\x80\x94 skipped (the viewer has no such shape)"));
    f.result.unsupportedTypes["Ellipse"] = 1;
    CHECK(contains(f.text(), "1 annotation of type \"Ellipse\" \xE2\x80\x94 skipped (the viewer has no such shape)"));
}

TEST_CASE("shapes outside the scene are warned about", "[ui][GeoJsonImport]")
{
    Fixture f;
    f.input.outsideScene = 4;
    CHECK(contains(f.text(), "4 of the annotations lie entirely outside this scene"));
    f.input.outsideScene = 1;
    CHECK(contains(f.text(), "1 of the annotations lies entirely outside this scene"));
}
