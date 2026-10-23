#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeoJson.h"
#include "slideio/viewer/core/AnnotationDocument.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationSerialization.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

std::chrono::system_clock::time_point at(long long epochSeconds)
{
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(epochSeconds));
}

const auto kImportedAt = at(1792576800LL);

GeoJsonImportResult read(const std::string& text)
{
    return geoJsonToAnnotations(text, kImportedAt);
}

/// Reads `text`, requires it to be readable, and returns the result.
GeoJsonImportResult readOk(const std::string& text)
{
    GeoJsonImportResult result = read(text);
    REQUIRE(result.error.empty());
    return result;
}

/// One feature with `geometry` (JSON text) and `properties` (JSON object text).
std::string feature(const std::string& geometry, const std::string& properties = "{}",
                    const std::string& id = "")
{
    std::string text = R"({"type":"Feature",)";
    if (!id.empty()) {
        text += R"("id":)" + id + ",";
    }
    return text + R"("geometry":)" + geometry + R"(,"properties":)" + properties + "}";
}

std::string collection(const std::vector<std::string>& features, const std::string& slideio = "")
{
    std::string text = R"({"type":"FeatureCollection",)";
    if (!slideio.empty()) {
        text += R"("slideio":)" + slideio + ",";
    }
    text += R"("features":[)";
    for (std::size_t i = 0; i < features.size(); ++i) {
        text += (i == 0 ? "" : ",") + features[i];
    }
    return text + "]}";
}

/// The single annotation `geometry` imports as.
Annotation only(const std::string& geometry, const std::string& properties = "{}")
{
    const GeoJsonImportResult result = readOk(collection({feature(geometry, properties)}));
    REQUIRE(result.annotations.size() == 1);
    return result.annotations.front();
}

int skippedCount(const GeoJsonImportResult& r)
{
    int total = r.withHoles + r.unsupportedGeometry + r.invalidGeometry;
    for (const auto& [kind, count] : r.skippedObjectTypes) {
        total += count;
    }
    return total;
}

bool samePoints(const std::vector<PointF>& a, const std::vector<PointF>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y) {
            return false;
        }
    }
    return true;
}

bool sameColor(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

Annotation styled(const std::string& id, AnnotationGeometry geometry)
{
    AnnotationProperties properties;
    properties.label = "label " + id;
    properties.classification = "Tumor";
    properties.color = Color{1, 2, 3, 200};
    properties.lineWidth = 3.5f;
    properties.fillOpacity = 0.5f;
    properties.notes = "n\n\"q\"";
    AnnotationMetadata metadata;
    metadata.author = "a";
    metadata.createdAt = at(1700000000LL);
    metadata.modifiedAt = at(1700000600LL);
    return Annotation(id, std::move(geometry), properties, metadata);
}

} // namespace

TEST_CASE("every type round-trips through export and import", "[core][GeoJsonImport]")
{
    const std::vector<Annotation> original = {
        styled("r", RectangleGeometry{PointF{10, 20}, PointF{110, 70}}),
        styled("ri", RectangleGeometry{PointF{110, 70}, PointF{10, 20}}),
        styled("e", EllipseGeometry{PointF{-5.5, 3.25}, PointF{200.75, 90}}),
        styled("p", PolygonGeometry{{PointF{0, 0}, PointF{10, 0}, PointF{4, 9}}, true}),
        styled("l", PolygonGeometry{{PointF{1, 1}, PointF{2, 5}, PointF{7, 3}}, false}),
        styled("pt", PointGeometry{PointF{5, 6}}),
        styled("ar", ArrowGeometry{PointF{9, 8}, PointF{1, 2}}),
        styled("deg", PolygonGeometry{{PointF{0, 0}, PointF{0, 0}, PointF{5, 5}}, true}),
    };
    const GeoJsonExportResult exported = annotationsToGeoJson(original, GeoJsonExportContext{}, kImportedAt);
    REQUIRE(exported.error.empty());

    const GeoJsonImportResult imported = readOk(exported.text);
    REQUIRE(imported.annotations.size() == original.size());

    for (std::size_t i = 0; i < original.size(); ++i) {
        const Annotation& before = original[i];
        const Annotation& after = imported.annotations[i];
        INFO("annotation " << before.id());
        CHECK(after.id() == before.id());
        CHECK(after.type() == before.type());
        if (before.type() == AnnotationType::Ellipse) {
            const RectF a = before.boundingBox();
            const RectF b = after.boundingBox();
            CHECK(b.x == Catch::Approx(a.x).margin(1e-9));
            CHECK(b.y == Catch::Approx(a.y).margin(1e-9));
            CHECK(b.width == Catch::Approx(a.width).margin(1e-9));
            CHECK(b.height == Catch::Approx(a.height).margin(1e-9));
        } else {
            CHECK(samePoints(outline(after.geometry()).points, outline(before.geometry()).points));
        }
        CHECK(after.properties().label == before.properties().label);
        CHECK(after.properties().classification == before.properties().classification);
        CHECK(sameColor(after.properties().color, before.properties().color));
        CHECK(after.properties().lineWidth == before.properties().lineWidth);
        CHECK(after.properties().fillOpacity == before.properties().fillOpacity);
        CHECK(after.properties().notes == before.properties().notes);
        CHECK(after.metadata().author == before.metadata().author);
        CHECK(after.metadata().createdAt == before.metadata().createdAt);
        CHECK(after.metadata().modifiedAt == before.metadata().modifiedAt);
    }

    const auto* arrow = std::get_if<ArrowGeometry>(&imported.annotations[6].geometry());
    REQUIRE(arrow != nullptr);
    CHECK(arrow->tail.x == 9);
    CHECK(arrow->head.x == 1);

    const auto* degenerate = std::get_if<PolygonGeometry>(&imported.annotations[7].geometry());
    REQUIRE(degenerate != nullptr);
    CHECK(degenerate->vertices.size() == 3);
}

