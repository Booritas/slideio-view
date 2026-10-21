#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeoJson.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <limits>
#include <string>
#include <vector>

using namespace slideio::viewer::core;
using json = nlohmann::json;

namespace
{

std::chrono::system_clock::time_point at(long long epochSeconds)
{
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(epochSeconds));
}

GeoJsonExportContext context()
{
    GeoJsonExportContext c;
    c.slidePath = "D:/slides/Philips-1.tiff";
    c.slideId = "ec83fc2ebbfbc515";
    c.sceneIndex = 0;
    return c;
}

/// Exports `annotations`, requires success, and parses the file back.
json exportAndParse(const std::vector<Annotation>& annotations)
{
    const GeoJsonExportResult result = annotationsToGeoJson(annotations, context(), at(1792576800LL));
    REQUIRE(result.error.empty());
    REQUIRE_FALSE(result.text.empty());
    return json::parse(result.text);
}

json onlyGeometry(const AnnotationGeometry& geometry)
{
    const json file = exportAndParse({Annotation("a1", geometry)});
    REQUIRE(file.at("features").size() == 1);
    return file.at("features").at(0).at("geometry");
}

json position(double x, double y)
{
    return json::array({x, y});
}

} // namespace

TEST_CASE("each shape maps to its GeoJSON geometry", "[core][GeoJson]")
{
    const json rectangle = onlyGeometry(RectangleGeometry{PointF{10, 20}, PointF{110, 70}});
    REQUIRE(rectangle.at("type") == "Polygon");
    REQUIRE(rectangle.at("coordinates").size() == 1);
    REQUIRE(rectangle.at("coordinates").at(0).size() == 5);
    REQUIRE(rectangle.at("coordinates").at(0).at(0) == position(10, 20));

    const json ellipse = onlyGeometry(EllipseGeometry{PointF{0, 0}, PointF{200, 100}});
    REQUIRE(ellipse.at("type") == "Polygon");
    REQUIRE(ellipse.at("coordinates").at(0).size() == kEllipseOutlineVertices + 1);

    const json polygon =
        onlyGeometry(PolygonGeometry{{PointF{0, 0}, PointF{10, 0}, PointF{10, 10}}, true});
    REQUIRE(polygon.at("type") == "Polygon");
    REQUIRE(polygon.at("coordinates").at(0).size() == 4);

    const json polyline =
        onlyGeometry(PolygonGeometry{{PointF{0, 0}, PointF{10, 0}, PointF{10, 10}}, false});
    REQUIRE(polyline.at("type") == "LineString");
    REQUIRE(polyline.at("coordinates").size() == 3);

    const json point = onlyGeometry(PointGeometry{PointF{5, 6}});
    REQUIRE(point.at("type") == "Point");
    REQUIRE(point.at("coordinates") == position(5, 6));

    const json arrow = onlyGeometry(ArrowGeometry{PointF{1, 2}, PointF{9, 8}});
    REQUIRE(arrow.at("type") == "LineString");
    REQUIRE(arrow.at("coordinates") == json::array({position(1, 2), position(9, 8)}));
}

TEST_CASE("an inverted rectangle is written normalised, as outline() paints it", "[core][GeoJson]")
{
    const json rectangle = onlyGeometry(RectangleGeometry{PointF{110, 70}, PointF{10, 20}});
    REQUIRE(rectangle.at("coordinates").at(0).at(0) == position(10, 20));
}

TEST_CASE("every closed ring ends where it starts", "[core][GeoJson]")
{
    const std::vector<AnnotationGeometry> closedShapes = {
        RectangleGeometry{PointF{10, 20}, PointF{110, 70}},
        EllipseGeometry{PointF{0, 0}, PointF{200, 100}},
        PolygonGeometry{{PointF{0, 0}, PointF{10, 0}, PointF{10, 10}}, true},
    };
    for (const AnnotationGeometry& shape : closedShapes) {
        const json ring = onlyGeometry(shape).at("coordinates").at(0);
        REQUIRE(ring.front() == ring.back());
    }
}

