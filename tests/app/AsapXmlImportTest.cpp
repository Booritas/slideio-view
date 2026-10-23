#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationImport.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AsapXmlImport.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace slideio::viewer;
using app::asapXmlToAnnotations;

namespace
{

const auto kImportedAt = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(1792576800LL));

std::string asap(const std::string& annotations, const std::string& groups = "")
{
    return "<?xml version=\"1.0\"?>\n<ASAP_Annotations>\n<Annotations>\n" + annotations
         + "</Annotations>\n<AnnotationGroups>\n" + groups + "</AnnotationGroups>\n</ASAP_Annotations>\n";
}

/// One <Annotation> with coordinates (x, y) pairs, Order attributes in sequence.
std::string annotation(const std::string& type, const std::vector<std::pair<std::string, std::string>>& points,
                       const std::string& group = "None", const std::string& extra = "",
                       const std::string& name = "Annotation 0")
{
    std::string xml = "<Annotation Name=\"" + name + "\" Type=\"" + type + "\" PartOfGroup=\"" + group + "\" "
                    + extra + "><Coordinates>";
    for (std::size_t i = 0; i < points.size(); ++i) {
        xml += "<Coordinate Order=\"" + std::to_string(i) + "\" X=\"" + points[i].first + "\" Y=\""
             + points[i].second + "\"/>";
    }
    return xml + "</Coordinates></Annotation>\n";
}

std::string group(const std::string& name, const std::string& color, const std::string& parent = "None")
{
    return "<Group Name=\"" + name + "\" PartOfGroup=\"" + parent + "\" Color=\"" + color
         + "\"><Attributes/></Group>\n";
}

core::AnnotationImportResult readOk(const std::string& text)
{
    core::AnnotationImportResult result = asapXmlToAnnotations(text, kImportedAt);
    INFO(result.error);
    REQUIRE(result.error.empty());
    REQUIRE_FALSE(result.cancelled);
    REQUIRE(result.idSeeds.size() == result.annotations.size());
    return result;
}

core::Annotation only(const std::string& annotationXml, const std::string& groups = "")
{
    const core::AnnotationImportResult result = readOk(asap(annotationXml, groups));
    REQUIRE(result.annotations.size() == 1);
    return result.annotations.front();
}

const std::vector<std::pair<std::string, std::string>> kSquare = {
    {"10", "20"}, {"40", "20"}, {"40", "50"}, {"10", "50"}};

/// A copy of the shape, so nothing points into a temporary annotation.
template<typename Shape>
std::optional<Shape> shapeOf(const core::Annotation& annotation)
{
    const auto* shape = std::get_if<Shape>(&annotation.geometry());
    return shape != nullptr ? std::optional<Shape>(*shape) : std::nullopt;
}

int skipCount(const core::AnnotationImportResult& r)
{
    int total = r.invalidGeometry;
    for (const auto& [type, count] : r.unsupportedTypes) {
        total += count;
    }
    return total;
}

} // namespace

