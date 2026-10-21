#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeoJson.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
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

namespace
{

Annotation withProperties(const std::string& id, const std::string& label,
                          const std::string& classification, const std::string& notes = "")
{
    AnnotationProperties properties;
    properties.label = label;
    properties.classification = classification;
    properties.notes = notes;
    properties.color = Color{0xE6, 0x7E, 0x22, 0xFF};
    AnnotationMetadata metadata;
    metadata.author = "booritas";
    metadata.createdAt = at(1792490669LL);
    metadata.modifiedAt = at(1792490700LL);
    return Annotation(id, PointGeometry{PointF{1, 1}}, properties, metadata);
}

json propertiesOf(const Annotation& annotation)
{
    return exportAndParse({annotation}).at("features").at(0).at("properties");
}

} // namespace

TEST_CASE("unclassified annotations carry no classification", "[core][GeoJson]")
{
    for (const std::string& unclassified : {std::string(), std::string("   "),
                                            std::string("unclassified"), std::string("UNCLASSIFIED")}) {
        REQUIRE_FALSE(propertiesOf(withProperties("a1", "", unclassified)).contains("classification"));
    }
}

TEST_CASE("a classification is written as stored, with the annotation's colour", "[core][GeoJson]")
{
    const json properties = propertiesOf(withProperties("a1", "", "Tumor, invasive"));
    REQUIRE(properties.at("classification")
            == json{{"name", "Tumor, invasive"}, {"color", json::array({230, 126, 34})}});
}

TEST_CASE("every feature carries its own colour", "[core][GeoJson]")
{
    REQUIRE(propertiesOf(withProperties("a1", "", "Tumor")).at("color") == json::array({230, 126, 34}));
    REQUIRE(propertiesOf(withProperties("a1", "", "")).at("color") == json::array({230, 126, 34}));
}

TEST_CASE("an empty label is omitted and a label is the name", "[core][GeoJson]")
{
    REQUIRE_FALSE(propertiesOf(withProperties("a1", "", "Tumor")).contains("name"));
    REQUIRE(propertiesOf(withProperties("a1", "Region 1", "Tumor")).at("name") == "Region 1");
}

TEST_CASE("the slideio block carries every field, empty ones included", "[core][GeoJson]")
{
    const json block = propertiesOf(withProperties("a1", "", "")).at("slideio");
    REQUIRE(block.at("type") == "point");
    REQUIRE(block.at("notes") == "");
    REQUIRE(block.at("author") == "booritas");
    REQUIRE(block.at("createdAt") == formatIso8601Utc(at(1792490669LL)));
    REQUIRE(block.at("modifiedAt") == formatIso8601Utc(at(1792490700LL)));
    REQUIRE(block.at("color") == "#E67E22FF");
    REQUIRE(block.at("lineWidth") == 2.0);
    REQUIRE(std::abs(block.at("fillOpacity").get<double>() - 0.3) < 1e-6);

    const json arrow = exportAndParse({Annotation("a1", ArrowGeometry{PointF{0, 0}, PointF{1, 1}})});
    REQUIRE(arrow.at("features").at(0).at("properties").at("slideio").at("type") == "arrow");
    const json line =
        exportAndParse({Annotation("a1", PolygonGeometry{{PointF{0, 0}, PointF{1, 1}}, false})});
    REQUIRE(line.at("features").at(0).at("properties").at("slideio").at("type") == "polyline");
}

TEST_CASE("the collection records where it came from", "[core][GeoJson]")
{
    const json block = exportAndParse({}).at("slideio");
    REQUIRE(block.at("format") == "slideio-viewer annotation export");
    REQUIRE(block.at("formatVersion") == 1);
    REQUIRE(block.at("slide") == "D:/slides/Philips-1.tiff");
    REQUIRE(block.at("slideId") == "ec83fc2ebbfbc515");
    REQUIRE(block.at("scene") == 0);
    REQUIRE(block.at("exportedAt") == formatIso8601Utc(at(1792576800LL)));
    REQUIRE(block.at("coordinates")
            == "level-0 pixels of the scene; origin top-left, x right, y down");
}

