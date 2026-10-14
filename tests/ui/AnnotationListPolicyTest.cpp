#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <chrono>
#include <string>
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

core::Annotation annotation(std::string id, std::string label, std::string classification,
                            int minutesOld = 0)
{
    core::AnnotationProperties properties;
    properties.label = std::move(label);
    properties.classification = std::move(classification);

    core::AnnotationMetadata metadata;
    metadata.author = "tester";
    metadata.createdAt = std::chrono::system_clock::time_point{} + std::chrono::minutes(minutesOld);
    metadata.modifiedAt = metadata.createdAt;

    return core::Annotation{std::move(id), rect(), std::move(properties), metadata};
}

} // namespace

TEST_CASE("an empty model gives an empty list", "[ui][AnnotationList]")
{
    // Review Focus 5. A slide with no annotations is the ordinary state of
    // every slide before anyone marks it, not an error.
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
    // Review Focus 3. The number counts across every annotation the slide
    // holds, not across the rows that survive the filter -- otherwise typing in
    // the search box renames the shapes, and the one the user is looking at
    // changes its name as they type.
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

TEST_CASE("sorts break ties by creation order", "[ui][AnnotationList]")
{
    // Two rows with the same sort key must not swap between rebuilds, and the
    // list rebuilds on every model change.
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Same", ""));
    annotations.push_back(annotation("b", "Same", ""));
    annotations.push_back(annotation("c", "Same", ""));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Label,
                                         testTypeName);
    REQUIRE(rows[0].id == "a");
    REQUIRE(rows[1].id == "b");
    REQUIRE(rows[2].id == "c");
}

TEST_CASE("sorting by modified time puts the most recent first", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("old", "", "", 0));
    annotations.push_back(annotation("new", "", "", 10));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::ModifiedTime,
                                         testTypeName);
    REQUIRE(rows[0].id == "new");
    REQUIRE(rows[1].id == "old");
}

TEST_CASE("the type column carries the supplied name", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("a", "Labelled", "Tumor"));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::CreationOrder,
                                         testTypeName);
    REQUIRE(rows[0].type == "Rectangle");
    REQUIRE(rows[0].classification == "Tumor");
}