TEST_CASE("a zero-extent rectangle round-trips as a rectangle", "[core][GeoJsonImport]")
{
    const std::vector<Annotation> original = {
        styled("dot", RectangleGeometry{PointF{5, 5}, PointF{5, 5}}),
        styled("bar", RectangleGeometry{PointF{5, 5}, PointF{5, 20}}),
        styled("flat", RectangleGeometry{PointF{5, 5}, PointF{30, 5}}),
    };
    const GeoJsonExportResult exported = annotationsToGeoJson(original, GeoJsonExportContext{}, kImportedAt);
    REQUIRE(exported.error.empty());

    const GeoJsonImportResult imported = readOk(exported.text);
    REQUIRE(imported.annotations.size() == original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        INFO(original[i].id());
        const auto* before = std::get_if<RectangleGeometry>(&original[i].geometry());
        const auto* after = std::get_if<RectangleGeometry>(&imported.annotations[i].geometry());
        REQUIRE(after != nullptr);
        CHECK(after->topLeft.x == before->topLeft.x);
        CHECK(after->topLeft.y == before->topLeft.y);
        CHECK(after->bottomRight.x == before->bottomRight.x);
        CHECK(after->bottomRight.y == before->bottomRight.y);
    }
}

TEST_CASE("the top level may be a collection, a feature or an array", "[core][GeoJsonImport]")
{
    const std::string one = feature(R"({"type":"Point","coordinates":[1,2]})");
    CHECK(readOk(collection({one})).annotations.size() == 1);
    CHECK(readOk(one).annotations.size() == 1);
    CHECK(readOk("[" + one + "]").annotations.size() == 1);

    const GeoJsonImportResult empty = readOk("[]");
    CHECK(empty.annotations.empty());
    CHECK(empty.featuresRead == 0);
}

TEST_CASE("an unreadable file is an error and returns nothing", "[core][GeoJsonImport]")
{
    const std::vector<std::string> inputs = {
        "",
        "not json",
        R"({"type":"Point","coordinates":[1,2]})",
        "42",
        R"({"type":)",
        R"({"type":"FeatureCollection","features":7})",
        // nlohmann refuses an overflowing literal outright, so the file is unreadable.
        feature(R"({"type":"Point","coordinates":[1e999,2]})"),
        feature(R"({"type":"Point","coordinates":[1,2]})", "{\"name\":\"\xC3\x28\"}"),
    };
    for (const std::string& input : inputs) {
        INFO(input);
        GeoJsonImportResult result;
        REQUIRE_NOTHROW(result = read(input));
        CHECK_FALSE(result.error.empty());
        CHECK(result.annotations.empty());
    }
}

TEST_CASE("a byte-order mark is skipped", "[core][GeoJsonImport]")
{
    const std::string text = "\xEF\xBB\xBF" + collection({feature(R"({"type":"Point","coordinates":[1,2]})")});
    CHECK(readOk(text).annotations.size() == 1);
}