TEST_CASE("each ASAP type maps as the spec's table says", "[app][AsapXmlImport]")
{
    const auto dot = shapeOf<core::PointGeometry>(only(annotation("Dot", {{"5", "6"}})));
    REQUIRE(dot.has_value());
    CHECK(dot->position.x == 5);
    CHECK(dot->position.y == 6);

    const core::AnnotationImportResult set =
        readOk(asap(annotation("PointSet", {{"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}})));
    REQUIRE(set.annotations.size() == 4);
    for (const core::Annotation& part : set.annotations) {
        CHECK(std::holds_alternative<core::PointGeometry>(part.geometry()));
    }
    CHECK(set.featuresRead == 1);
    CHECK(set.featuresImported == 1);

    const auto polygon = shapeOf<core::PolygonGeometry>(
        only(annotation("Polygon", {{"0", "0"}, {"9", "0"}, {"9", "9"}, {"4", "12"}, {"0", "9"}})));
    REQUIRE(polygon.has_value());
    CHECK(polygon->closed);
    CHECK(polygon->vertices.size() == 5);

    CHECK(std::holds_alternative<core::RectangleGeometry>(only(annotation("Rectangle", kSquare)).geometry()));
    const auto rotated = shapeOf<core::PolygonGeometry>(
        only(annotation("Rectangle", {{"0", "5"}, {"5", "0"}, {"10", "5"}, {"5", "10"}})));
    REQUIRE(rotated.has_value());
    CHECK(rotated->closed);

    const core::AnnotationImportResult spline = readOk(
        asap(annotation("Spline", {{"0", "0"}, {"5", "-2"}, {"10", "0"}, {"12", "5"}, {"10", "10"}, {"0", "10"}})));
    REQUIRE(spline.annotations.size() == 1);
    const auto* splinePolygon = std::get_if<core::PolygonGeometry>(&spline.annotations[0].geometry());
    REQUIRE(splinePolygon != nullptr);
    CHECK(splinePolygon->closed);
    CHECK(splinePolygon->vertices.size() == 6);
    CHECK(spline.splinesApproximated == 1);

    const auto ruler =
        shapeOf<core::PolygonGeometry>(only(annotation("Measurement", {{"0", "0"}, {"30", "40"}})));
    REQUIRE(ruler.has_value());
    CHECK_FALSE(ruler->closed);
    CHECK(ruler->vertices.size() == 2);
}

TEST_CASE("coordinates follow Order, or document order without it", "[app][AsapXmlImport]")
{
    const std::string shuffled = "<Annotation Name=\"a\" Type=\"Polygon\" PartOfGroup=\"None\"><Coordinates>"
                                 "<Coordinate Order=\"2\" X=\"9\" Y=\"9\"/><Coordinate Order=\"0\" X=\"0\" Y=\"0\"/>"
                                 "<Coordinate Order=\"1\" X=\"9\" Y=\"0\"/></Coordinates></Annotation>";
    const auto sorted = shapeOf<core::PolygonGeometry>(only(shuffled));
    REQUIRE(sorted.has_value());
    CHECK(sorted->vertices[0].x == 0);
    CHECK(sorted->vertices[1].x == 9);
    CHECK(sorted->vertices[1].y == 0);
    CHECK(sorted->vertices[2].y == 9);

    const std::string unordered = "<Annotation Name=\"a\" Type=\"Polygon\" PartOfGroup=\"None\"><Coordinates>"
                                  "<Coordinate X=\"9\" Y=\"9\"/><Coordinate X=\"0\" Y=\"0\"/>"
                                  "<Coordinate X=\"9\" Y=\"0\"/></Coordinates></Annotation>";
    const auto asWritten = shapeOf<core::PolygonGeometry>(only(unordered));
    REQUIRE(asWritten.has_value());
    CHECK(asWritten->vertices[0].x == 9);
    CHECK(asWritten->vertices[1].x == 0);
}

TEST_CASE("decimal commas and other numbers", "[app][AsapXmlImport]")
{
    const auto position = [](const std::string& x) {
        const core::AnnotationImportResult result = readOk(asap(annotation("Dot", {{x, "0"}})));
        return result;
    };
    const auto x = [&position](const std::string& text) {
        const core::AnnotationImportResult result = position(text);
        REQUIRE(result.annotations.size() == 1);
        return std::get<core::PointGeometry>(result.annotations[0].geometry()).position.x;
    };
    CHECK(x("1234,5") == 1234.5);
    CHECK(x("1.5e3") == 1500.0);
    CHECK(x(" +7 ") == 7.0);
    CHECK(x("-2.25") == -2.25);
    for (const std::string& bad : {"1,234.5", "abc", "nan", "inf", "", "1e999", "12abc"}) {
        INFO(bad);
        const core::AnnotationImportResult result = position(bad);
        CHECK(result.annotations.empty());
        CHECK(result.invalidGeometry == 1);
    }
}

TEST_CASE("too few coordinates are invalid; an unknown type is skipped by name", "[app][AsapXmlImport]")
{
    const core::AnnotationImportResult result = readOk(asap(
        annotation("Polygon", {{"0", "0"}, {"1", "1"}}) + annotation("Measurement", {{"0", "0"}})
        + annotation("Dot", {}) + annotation("Spline", {{"0", "0"}, {"1", "1"}})
        + annotation("Ellipse", {{"0", "0"}, {"5", "5"}}) + annotation("Ellipse", {{"0", "0"}, {"6", "6"}})
        + annotation("Polygon", {{"-1.7e308", "0"}, {"1.7e308", "0"}, {"0", "5"}})));
    CHECK(result.annotations.empty());
    CHECK(result.invalidGeometry == 5);
    CHECK(result.unsupportedTypes.at("Ellipse") == 2);
    CHECK(result.splinesApproximated == 0);
}

TEST_CASE("groups become classifications", "[app][AsapXmlImport]")
{
    const std::string groups = group("Tumor", "#FF0000") + group("Child", "#00FF00", "Parent")
                             + group("Parent", "#0000FF");
    const core::AnnotationImportResult result = readOk(asap(
        annotation("Dot", {{"1", "1"}}, "Tumor") + annotation("Dot", {{"2", "2"}}, "None")
            + annotation("Dot", {{"3", "3"}}, "") + annotation("Dot", {{"4", "4"}}, "Child")
            + annotation("Dot", {{"5", "5"}}, "Undeclared"),
        groups));
    REQUIRE(result.annotations.size() == 5);
    CHECK(result.annotations[0].properties().classification == "Tumor");
    CHECK(result.annotations[1].properties().classification.empty());
    CHECK(result.annotations[2].properties().classification.empty());
    CHECK(result.annotations[3].properties().classification == "Child");
    CHECK(result.annotations[4].properties().classification == "Undeclared");
}

TEST_CASE("colour comes from the annotation, then its group, then the default", "[app][AsapXmlImport]")
{
    const std::string groups = group("Tumor", "#00FF00");
    const core::AnnotationImportResult result = readOk(asap(
        annotation("Dot", {{"1", "1"}}, "Tumor", "Color=\"#F4FA58\"") + annotation("Dot", {{"2", "2"}}, "Tumor")
            + annotation("Dot", {{"3", "3"}}, "Tumor", "Color=\"#zz\"") + annotation("Dot", {{"4", "4"}}, "None"),
        groups));
    REQUIRE(result.annotations.size() == 4);
    const auto rgb = [&result](std::size_t i) {
        const core::Color c = result.annotations[i].properties().color;
        return std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," + std::to_string(c.a);
    };
    CHECK(rgb(0) == "244,250,88,255");
    CHECK(rgb(1) == "0,255,0,255");
    CHECK(rgb(2) == "0,255,0,255");
    const core::Color fallback = core::AnnotationProperties{}.color;
    CHECK(rgb(3) == std::to_string(fallback.r) + "," + std::to_string(fallback.g) + "," + std::to_string(fallback.b)
                        + "," + std::to_string(fallback.a));
}

TEST_CASE("automatic names are not labels", "[app][AsapXmlImport]")
{
    CHECK(only(annotation("Dot", {{"1", "1"}}, "None", "", "Annotation 12")).properties().label.empty());
    CHECK(only(annotation("Dot", {{"1", "1"}}, "None", "", "Annotation 12b")).properties().label == "Annotation 12b");
    CHECK(only(annotation("Dot", {{"1", "1"}}, "None", "", "Tumour margin")).properties().label == "Tumour margin");
}

TEST_CASE("every annotation is seeded from its content", "[app][AsapXmlImport]")
{
    const std::string file = asap(annotation("Polygon", kSquare, "Tumor") + annotation("Polygon", kSquare, "Tumor")
                                  + annotation("Polygon", kSquare, "Stroma"));
    const core::AnnotationImportResult first = readOk(file);
    const core::AnnotationImportResult second = readOk(file);
    REQUIRE(first.annotations.size() == 3);
    for (const core::Annotation& annotation : first.annotations) {
        CHECK(annotation.id().empty());
    }
    CHECK(first.idSeeds == second.idSeeds);
    CHECK(std::set<std::string>(first.idSeeds.begin(), first.idSeeds.end()).size() == 3);

    app::AnnotationModel model;
    core::AnnotationImportResult once = readOk(file);
    model.insertMany(app::planImport(std::move(once.annotations), std::move(once.idSeeds), model).toAdd);
    REQUIRE(model.annotations().size() == 3);
    core::AnnotationImportResult twice = readOk(file);
    const app::ImportPlan again = app::planImport(std::move(twice.annotations), std::move(twice.idSeeds), model);
    CHECK(again.toAdd.empty());
    CHECK(again.alreadyPresent == 3);
}

TEST_CASE("each point of a PointSet is identified by where it is", "[app][AsapXmlImport]")
{
    // ASAP counts cells as one PointSet. Editing it -- deleting the first
    // point, adding one -- must re-import as the one new point, not as a whole
    // new set beside the old one.
    app::AnnotationModel model;
    core::AnnotationImportResult first =
        readOk(asap(annotation("PointSet", {{"1", "1"}, {"2", "2"}, {"3", "3"}}, "Ki67")));
    model.insertMany(app::planImport(std::move(first.annotations), std::move(first.idSeeds), model).toAdd);
    REQUIRE(model.annotations().size() == 3);

    core::AnnotationImportResult edited =
        readOk(asap(annotation("PointSet", {{"2", "2"}, {"3", "3"}, {"4", "4"}}, "Ki67")));
    const app::ImportPlan plan = app::planImport(std::move(edited.annotations), std::move(edited.idSeeds), model);
    REQUIRE(plan.toAdd.size() == 1);
    CHECK(plan.alreadyPresent == 2);
    CHECK(plan.toAdd[0].boundingBox().x == 4.0);

    // Two points at the same place are still two counts.
    const core::AnnotationImportResult twice = readOk(asap(annotation("PointSet", {{"1", "1"}, {"1", "1"}})));
    REQUIRE(twice.idSeeds.size() == 2);
    CHECK(twice.idSeeds[0] != twice.idSeeds[1]);
}

TEST_CASE("a large PointSet is read without copying the set into every point", "[app][AsapXmlImport]")
{
    std::vector<std::pair<std::string, std::string>> cells;
    for (int i = 0; i < 50000; ++i) {
        cells.emplace_back(std::to_string(i % 500 * 4), std::to_string(i / 500 * 4));
    }
    const core::AnnotationImportResult result = readOk(asap(annotation("PointSet", cells, "Ki67")));
    REQUIRE(result.annotations.size() == 50000);
    std::size_t longest = 0;
    for (const std::string& seed : result.idSeeds) {
        longest = std::max(longest, seed.size());
    }
    CHECK(longest < 64);
}

TEST_CASE("an annotation nested in another is counted, not lost", "[app][AsapXmlImport]")
{
    const std::string nested =
        "<ASAP_Annotations><Annotations>"
        "<Annotation Name=\"outer\" Type=\"Dot\" PartOfGroup=\"A\"><Coordinates>"
        "<Coordinate Order=\"0\" X=\"1\" Y=\"1\"/></Coordinates>"
        "<Annotation Name=\"inner\" Type=\"Dot\" PartOfGroup=\"B\"><Coordinates>"
        "<Coordinate Order=\"0\" X=\"2\" Y=\"2\"/></Coordinates></Annotation>"
        "</Annotation></Annotations></ASAP_Annotations>";
    const core::AnnotationImportResult result = readOk(nested);
    CHECK(result.featuresRead == 2);
    REQUIRE(result.annotations.size() == 2);
    std::set<std::string> groups;
    for (const core::Annotation& annotation : result.annotations) {
        groups.insert(annotation.properties().classification);
    }
    CHECK(groups == std::set<std::string>{"A", "B"});
}

TEST_CASE("a UTF-16 ASAP file is read", "[app][AsapXmlImport]")
{
    const std::string utf8 =
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?><ASAP_Annotations><Annotations>"
        + annotation("Dot", {{"5", "6"}}, "Tumor") + "</Annotations></ASAP_Annotations>";
    std::string utf16 = "\xFF\xFE";
    for (const char c : utf8) {
        utf16 += c;
        utf16 += '\0';
    }
    REQUIRE(core::detectAnnotationFileFormat(utf16) == core::AnnotationFileFormat::AsapXml);
    const core::AnnotationImportResult result = readOk(utf16);
    REQUIRE(result.annotations.size() == 1);
    CHECK(result.annotations[0].properties().classification == "Tumor");
}

TEST_CASE("Order tolerates what X and Y tolerate", "[app][AsapXmlImport]")
{
    const std::string padded = "<Annotation Name=\"a\" Type=\"Polygon\" PartOfGroup=\"None\"><Coordinates>"
                               "<Coordinate Order=\" 2 \" X=\"9\" Y=\"9\"/><Coordinate Order=\"+0\" X=\"0\" Y=\"0\"/>"
                               "<Coordinate Order=\"1\" X=\"9\" Y=\"0\"/></Coordinates></Annotation>";
    const auto sorted = shapeOf<core::PolygonGeometry>(only(padded));
    REQUIRE(sorted.has_value());
    CHECK(sorted->vertices[0].x == 0);
    CHECK(sorted->vertices[1].y == 0);
    CHECK(sorted->vertices[2].y == 9);
}

TEST_CASE("a padded group name finds its group's colour", "[app][AsapXmlImport]")
{
    const core::Annotation dot =
        only(annotation("Dot", {{"1", "1"}}, " Tumor "),
             "<Group Name=\"Tumor \" PartOfGroup=\"None\" Color=\"#00FF00\"><Attributes/></Group>");
    CHECK(dot.properties().classification == "Tumor");
    CHECK(dot.properties().color.g == 255);
    CHECK(dot.properties().color.r == 0);
}

TEST_CASE("a seed stays small however many coordinates it covers", "[app][AsapXmlImport]")
{
    std::vector<std::pair<std::string, std::string>> trace;
    for (int i = 0; i < 5000; ++i) {
        trace.emplace_back(std::to_string(1000 + i), std::to_string(i % 2 == 0 ? 0 : 7));
    }
    const core::AnnotationImportResult result = readOk(asap(annotation("Polygon", trace)));
    REQUIRE(result.idSeeds.size() == 1);
    CHECK(result.idSeeds[0].size() < 64);
}

TEST_CASE("a file that is not ASAP fails and changes nothing", "[app][AsapXmlImport]")
{
    const std::string laughs =
        "<?xml version=\"1.0\"?><!DOCTYPE lolz [<!ENTITY lol \"lol\">"
        "<!ENTITY lol1 \"&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;\">"
        "<!ENTITY lol2 \"&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;\">"
        "<!ENTITY lol3 \"&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;\">]>"
        "<ASAP_Annotations><Annotations><Annotation Name=\"&lol3;\" Type=\"Dot\" PartOfGroup=\"None\">"
        "<Coordinates><Coordinate Order=\"0\" X=\"1\" Y=\"1\"/></Coordinates></Annotation></Annotations>"
        "</ASAP_Annotations>";
    const std::string external =
        "<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY ext SYSTEM \"file:///C:/Windows/win.ini\">]>"
        "<ASAP_Annotations><Annotations><Annotation Name=\"&ext;\" Type=\"Dot\" PartOfGroup=\"None\">"
        "<Coordinates><Coordinate Order=\"0\" X=\"1\" Y=\"1\"/></Coordinates></Annotation></Annotations>"
        "</ASAP_Annotations>";
    for (const std::string& text : std::vector<std::string>{"not xml", "<Other/>",
                                                            "<ASAP_Annotations><Annotations><Annotation",
                                                            laughs, external}) {
        INFO(text.substr(0, 60));
        core::AnnotationImportResult result;
        REQUIRE_NOTHROW(result = asapXmlToAnnotations(text, kImportedAt));
        CHECK_FALSE(result.error.empty());
        CHECK(result.annotations.empty());
    }
}

TEST_CASE("files real ASAP might write are read", "[app][AsapXmlImport]")
{
    const std::string reordered =
        "\xEF\xBB\xBF<?xml version=\"1.0\"?>\r\n<ASAP_Annotations>\r\n"
        "<AnnotationGroups><Group Color=\"#123456\" PartOfGroup=\"None\" Name=\"Tumor\"><Attributes/></Group>"
        "</AnnotationGroups>\r\n<Annotations>\r\n"
        "<Annotation PartOfGroup=\"Tumor\" Color=\"#F4FA58\" Type=\"Dot\" Name=\"x\" Extra=\"ignored\">"
        "<Coordinates><Coordinate Y=\"2\" X=\"1\" Order=\"0\"/></Coordinates></Annotation>\r\n"
        "<Annotation Name=\"empty\" Type=\"Dot\" PartOfGroup=\"None\"><Coordinates/></Annotation>\r\n"
        "</Annotations>\r\n</ASAP_Annotations>\r\n";
    const core::AnnotationImportResult result = readOk(reordered);
    REQUIRE(result.annotations.size() == 1);
    CHECK(result.annotations[0].properties().classification == "Tumor");
    CHECK(result.invalidGeometry == 1);
    CHECK(result.featuresRead == 2);

    const core::AnnotationImportResult empty =
        readOk("<ASAP_Annotations><Annotations/><AnnotationGroups/></ASAP_Annotations>");
    CHECK(empty.annotations.empty());
    CHECK(empty.featuresRead == 0);
}

TEST_CASE("a cancelled read returns cancelled and nothing else", "[app][AsapXmlImport]")
{
    std::atomic<bool> cancel{true};
    const core::AnnotationImportResult result =
        asapXmlToAnnotations(asap(annotation("Dot", {{"1", "1"}})), kImportedAt, &cancel);
    CHECK(result.cancelled);
    CHECK(result.error.empty());
    CHECK(result.annotations.empty());
}

TEST_CASE("every annotation element is accounted for", "[app][AsapXmlImport]")
{
    const core::AnnotationImportResult result = readOk(
        asap(annotation("Dot", {{"1", "1"}}) + annotation("PointSet", {{"1", "1"}, {"2", "2"}})
             + annotation("Polygon", {{"0", "0"}}) + annotation("Ellipse", {{"0", "0"}})
             + annotation("Spline", {{"0", "0"}, {"5", "0"}, {"5", "5"}})));
    CHECK(result.featuresRead == 5);
    CHECK(result.featuresRead == result.featuresImported + skipCount(result));
    CHECK(result.annotations.size() == 4);
}

TEST_CASE("Greek and Cyrillic group names survive", "[app][AsapXmlImport]")
{
    const std::string greek = "\xCE\x9D\xCE\xAD\xCE\xBA\xCF\x81\xCF\x89\xCF\x83\xCE\xB7";
    const std::string cyrillic = "\xD0\x9E\xD0\xBF\xD1\x83\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C";
    const core::AnnotationImportResult result =
        readOk(asap(annotation("Dot", {{"1", "1"}}, greek) + annotation("Dot", {{"2", "2"}}, cyrillic)));
    REQUIRE(result.annotations.size() == 2);
    CHECK(result.annotations[0].properties().classification == greek);
    CHECK(result.annotations[1].properties().classification == cyrillic);
}

TEST_CASE("a large file is read whole", "[app][AsapXmlImport]")
{
    std::string annotations;
    for (int i = 0; i < 50000; ++i) {
        const std::string x = std::to_string(i % 500 * 20);
        const std::string y = std::to_string(i / 500 * 20);
        annotations += annotation("Polygon", {{x, y}, {x + "5", y}, {x + "5", y + "5"}, {x, y + "5"}}, "_0");
    }
    const core::AnnotationImportResult result = readOk(asap(annotations));
    CHECK(result.annotations.size() == 50000);
}