TEST_CASE("a consecutive duplicate vertex is dropped, as outline() drops it", "[core][GeoJson]")
{
    const json ring = onlyGeometry(PolygonGeometry{
        {PointF{0, 0}, PointF{10, 0}, PointF{10, 0}, PointF{10, 10}}, true})
                          .at("coordinates")
                          .at(0);
    REQUIRE(ring == json::array({position(0, 0), position(10, 0), position(10, 10), position(0, 0)}));
}

TEST_CASE("a polygon outline() reduces below three points is written from its stored vertices",
          "[core][GeoJson]")
{
    const json ring =
        onlyGeometry(PolygonGeometry{{PointF{0, 0}, PointF{0, 0}, PointF{5, 5}}, true})
            .at("coordinates")
            .at(0);
    REQUIRE(ring == json::array({position(0, 0), position(0, 0), position(5, 5), position(0, 0)}));
}

TEST_CASE("an open line outline() reduces below two points is written from its stored vertices",
          "[core][GeoJson]")
{
    const json line = onlyGeometry(PolygonGeometry{{PointF{3, 3}, PointF{3, 3}}, false});
    REQUIRE(line.at("type") == "LineString");
    REQUIRE(line.at("coordinates") == json::array({position(3, 3), position(3, 3)}));
}

TEST_CASE("a self-intersecting polygon is written unchanged", "[core][GeoJson]")
{
    const json ring = onlyGeometry(PolygonGeometry{
        {PointF{0, 0}, PointF{10, 10}, PointF{10, 0}, PointF{0, 10}}, true})
                          .at("coordinates")
                          .at(0);
    REQUIRE(ring == json::array({position(0, 0), position(10, 10), position(10, 0), position(0, 10),
                                 position(0, 0)}));
}

TEST_CASE("coordinates outside the slide are written verbatim", "[core][GeoJson]")
{
    const json point = onlyGeometry(PointGeometry{PointF{-50.5, 1e6}});
    REQUIRE(point.at("coordinates") == position(-50.5, 1000000.0));
}

TEST_CASE("one feature per annotation, ids and order kept", "[core][GeoJson]")
{
    const json file = exportAndParse({Annotation("a1", PointGeometry{PointF{1, 1}}),
                                      Annotation("a2", PointGeometry{PointF{2, 2}}),
                                      Annotation("a3", PointGeometry{PointF{3, 3}})});
    REQUIRE(file.at("type") == "FeatureCollection");
    const json& features = file.at("features");
    REQUIRE(features.size() == 3);
    const std::vector<std::string> ids = {"a1", "a2", "a3"};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        REQUIRE(features.at(i).at("type") == "Feature");
        REQUIRE(features.at(i).at("id") == ids[i]);
        REQUIRE(features.at(i).at("properties").at("objectType") == "annotation");
    }
}

TEST_CASE("an empty scene is a valid empty collection", "[core][GeoJson]")
{
    const GeoJsonExportResult result = annotationsToGeoJson({}, context(), at(1792576800LL));
    REQUIRE(result.error.empty());
    REQUIRE(result.text.back() == '\n');
    REQUIRE(result.text.find('\n') == result.text.size() - 1);
    const json file = json::parse(result.text);
    REQUIRE(file.at("type") == "FeatureCollection");
    REQUIRE(file.at("features").is_array());
    REQUIRE(file.at("features").empty());
}

TEST_CASE("a non-finite coordinate fails the export and names the annotation", "[core][GeoJson]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const GeoJsonExportResult result =
        annotationsToGeoJson({Annotation("a1", PointGeometry{PointF{1, 1}}),
                              Annotation("a2", PointGeometry{PointF{nan, 0}})},
                             context(), at(1792576800LL));
    REQUIRE(result.text.empty());
    REQUIRE(result.error.find("a2") != std::string::npos);
}

TEST_CASE("a large scene is written whole", "[core][GeoJson]")
{
    std::vector<Annotation> annotations;
    annotations.reserve(2000);
    for (int i = 0; i < 2000; ++i) {
        const double x = i * 10.0;
        annotations.emplace_back("e" + std::to_string(i),
                                 EllipseGeometry{PointF{x, 0}, PointF{x + 8, 6}});
    }
    REQUIRE(exportAndParse(annotations).at("features").size() == 2000);
}