TEST_CASE("a QuPath file maps by geometry", "[core][GeoJsonImport]")
{
    const std::string props =
        R"({"objectType":"annotation","name":"Region",)"
        R"("classification":{"name":"Stroma","color":[1,1,1]},"color":[10,20,30]})";

    const Annotation box = only(R"({"type":"Polygon","coordinates":[[[0,0],[40,0],[40,30],[0,30],[0,0]]]})", props);
    const auto* rectangle = std::get_if<RectangleGeometry>(&box.geometry());
    REQUIRE(rectangle != nullptr);
    CHECK(rectangle->topLeft.x == 0);
    CHECK(rectangle->topLeft.y == 0);
    CHECK(rectangle->bottomRight.x == 40);
    CHECK(rectangle->bottomRight.y == 30);
    CHECK(box.properties().label == "Region");
    CHECK(box.properties().classification == "Stroma");
    CHECK(sameColor(box.properties().color, Color{10, 20, 30, 255}));

    const Annotation triangle = only(R"({"type":"Polygon","coordinates":[[[0,0],[40,0],[20,30],[0,0]]]})");
    const auto* polygon = std::get_if<PolygonGeometry>(&triangle.geometry());
    REQUIRE(polygon != nullptr);
    CHECK(polygon->closed);
    CHECK(polygon->vertices.size() == 3);

    const Annotation line = only(R"({"type":"LineString","coordinates":[[0,0],[5,5],[9,1]]})");
    const auto* polyline = std::get_if<PolygonGeometry>(&line.geometry());
    REQUIRE(polyline != nullptr);
    CHECK_FALSE(polyline->closed);
    CHECK(polyline->vertices.size() == 3);

    const Annotation marker = only(R"({"type":"Point","coordinates":[7.5,8.5]})");
    const auto* point = std::get_if<PointGeometry>(&marker.geometry());
    REQUIRE(point != nullptr);
    CHECK(point->position.x == 7.5);
    CHECK(point->position.y == 8.5);
}

TEST_CASE("multi-part geometries become one annotation per part", "[core][GeoJsonImport]")
{
    const GeoJsonImportResult points = readOk(collection({feature(
        R"({"type":"MultiPoint","coordinates":[[1,1],[2,2],[3,3],[4,4],[5,5]]})",
        R"({"classification":{"name":"Tumor"}})", R"("m")")}));
    REQUIRE(points.annotations.size() == 5);
    REQUIRE(points.idSeeds.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(std::holds_alternative<PointGeometry>(points.annotations[i].geometry()));
        CHECK(points.annotations[i].properties().classification == "Tumor");
        // Every part's id is minted by the caller from its seed.
        CHECK(points.annotations[i].id().empty());
        CHECK_FALSE(points.idSeeds[i].empty());
    }
    CHECK(points.featuresRead == 1);
    CHECK(points.featuresImported == 1);

    const GeoJsonImportResult polygons = readOk(collection({feature(
        R"({"type":"MultiPolygon","coordinates":[[[[0,0],[9,0],[5,7],[0,0]]],[[[20,0],[29,0],[25,7],[20,0]]]]})")}));
    REQUIRE(polygons.annotations.size() == 2);
    for (const Annotation& part : polygons.annotations) {
        const auto* poly = std::get_if<PolygonGeometry>(&part.geometry());
        REQUIRE(poly != nullptr);
        CHECK(poly->closed);
    }

    const GeoJsonImportResult lines = readOk(collection({feature(
        R"({"type":"MultiLineString","coordinates":[[[0,0],[1,1]],[[5,5],[6,6],[7,5]]]})")}));
    REQUIRE(lines.annotations.size() == 2);
    for (const Annotation& part : lines.annotations) {
        const auto* poly = std::get_if<PolygonGeometry>(&part.geometry());
        REQUIRE(poly != nullptr);
        CHECK_FALSE(poly->closed);
    }
}

TEST_CASE("holes, collections and null geometry are skipped under their reasons", "[core][GeoJsonImport]")
{
    const GeoJsonImportResult result = readOk(collection({
        feature(R"({"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]],[[2,2],[3,2],[3,3],[2,2]]]})"),
        feature(R"({"type":"MultiPolygon","coordinates":[[[[0,0],[9,0],[5,7],[0,0]]],)"
                R"([[[20,0],[29,0],[25,7],[20,0]],[[21,1],[22,1],[22,2],[21,1]]]]})"),
        feature(R"({"type":"GeometryCollection","geometries":[]})"),
        feature("null"),
    }));
    CHECK(result.annotations.empty());
    CHECK(result.withHoles == 2);
    CHECK(result.unsupportedGeometry == 2);
    CHECK(result.featuresRead == 4);
    CHECK(result.featuresImported == 0);
}

