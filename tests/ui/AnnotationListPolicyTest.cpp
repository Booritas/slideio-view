#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace slideio::viewer;

namespace
{

/// Plain ASCII names, so these tests need no QApplication and no translation.
std::string testTypeName(core::AnnotationType type)
{
    switch (type) {
    case core::AnnotationType::Rectangle:
        return "Rectangle";
    case core::AnnotationType::Polygon:
        return "Polygon";
    case core::AnnotationType::Polyline:
        return "Freehand Line";
    }
    return "Shape";
}

core::AnnotationGeometry rect()
{
    core::RectangleGeometry geometry;
    geometry.topLeft = core::PointF{0.0, 0.0};
    geometry.bottomRight = core::PointF{10.0, 10.0};
    return geometry;
}

core::AnnotationGeometry polygonGeometry()
{
    core::PolygonGeometry geometry;
    geometry.vertices = {core::PointF{0.0, 0.0}, core::PointF{10.0, 0.0},
                         core::PointF{10.0, 10.0}};
    geometry.closed = true;
    return geometry;
}

core::AnnotationGeometry polylineGeometry()
{
    core::PolygonGeometry geometry;
    geometry.vertices = {core::PointF{0.0, 0.0}, core::PointF{10.0, 10.0}};
    geometry.closed = false;
    return geometry;
}

core::Annotation annotation(std::string id, std::string label, std::string classification,
                            int minutesOld = 0,
                            const core::AnnotationGeometry& geometry = rect())
{
    core::AnnotationProperties properties;
    properties.label = std::move(label);
    properties.classification = std::move(classification);

    core::AnnotationMetadata metadata;
    metadata.author = "tester";
    metadata.createdAt = std::chrono::system_clock::time_point{} +
                         std::chrono::minutes(minutesOld);
    metadata.modifiedAt = metadata.createdAt;

    return core::Annotation{std::move(id), geometry, std::move(properties), metadata};
}

} // namespace

TEST_CASE("an empty model gives an empty list", "[ui][AnnotationList]")
{
    const auto rows = ui::annotationRows({}, "", ui::AnnotationSortKey::CreationOrder,
                                         testTypeName);
    REQUIRE(rows.empty());
    REQUIRE(ui::annotationDockTitle(0, 0) == "Annotations (0)");
}

TEST_CASE("the dock title counts, and says so when filtered", "[ui][AnnotationList]")
{
    REQUIRE(ui::annotationDockTitle(12, 12) == "Annotations (12)");
    REQUIRE(ui::annotationDockTitle(12, 3) == "Annotations (3 of 12)");
}

TEST_CASE("an unlabeled annotation is named by type and creation number",
          "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "", ""));
    annotations.push_back(annotation("b", "Tumour front", ""));
    annotations.push_back(annotation("c", "", ""));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::CreationOrder,
                                         testTypeName);

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].label == "Rectangle 1");
    REQUIRE(rows[1].label == "Tumour front");
    REQUIRE(rows[2].label == "Rectangle 3");
}

TEST_CASE("the creation number survives a filter", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "", "Tumor"));
    annotations.push_back(annotation("b", "", "Stroma"));
    annotations.push_back(annotation("c", "", "Tumor"));

    const auto rows = ui::annotationRows(annotations, "stroma",
                                         ui::AnnotationSortKey::CreationOrder, testTypeName);

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == "b");
    REQUIRE(rows[0].label == "Rectangle 2");
}

TEST_CASE("the creation number survives a sort", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "", "Zeta"));
    annotations.push_back(annotation("b", "", "Alpha"));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Classification,
                                         testTypeName);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].id == "b");
    REQUIRE(rows[0].label == "Rectangle 2");
    REQUIRE(rows[1].id == "a");
    REQUIRE(rows[1].label == "Rectangle 1");
}

TEST_CASE("filtering is case-insensitive over label and classification",
          "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Tumour Front", ""));
    annotations.push_back(annotation("b", "", "NECROSIS"));
    annotations.push_back(annotation("c", "Nothing", "Normal"));

    REQUIRE(ui::annotationRows(annotations, "tumour", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).size() == 1);
    REQUIRE(ui::annotationRows(annotations, "necro", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).size() == 1);
    REQUIRE(ui::annotationRows(annotations, "zzz", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).empty());
}

TEST_CASE("notes are not searched", "[ui][AnnotationList]")
{
    // A row matching on notes would show nothing resembling the search text in
    // any of its three visible columns, and read as a bug.
    core::AnnotationProperties properties;
    properties.notes = "mitotic figures throughout";
    core::AnnotationMetadata metadata;
    std::vector<core::Annotation> annotations;
    annotations.push_back(core::Annotation{"a", rect(), properties, metadata});

    REQUIRE(ui::annotationRows(annotations, "mitotic", ui::AnnotationSortKey::CreationOrder,
                               testTypeName).empty());
}

TEST_CASE("sorting by label breaks ties by creation order", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    // Add 50 annotations with the same label, interleaved with distinct ones.
    for (int i = 0; i < 50; ++i) {
        annotations.push_back(annotation("same_" + std::to_string(i), "TieLabel", ""));
        if (i < 10) {
            const auto id = "distinct_" + std::to_string(i);
            const auto label = "Distinct_" + std::to_string(i);
            annotations.push_back(annotation(id, label, ""));
        }
    }

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Label,
                                         testTypeName);

    // Find the tied rows and verify their IDs are in creation order
    std::vector<std::string> tiedIds;
    for (const auto& row : rows) {
        if (row.label == "TieLabel") {
            tiedIds.push_back(row.id);
        }
    }
    REQUIRE(tiedIds.size() == 50);
    // Verify they appear in creation order (same_0, same_1, ..., same_49)
    for (std::size_t i = 0; i < tiedIds.size(); ++i) {
        REQUIRE(tiedIds[i] == "same_" + std::to_string(i));
    }
}