TEST_CASE("pixel size is written only when known", "[core][GeoJson]")
{
    GeoJsonExportContext known = context();
    known.pixelSizeMicronsX = 0.25;
    known.pixelSizeMicronsY = 0.25;
    const json withSize = json::parse(annotationsToGeoJson({}, known, at(1792576800LL)).text);
    REQUIRE(withSize.at("slideio").at("pixelSizeMicrons") == json::array({0.25, 0.25}));

    REQUIRE_FALSE(exportAndParse({}).at("slideio").contains("pixelSizeMicrons"));
    GeoJsonExportContext half = context();
    half.pixelSizeMicronsX = 0.25;
    const json halfKnown = json::parse(annotationsToGeoJson({}, half, at(1792576800LL)).text);
    REQUIRE_FALSE(halfKnown.at("slideio").contains("pixelSizeMicrons"));
}

TEST_CASE("Greek and Cyrillic text is written as UTF-8, unescaped", "[core][GeoJson]")
{
    const std::string greek = "\xCE\x9D\xCE\xAD\xCE\xBA\xCF\x81\xCF\x89\xCF\x83\xCE\xB7";  // Νέκρωση
    const std::string cyrillic = "\xD0\x9E\xD0\xBF\xD1\x83\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C";  // Опухоль
    const GeoJsonExportResult result =
        annotationsToGeoJson({withProperties("a1", cyrillic, greek)}, context(), at(1792576800LL));
    REQUIRE(result.text.find(greek) != std::string::npos);
    REQUIRE(result.text.find(cyrillic) != std::string::npos);
    const json properties = json::parse(result.text).at("features").at(0).at("properties");
    REQUIRE(properties.at("classification").at("name") == greek);
    REQUIRE(properties.at("name") == cyrillic);
}

TEST_CASE("JSON-special characters round-trip", "[core][GeoJson]")
{
    const std::string label = "say \"hi\"\t\\x";
    const std::string notes = "line1\nline2";
    const json properties = propertiesOf(withProperties("a1", label, "", notes));
    REQUIRE(properties.at("name") == label);
    REQUIRE(properties.at("slideio").at("notes") == notes);
}

TEST_CASE("invalid UTF-8 does not abort the export", "[core][GeoJson]")
{
    const GeoJsonExportResult result = annotationsToGeoJson(
        {withProperties("a1", "", "", "ok\xFF\xFE" "end")}, context(), at(1792576800LL));
    REQUIRE(result.error.empty());
    const json properties = json::parse(result.text).at("features").at(0).at("properties");
    REQUIRE(properties.at("slideio").at("notes") == "ok\xEF\xBF\xBD\xEF\xBF\xBD" "end");
}

TEST_CASE("a Windows path with non-ASCII characters round-trips", "[core][GeoJson]")
{
    GeoJsonExportContext windows = context();
    windows.slidePath = "D:\\\xD1\x81\xD0\xBB\xD0\xB0\xD0\xB9\xD0\xB4\xD1\x8B\\Philips-1.tiff";  // D:\слайды\...
    const json file = json::parse(annotationsToGeoJson({}, windows, at(1792576800LL)).text);
    REQUIRE(file.at("slideio").at("slide") == windows.slidePath);
}

TEST_CASE("an unrepresentable timestamp fails the export", "[core][GeoJson]")
{
    AnnotationMetadata metadata;
    metadata.createdAt = at(-86400LL);  // before 1970
    metadata.modifiedAt = at(1792490700LL);
    const Annotation old("a7", PointGeometry{PointF{1, 1}}, AnnotationProperties{}, metadata);
    const GeoJsonExportResult result = annotationsToGeoJson({old}, context(), at(1792576800LL));
    REQUIRE(result.text.empty());
    REQUIRE(result.error.find("a7") != std::string::npos);
}