TEST_CASE("only annotations are imported", "[core][GeoJsonImport]")
{
    const std::string point = R"({"type":"Point","coordinates":[1,2]})";
    const GeoJsonImportResult result = readOk(collection({
        feature(point, R"({"objectType":"detection"})"),
        feature(point, R"({"objectType":"detection"})"),
        feature(point, R"({"objectType":"cell"})"),
        feature(point, R"({"objectType":"tile"})"),
        feature(point, R"({"objectType":"weird"})"),
        feature(point, R"({})"),
        feature(point, R"({"object_type":"annotation"})"),
        feature(point, R"({"objectType":"annotation"})"),
    }));
    CHECK(result.annotations.size() == 3);
    CHECK(result.skippedObjectTypes.at("detection") == 2);
    CHECK(result.skippedObjectTypes.at("cell") == 1);
    CHECK(result.skippedObjectTypes.at("tile") == 1);
    CHECK(result.skippedObjectTypes.at("weird") == 1);
}

TEST_CASE("nested child objects are counted and skipped", "[core][GeoJsonImport]")
{
    const std::string point = R"({"type":"Point","coordinates":[1,2]})";
    const std::string grandchild = feature(point, R"({"objectType":"detection"})");
    const std::string child = feature(point, R"({"objectType":"annotation","childObjects":[)" + grandchild + "]}");
    const GeoJsonImportResult result =
        readOk(collection({feature(point, R"({"objectType":"annotation","childObjects":[)" + child + "]}")}));
    CHECK(result.annotations.size() == 1);
    CHECK(result.nestedObjects == 2);
    // The grandchild detection is nested, not a top-level object to skip by
    // type: dropping objects early while parsing must not reach into children.
    CHECK(result.skippedObjectTypes.empty());
}

TEST_CASE("skipped objects are counted wherever the file puts them", "[core][GeoJsonImport]")
{
    // Non-annotation objects are dropped while the file is parsed, to keep a
    // 60 000-cell export from being held whole in memory; the counts must not
    // notice.
    const std::string point = R"({"type":"Point","coordinates":[1,2]})";
    const std::string child = feature(point, R"({"objectType":"annotation"})");
    const std::string detectionWithChild =
        feature(point, R"({"objectType":"detection","childObjects":[)" + child + "]}");

    SECTION("in a collection")
    {
        const GeoJsonImportResult result =
            readOk(collection({detectionWithChild, feature(point, R"({"objectType":"cell"})"), feature(point)}));
        CHECK(result.featuresRead == 3);
        CHECK(result.skippedObjectTypes.at("detection") == 1);
        CHECK(result.skippedObjectTypes.at("cell") == 1);
        CHECK(result.nestedObjects == 1);
        CHECK(result.annotations.size() == 1);
    }
    SECTION("in a top-level array")
    {
        const GeoJsonImportResult result =
            readOk("[" + detectionWithChild + "," + feature(point, R"({"object_type":"tile"})") + "]");
        CHECK(result.featuresRead == 2);
        CHECK(result.skippedObjectTypes.at("detection") == 1);
        CHECK(result.skippedObjectTypes.at("tile") == 1);
        CHECK(result.nestedObjects == 1);
        CHECK(result.annotations.empty());
    }
    SECTION("as the whole file")
    {
        const GeoJsonImportResult result = readOk(detectionWithChild);
        CHECK(result.featuresRead == 1);
        CHECK(result.skippedObjectTypes.at("detection") == 1);
        CHECK(result.nestedObjects == 1);
    }
    SECTION("a foreign member holding feature-like objects is not counted")
    {
        const GeoJsonImportResult result = readOk(
            R"({"type":"FeatureCollection","other":[)" + feature(point, R"({"objectType":"cell"})") +
            R"(],"features":[)" + feature(point) + "]}");
        CHECK(result.featuresRead == 1);
        CHECK(result.skippedObjectTypes.empty());
    }
}

TEST_CASE("a cancelled read stops and returns nothing", "[core][GeoJsonImport]")
{
    std::atomic<bool> cancel{true};
    const GeoJsonImportResult result =
        geoJsonToAnnotations(collection({feature(R"({"type":"Point","coordinates":[1,2]})")}), kImportedAt, &cancel);
    CHECK(result.cancelled);
    CHECK(result.error.empty());
    CHECK(result.annotations.empty());
    CHECK(result.featuresRead == 0);

    std::atomic<bool> carryOn{false};
    const GeoJsonImportResult read =
        geoJsonToAnnotations(collection({feature(R"({"type":"Point","coordinates":[1,2]})")}), kImportedAt, &carryOn);
    CHECK_FALSE(read.cancelled);
    CHECK(read.annotations.size() == 1);
}