TEST_CASE("sorting by classification breaks ties by creation order", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    // Add 50 annotations with the same classification, interleaved with distinct ones.
    for (int i = 0; i < 50; ++i) {
        annotations.push_back(annotation("same_" + std::to_string(i), "", "TieClass"));
        if (i < 10) {
            const auto id = "distinct_" + std::to_string(i);
            const auto label = "Distinct_" + std::to_string(i);
            annotations.push_back(annotation(id, label, ""));
        }
    }

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Classification,
                                         testTypeName);

    // Find the tied rows and verify their IDs are in creation order
    std::vector<std::string> tiedIds;
    for (const auto& row : rows) {
        if (row.classification == "TieClass") {
            tiedIds.push_back(row.id);
        }
    }
    REQUIRE(tiedIds.size() == 50);
    for (std::size_t i = 0; i < tiedIds.size(); ++i) {
        REQUIRE(tiedIds[i] == "same_" + std::to_string(i));
    }
}

TEST_CASE("sorting by type breaks ties by creation order", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    // Add 50 rectangles (all same type), interleaved with polylines (different type)
    for (int i = 0; i < 50; ++i) {
        annotations.push_back(annotation("rect_" + std::to_string(i), "", "",
                                         0, rect()));
        if (i < 10) {
            annotations.push_back(annotation("poly_" + std::to_string(i), "", "",
                                             0, polylineGeometry()));
        }
    }

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Type,
                                         testTypeName);

    // Find the rectangle rows and verify their IDs are in creation order
    std::vector<std::string> rectIds;
    for (const auto& row : rows) {
        if (row.type == "Rectangle" && row.label.find("Rectangle") == 0) {
            rectIds.push_back(row.id);
        }
    }
    REQUIRE(rectIds.size() == 50);
    for (std::size_t i = 0; i < rectIds.size(); ++i) {
        REQUIRE(rectIds[i] == "rect_" + std::to_string(i));
    }
}

TEST_CASE("sorting by modified time breaks ties by creation order", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    // Add 50 annotations with the same modification time
    for (int i = 0; i < 50; ++i) {
        annotations.push_back(annotation("same_time_" + std::to_string(i), "", "", 100));
        if (i < 10) {
            const auto id = "diff_time_" + std::to_string(i);
            annotations.push_back(annotation(id, "", "", i));
        }
    }

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::ModifiedTime,
                                         testTypeName);

    // Find rows with tied times and verify their IDs appear in creation order
    std::vector<std::string> tieTimeIds;
    for (const auto& row : rows) {
        if (row.id.find("same_time_") == 0) {
            tieTimeIds.push_back(row.id);
        }
    }
    REQUIRE(tieTimeIds.size() == 50);
    for (std::size_t i = 0; i < tieTimeIds.size(); ++i) {
        REQUIRE(tieTimeIds[i] == "same_time_" + std::to_string(i));
    }
}

TEST_CASE("the type column uses the supplied callback", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Labelled", "Tumor"));

    // Use a sentinel callback that returns a distinctive name, not the real type name
    const auto sentinelTypeName = [](core::AnnotationType) { return std::string{"ZZ"}; };
    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::CreationOrder,
                                         sentinelTypeName);
    REQUIRE(rows[0].type == "ZZ");
    REQUIRE(rows[0].classification == "Tumor");
}

TEST_CASE("filtering matches the display label including auto-numbered fallback", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("first", "", "Class1"));
    annotations.push_back(annotation("second", "", "Class2"));

    // Filter for "rectangle 2" should match the second rectangle (auto-numbered as "Rectangle 2")
    const auto rows = ui::annotationRows(annotations, "rectangle 2",
                                         ui::AnnotationSortKey::CreationOrder, testTypeName);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == "second");
    REQUIRE(rows[0].label == "Rectangle 2");
}

TEST_CASE("type names do not match filters", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    // A labelled rectangle - filtering for "rectangle" should NOT match it
    annotations.push_back(annotation("tumour", "Tumour", ""));

    const auto rows = ui::annotationRows(annotations, "rectangle",
                                         ui::AnnotationSortKey::CreationOrder, testTypeName);
    REQUIRE(rows.empty());
}

TEST_CASE("sorting by type orders rows by type name", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    // Build a mix of types in an order NOT sorted by type name
    annotations.push_back(annotation("poly1", "Polygon 1", "", 0, polygonGeometry()));
    annotations.push_back(annotation("rect1", "Rectangle 1", "", 0, rect()));
    annotations.push_back(annotation("line1", "Freehand 1", "", 0, polylineGeometry()));
    annotations.push_back(annotation("poly2", "Polygon 2", "", 0, polygonGeometry()));
    annotations.push_back(annotation("rect2", "Rectangle 2", "", 0, rect()));
    annotations.push_back(annotation("line2", "Freehand 2", "", 0, polylineGeometry()));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Type,
                                         testTypeName);

    REQUIRE(rows.size() == 6);
    // Type names should be in order: Freehand Line, Polygon, Rectangle
    REQUIRE(rows[0].type == "Freehand Line");
    REQUIRE(rows[1].type == "Freehand Line");
    REQUIRE(rows[2].type == "Polygon");
    REQUIRE(rows[3].type == "Polygon");
    REQUIRE(rows[4].type == "Rectangle");
    REQUIRE(rows[5].type == "Rectangle");
}