TEST_CASE("invalid geometry is counted, never thrown", "[core][GeoJsonImport]")
{
    const std::vector<std::string> geometries = {
        R"({"type":"Point","coordinates":["1",2]})",
        R"({"type":"Point","coordinates":[null,2]})",
        R"({"type":"Point","coordinates":[true,2]})",
        R"({"type":"Point","coordinates":[[1],2]})",
        R"({"type":"Point","coordinates":[1]})",
        R"({"type":"Point","coordinates":"here"})",
        R"({"type":"Polygon","coordinates":[[[0,0],[5,5],[0,0]]]})",
        R"({"type":"Polygon","coordinates":[]})",
        R"({"type":"LineString","coordinates":[[0,0]]})",
        R"({"type":"MultiPoint","coordinates":[[1,1],[2,"x"]]})",
        R"({"type":"MultiPoint","coordinates":[]})",
    };
    for (const std::string& geometry : geometries) {
        INFO(geometry);
        GeoJsonImportResult result;
        REQUIRE_NOTHROW(result = read(collection({feature(geometry)})));
        CHECK(result.error.empty());
        CHECK(result.annotations.empty());
        CHECK(result.invalidGeometry == 1);
    }
}

TEST_CASE("a z coordinate is ignored", "[core][GeoJsonImport]")
{
    const Annotation marker = only(R"({"type":"Point","coordinates":[1,2,3]})");
    const auto* point = std::get_if<PointGeometry>(&marker.geometry());
    REQUIRE(point != nullptr);
    CHECK(point->position.x == 1);
    CHECK(point->position.y == 2);
}

TEST_CASE("an unclosed ring is accepted as it is", "[core][GeoJsonImport]")
{
    const Annotation shape = only(R"({"type":"Polygon","coordinates":[[[0,0],[4,0],[4,4]]]})");
    const auto* polygon = std::get_if<PolygonGeometry>(&shape.geometry());
    REQUIRE(polygon != nullptr);
    CHECK(polygon->vertices.size() == 3);
}

TEST_CASE("a slideio type that disagrees with the geometry is ignored", "[core][GeoJsonImport]")
{
    const Annotation line = only(R"({"type":"LineString","coordinates":[[0,0],[5,5],[9,1]]})",
                                 R"({"slideio":{"type":"arrow"}})");
    CHECK(line.type() == AnnotationType::Polyline);

    const Annotation triangle = only(R"({"type":"Polygon","coordinates":[[[0,0],[40,0],[20,30],[0,0]]]})",
                                     R"({"slideio":{"type":"rectangle"}})");
    CHECK(triangle.type() == AnnotationType::Polygon);

    const Annotation marker = only(R"({"type":"Point","coordinates":[1,2]})", R"({"slideio":{"type":"ellipse"}})");
    CHECK(marker.type() == AnnotationType::Point);

    const Annotation square = only(R"({"type":"Polygon","coordinates":[[[0,0],[40,0],[40,30],[0,30],[0,0]]]})",
                                   R"({"slideio":{"type":"polygon"}})");
    CHECK(square.type() == AnnotationType::Polygon);
}

TEST_CASE("an id written as a number is kept as its text", "[core][GeoJsonImport]")
{
    const GeoJsonImportResult result =
        readOk(collection({feature(R"({"type":"Point","coordinates":[1,2]})", "{}", "17")}));
    REQUIRE(result.annotations.size() == 1);
    CHECK(result.annotations[0].id() == "17");
}

TEST_CASE("classification names and the unclassified rule", "[core][GeoJsonImport]")
{
    const std::string point = R"({"type":"Point","coordinates":[1,2]})";
    CHECK(only(point, R"({"classification":{"names":["Tumor","Positive"]}})").properties().classification
          == "Tumor: Positive");
    CHECK(only(point, R"({"classification":{"name":"UNCLASSIFIED"}})").properties().classification.empty());
    CHECK(only(point, R"({"classification":{"name":"  "}})").properties().classification.empty());
    CHECK(only(point, "{\"classification\":{\"name\":\"\xCE\x9D\xCE\xAD\xCE\xBA\xCF\x81\xCF\x89\xCF\x83\xCE\xB7\"}}")
              .properties()
              .classification
          == "\xCE\x9D\xCE\xAD\xCE\xBA\xCF\x81\xCF\x89\xCF\x83\xCE\xB7");
    CHECK(only(point, "{\"classification\":{\"name\":\"\xD0\x9E\xD0\xBF\xD1\x83\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C\"}}")
              .properties()
              .classification
          == "\xD0\x9E\xD0\xBF\xD1\x83\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C");
    CHECK(only(point, R"({"name":42})").properties().label.empty());
}

TEST_CASE("colour precedence", "[core][GeoJsonImport]")
{
    const std::string point = R"({"type":"Point","coordinates":[1,2]})";
    CHECK(sameColor(only(point, R"({"color":[9,9,9],"slideio":{"color":"#01020304"}})").properties().color,
                    Color{1, 2, 3, 4}));
    CHECK(sameColor(only(point, R"({"color":[9,8,7],"colorRGB":1})").properties().color, Color{9, 8, 7, 255}));
    CHECK(sameColor(only(point, R"({"colorRGB":660510})").properties().color, Color{10, 20, 30, 255}));
    CHECK(sameColor(only(point, R"({"colorRGB":-16116706})").properties().color, Color{10, 20, 30, 255}));
    CHECK(sameColor(only(point, R"({"classification":{"name":"T","color":[4,5,6]}})").properties().color,
                    Color{4, 5, 6, 255}));
    CHECK(sameColor(only(point).properties().color, Color{0xE6, 0x7E, 0x22, 0xFF}));
    CHECK(sameColor(only(point, R"({"color":[9,8,7],"slideio":{"color":"#zz"}})").properties().color,
                    Color{9, 8, 7, 255}));
    CHECK(sameColor(only(point, R"({"color":[300,8,7]})").properties().color, Color{0xE6, 0x7E, 0x22, 0xFF}));
}

TEST_CASE("out-of-range style falls back to the defaults", "[core][GeoJsonImport]")
{
    const Annotation marker =
        only(R"({"type":"Point","coordinates":[1,2]})", R"({"slideio":{"lineWidth":5000,"fillOpacity":2}})");
    CHECK(marker.properties().lineWidth == 2.0f);
    CHECK(marker.properties().fillOpacity == 0.3f);
}

TEST_CASE("missing metadata takes the import instant and no author", "[core][GeoJsonImport]")
{
    const Annotation plain = only(R"({"type":"Point","coordinates":[1,2]})");
    CHECK(plain.metadata().createdAt == kImportedAt);
    CHECK(plain.metadata().modifiedAt == kImportedAt);
    CHECK(plain.metadata().author.empty());

    const Annotation malformed = only(R"({"type":"Point","coordinates":[1,2]})",
                                      R"({"slideio":{"createdAt":"yesterday","modifiedAt":17}})");
    CHECK(malformed.metadata().createdAt == kImportedAt);
    CHECK(malformed.metadata().modifiedAt == kImportedAt);

    const auto subSecond = kImportedAt + std::chrono::milliseconds(700);
    const GeoJsonImportResult truncated =
        geoJsonToAnnotations(collection({feature(R"({"type":"Point","coordinates":[1,2]})")}), subSecond);
    REQUIRE(truncated.annotations.size() == 1);
    CHECK(truncated.annotations[0].metadata().createdAt == kImportedAt);
}

TEST_CASE("provenance is returned when the collection carries it", "[core][GeoJsonImport]")
{
    const std::string point = feature(R"({"type":"Point","coordinates":[1,2]})");
    const GeoJsonImportResult with =
        readOk(collection({point}, R"({"slide":"D:/s/a.svs","slideId":"abc","scene":1})"));
    CHECK(with.provenance.present);
    CHECK(with.provenance.slide == "D:/s/a.svs");
    CHECK(with.provenance.slideId == "abc");
    CHECK(with.provenance.scene == 1);

    const GeoJsonImportResult without = readOk(collection({point}));
    CHECK_FALSE(without.provenance.present);
    CHECK(without.provenance.scene == -1);
}

TEST_CASE("every feature is accounted for", "[core][GeoJsonImport]")
{
    const std::string point = R"({"type":"Point","coordinates":[1,2]})";
    const GeoJsonImportResult result = readOk(collection({
        feature(R"({"type":"MultiPoint","coordinates":[[1,1],[2,2]]})"),
        feature(R"({"type":"MultiLineString","coordinates":[[[0,0],[1,1]],[[5,5],[6,6]]]})"),
        feature(point, R"({"objectType":"detection"})"),
        feature(point, R"({"objectType":"detection"})"),
        feature(point, R"({"objectType":"cell"})"),
        feature(R"({"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,0]],[[2,2],[3,2],[3,3],[2,2]]]})"),
        feature("null"),
        feature(R"({"type":"Point","coordinates":[1]})"),
        feature(R"({"type":"LineString","coordinates":[]})"),
        feature(point),
        feature(point),
        feature(point),
    }));
    CHECK(result.featuresRead == 12);
    CHECK(result.featuresImported == 5);
    CHECK(result.annotations.size() == 7);
    CHECK(result.featuresRead == result.featuresImported + skippedCount(result));
}

namespace
{

std::vector<std::string> sortedSeeds(const GeoJsonImportResult& result)
{
    std::vector<std::string> seeds = result.idSeeds;
    std::sort(seeds.begin(), seeds.end());
    return seeds;
}

GeoJsonImportResult multiPoint(const std::string& coordinates, const std::string& id = R"("m")")
{
    return readOk(collection({feature(R"({"type":"MultiPoint","coordinates":)" + coordinates + "}", "{}", id)}));
}

} // namespace

TEST_CASE("the parts of a multi-part feature are seeded from its id and their own coordinates",
          "[core][GeoJsonImport]")
{
    // QuPath writes a whole Points class as ONE feature with one id, and keeps
    // that id when the class is edited. A part's identity is therefore the
    // feature's id plus where the part is -- never its position in the list,
    // which shifts when a point is deleted.
    const GeoJsonImportResult abc = multiPoint("[[1,1],[2,2],[3,3]]");
    REQUIRE(abc.idSeeds.size() == 3);
    for (const Annotation& part : abc.annotations) {
        CHECK(part.id().empty());
    }
    CHECK(sortedSeeds(abc) == sortedSeeds(multiPoint("[[1,1],[2,2],[3,3]]")));
    CHECK(sortedSeeds(abc) == sortedSeeds(multiPoint("[[3,3],[1,1],[2,2]]")));
    CHECK(std::set<std::string>(abc.idSeeds.begin(), abc.idSeeds.end()).size() == 3);

    const GeoJsonImportResult bcd = multiPoint("[[2,2],[3,3],[4,4]]");
    std::vector<std::string> shared;
    const std::vector<std::string> a = sortedSeeds(abc);
    const std::vector<std::string> b = sortedSeeds(bcd);
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(shared));
    CHECK(shared.size() == 2);

    const GeoJsonImportResult otherClass = multiPoint("[[1,1],[2,2],[3,3]]", R"("n")");
    CHECK(sortedSeeds(otherClass) != sortedSeeds(abc));

    const GeoJsonImportResult twice = multiPoint("[[1,1],[1,1]]");
    REQUIRE(twice.idSeeds.size() == 2);
    CHECK(twice.idSeeds[0] != twice.idSeeds[1]);

    const GeoJsonImportResult polygons = readOk(collection({feature(
        R"({"type":"MultiPolygon","coordinates":[[[[0,0],[9,0],[5,7],[0,0]]],[[[0,0],[9,0],[5,8],[0,0]]]]})",
        "{}", R"("p")")}));
    REQUIRE(polygons.idSeeds.size() == 2);
    CHECK(polygons.idSeeds[0] != polygons.idSeeds[1]);
}

TEST_CASE("only the parts of an identified multi-part feature are seeded", "[core][GeoJsonImport]")
{
    const GeoJsonImportResult anonymous = multiPoint("[[1,1],[2,2]]", "");
    REQUIRE(anonymous.idSeeds.size() == 2);
    CHECK(anonymous.idSeeds[0].empty());
    CHECK(anonymous.idSeeds[1].empty());

    const GeoJsonImportResult single =
        readOk(collection({feature(R"({"type":"Point","coordinates":[1,2]})", "{}", R"("p")")}));
    REQUIRE(single.annotations.size() == 1);
    CHECK(single.annotations[0].id() == "p");
    REQUIRE(single.idSeeds.size() == 1);
    CHECK(single.idSeeds[0].empty());
}

TEST_CASE("a shape whose extent overflows is invalid geometry", "[core][GeoJsonImport]")
{
    // Every corner is a finite number, but the width is not: the outline, the
    // bounding box and measure() all subtract, and would paint, hit-test and
    // export infinities and NaNs.
    const std::string ring = R"([[[-1.7e308,-1.7e308],[1.7e308,-1.7e308],)"
                             R"([1.7e308,1.7e308],[-1.7e308,1.7e308],[-1.7e308,-1.7e308]]])";
    const std::string line = R"([[-1.7e308,0],[1.7e308,0]])";
    const GeoJsonImportResult result = readOk(collection({
        feature(R"({"type":"Polygon","coordinates":)" + ring + "}"),
        feature(R"({"type":"Polygon","coordinates":)" + ring + "}", R"({"slideio":{"type":"ellipse"}})"),
        feature(R"({"type":"Polygon","coordinates":[[[-1.7e308,0],[1.7e308,0],[0,5],[-1.7e308,0]]]})"),
        feature(R"({"type":"LineString","coordinates":)" + line + "}"),
        feature(R"({"type":"LineString","coordinates":)" + line + "}", R"({"slideio":{"type":"arrow"}})"),
    }));
    CHECK(result.annotations.empty());
    CHECK(result.invalidGeometry == 5);
}

TEST_CASE("a large but finite shape imports and exports", "[core][GeoJsonImport]")
{
    const std::string ring = R"([[[-8e307,-8e307],[8e307,-8e307],[8e307,8e307],[-8e307,8e307],[-8e307,-8e307]]])";
    const GeoJsonImportResult result = readOk(collection({
        feature(R"({"type":"Polygon","coordinates":)" + ring + "}", "{}", R"("r")"),
        feature(R"({"type":"Polygon","coordinates":)" + ring + "}", R"({"slideio":{"type":"ellipse"}})", R"("e")"),
    }));
    REQUIRE(result.annotations.size() == 2);
    CHECK(result.annotations[0].type() == AnnotationType::Rectangle);
    CHECK(result.annotations[1].type() == AnnotationType::Ellipse);

    AnnotationDocument document;
    document.slideId = "s";
    document.annotations = result.annotations;
    CHECK_FALSE(serializeAnnotationDocument(document).empty());
    CHECK(annotationsToGeoJson(result.annotations, GeoJsonExportContext{}, kImportedAt).error.empty());
}

TEST_CASE("nesting deeper than any GeoJSON file needs is refused, not a crash", "[core][GeoJsonImport]")
{
    constexpr int kDepth = 20000;
    std::string children;
    for (int i = 0; i < kDepth; ++i) {
        children += R"({"type":"Feature","geometry":null,"properties":{"childObjects":[)";
    }
    for (int i = 0; i < kDepth; ++i) {
        children += "]}}";
    }
    GeoJsonImportResult nested;
    REQUIRE_NOTHROW(nested = read(collection(
        {feature(R"({"type":"Point","coordinates":[1,2]})", R"({"childObjects":[)" + children + "]}")})));
    CHECK_FALSE(nested.error.empty());
    CHECK(nested.annotations.empty());

    const std::string deep = std::string(kDepth, '[') + std::string(kDepth, ']');
    GeoJsonImportResult coordinates;
    REQUIRE_NOTHROW(coordinates = read(collection({feature(R"({"type":"Polygon","coordinates":)" + deep + "}")})));
    CHECK_FALSE(coordinates.error.empty());
}

TEST_CASE("nesting as deep as a QuPath hierarchy is read", "[core][GeoJsonImport]")
{
    constexpr int kLevels = 30;
    std::string children;
    for (int i = 0; i < kLevels; ++i) {
        children += R"({"type":"Feature","geometry":null,"properties":{"childObjects":[)";
    }
    for (int i = 0; i < kLevels; ++i) {
        children += "]}}";
    }
    const GeoJsonImportResult nested = readOk(
        collection({feature(R"({"type":"Point","coordinates":[1,2]})", R"({"childObjects":[)" + children + "]}")}));
    CHECK(nested.annotations.size() == 1);
    CHECK(nested.nestedObjects == kLevels);
}

TEST_CASE("a large detection export is skipped whole", "[core][GeoJsonImport]")
{
    const std::string detection = feature(R"({"type":"Point","coordinates":[1,2]})", R"({"objectType":"detection"})");
    std::string text = R"({"type":"FeatureCollection","features":[)";
    for (int i = 0; i < 20000; ++i) {
        text += (i == 0 ? "" : ",") + detection;
    }
    text += "]}";
    const GeoJsonImportResult result = readOk(text);
    CHECK(result.annotations.empty());
    CHECK(result.skippedObjectTypes.at("detection") == 20000);
}

// Hidden: runs only when asked for by name or tag, with the file in
// SLIDEIO_GEOJSON_BENCHMARK. Times the reader on a real-sized file -- the
// measurement behind docs/technical-debt.md's TD-2.
TEST_CASE("benchmark: reading a large GeoJSON file", "[.benchmark]")
{
    const char* path = std::getenv("SLIDEIO_GEOJSON_BENCHMARK");
    if (path == nullptr) {
        SKIP("set SLIDEIO_GEOJSON_BENCHMARK to a GeoJSON file");
    }
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto start = std::chrono::steady_clock::now();
    const GeoJsonImportResult result = geoJsonToAnnotations(text, kImportedAt);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    int skipped = 0;
    for (const auto& [kind, count] : result.skippedObjectTypes) {
        skipped += count;
    }
    WARN("read " << text.size() << " bytes in " << seconds << " s: " << result.featuresRead << " features, "
                 << result.annotations.size() << " annotations, " << skipped << " skipped by type");
    CHECK(result.error.empty());
}
