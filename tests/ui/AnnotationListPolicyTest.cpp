#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <QtCore/qnamespace.h>

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

core::Annotation markerWith(const std::string& id, const std::string& classification)
{
    core::Annotation annotation(id, core::PointGeometry{core::PointF{1.0, 2.0}});
    core::AnnotationProperties properties = annotation.properties();
    properties.classification = classification;
    annotation.setProperties(std::move(properties));
    return annotation;
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

TEST_CASE("categoryTally lists configured categories in order, including zeros",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "Stroma")};

    const auto rows = ui::categoryTally(annotations, {"Tumor", "Stroma"});

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].category == "Tumor");
    REQUIRE(rows[0].count == 0);
    REQUIRE(rows[0].configured);
    REQUIRE(rows[1].category == "Stroma");
    REQUIRE(rows[1].count == 1);
}

TEST_CASE("categoryTally puts unconfigured categories after the configured ones, in "
          "first-appearance order", "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{
        markerWith("a", "Zeta"), markerWith("b", "Alpha"), markerWith("c", "Tumor")};

    const auto rows = ui::categoryTally(annotations, {"Tumor"});

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].category == "Tumor");
    REQUIRE(rows[1].category == "Zeta");
    REQUIRE_FALSE(rows[1].configured);
    REQUIRE(rows[2].category == "Alpha");
}

TEST_CASE("categoryTally never orders rows by count", "[ui][CategoryTally]")
{
    // A row that jumps while the user is clicking at it is a misclick, and this
    // is a workflow of several hundred clicks.
    const std::vector<core::Annotation> annotations{
        markerWith("a", "Stroma"), markerWith("b", "Stroma"), markerWith("c", "Tumor")};

    const auto rows = ui::categoryTally(annotations, {"Tumor", "Stroma"});

    REQUIRE(rows[0].category == "Tumor");
    REQUIRE(rows[0].count == 1);
    REQUIRE(rows[1].category == "Stroma");
    REQUIRE(rows[1].count == 2);
}

TEST_CASE("categoryTally counts case-insensitively under the configured spelling",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "mitosis"),
                                                    markerWith("b", "MITOSIS")};

    const auto rows = ui::categoryTally(annotations, {"Mitosis"});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "Mitosis");
    REQUIRE(rows[0].count == 2);
}

TEST_CASE("categoryTally counts an unconfigured category under its first-seen spelling",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "mitosis"),
                                                    markerWith("b", "Mitosis")};

    const auto rows = ui::categoryTally(annotations, {});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "mitosis");
    REQUIRE(rows[0].count == 2);
}

TEST_CASE("categoryTally puts Unclassified last and only when it is non-zero",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> withNone{markerWith("a", "Tumor")};
    REQUIRE(ui::categoryTally(withNone, {"Tumor"}).size() == 1);

    const std::vector<core::Annotation> withSome{markerWith("a", "Tumor"), markerWith("b", "")};
    const auto rows = ui::categoryTally(withSome, {"Tumor"});

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[1].category == "Unclassified");
    REQUIRE(rows[1].count == 1);
    REQUIRE_FALSE(rows[1].configured);
}

TEST_CASE("categoryTally treats a whitespace-only classification as Unclassified",
          "[ui][CategoryTally]")
{
    // Review Focus 3: annotation files are hand-editable, and a row labelled
    // with three spaces is unreadable and unselectable.
    const std::vector<core::Annotation> annotations{markerWith("a", "   ")};

    const auto rows = ui::categoryTally(annotations, {});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "Unclassified");
    REQUIRE(rows[0].count == 1);
}

TEST_CASE("categoryTally on an empty model still lists the configured categories",
          "[ui][CategoryTally]")
{
    const auto rows = ui::categoryTally({}, {"Tumor", "Stroma"});

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].count == 0);
    REQUIRE(rows[1].count == 0);
}

TEST_CASE("categoryTally folds a literal \"Unclassified\" classification into the synthetic row",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "Unclassified"),
                                                    markerWith("b", "unclassified"),
                                                    markerWith("c", "")};

    const auto rows = ui::categoryTally(annotations, {});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "Unclassified");
    REQUIRE(rows[0].count == 3);
    REQUIRE_FALSE(rows[0].configured);
}

TEST_CASE("categoryTally refuses \"Unclassified\" as a configured category",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "Tumor")};

    const auto rows = ui::categoryTally(annotations, {"Tumor", "Unclassified"});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "Tumor");
}

TEST_CASE("categoryTally flags only the appended Unclassified row as synthetic", "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{
        markerWith("a", "Tumor"), markerWith("b", ""), markerWith("c", "Zeta"), markerWith("d", "UNCLASSIFIED")};

    const auto rows = ui::categoryTally(annotations, {"Tumor", "Stroma"});

    REQUIRE(rows.size() == 4);
    for (std::size_t i = 0; i + 1 < rows.size(); ++i) {
        REQUIRE_FALSE(rows[i].synthetic);
    }
    REQUIRE(rows.back().category == "Unclassified");
    REQUIRE(rows.back().synthetic);
}

// The counting combo's key and wheel rule. Group AC26 found that a closed
// editable combo steps its item list on an arrow key, silently reassigning the
// clinical counting category; these pin the predicate that stops it.

TEST_CASE("countingComboSwallowsKey swallows the stepping keys while the popup is closed",
          "[ui][CountingCombo]")
{
    REQUIRE(ui::countingComboSwallowsKey(Qt::Key_Down, false));
    REQUIRE(ui::countingComboSwallowsKey(Qt::Key_Up, false));
    REQUIRE(ui::countingComboSwallowsKey(Qt::Key_PageDown, false));
    REQUIRE(ui::countingComboSwallowsKey(Qt::Key_PageUp, false));
}

TEST_CASE("countingComboSwallowsKey leaves the stepping keys alone while the popup is open",
          "[ui][CountingCombo]")
{
    // Deliberate keyboard selection -- F4, arrows, Enter -- must keep working;
    // inside an open popup the arrows only move the highlight and commit
    // nothing until Enter.
    REQUIRE_FALSE(ui::countingComboSwallowsKey(Qt::Key_Down, true));
    REQUIRE_FALSE(ui::countingComboSwallowsKey(Qt::Key_Up, true));
    REQUIRE_FALSE(ui::countingComboSwallowsKey(Qt::Key_PageDown, true));
    REQUIRE_FALSE(ui::countingComboSwallowsKey(Qt::Key_PageUp, true));
}

TEST_CASE("countingComboSwallowsKey passes every editing key through", "[ui][CountingCombo]")
{
    // Typing a new category is the feature AC11 covers; the fix must not touch
    // it. Home and End are text navigation on an editable combo, not item
    // stepping, so they pass too.
    for (const int key : {Qt::Key_A, Qt::Key_Backspace, Qt::Key_Delete, Qt::Key_Left,
                          Qt::Key_Right, Qt::Key_Home, Qt::Key_End, Qt::Key_Return,
                          Qt::Key_Enter, Qt::Key_Escape, Qt::Key_Tab}) {
        REQUIRE_FALSE(ui::countingComboSwallowsKey(key, false));
        REQUIRE_FALSE(ui::countingComboSwallowsKey(key, true));
    }
}

TEST_CASE("countingComboSwallowsWheel swallows the wheel only while the popup is closed",
          "[ui][CountingCombo]")
{
    // A wheel over a closed combo steps its items exactly as an arrow key
    // does, and a hand resting on a trackpad during a counting pass is at
    // least as likely as a bumped arrow key.
    REQUIRE(ui::countingComboSwallowsWheel(false));
    REQUIRE_FALSE(ui::countingComboSwallowsWheel(true));
}

TEST_CASE("sorting by label ignores case, as the filter does", "[ui][AnnotationList]")
{
    // The filter folds case; byte-order sorting does not, so every capitalised
    // label used to collect above every lowercase one -- "Zebra" before
    // "apple". A list the user filters case-insensitively and reads in a case-
    // sensitive order is the same list disagreeing with itself.
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("1", "Zebra", ""));
    annotations.push_back(annotation("2", "apple", ""));
    annotations.push_back(annotation("3", "Mitosis", ""));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Label,
                                         testTypeName);

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].label == "apple");
    REQUIRE(rows[1].label == "Mitosis");
    REQUIRE(rows[2].label == "Zebra");
}

TEST_CASE("labels differing only in case keep creation order", "[ui][AnnotationList]")
{
    // Folding case makes these equal, so the tie-break must still be creation
    // order -- otherwise two rows swap places between rebuilds, which is the
    // misclick categoryTally's ordering rule exists to prevent.
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("first", "tumor", ""));
    annotations.push_back(annotation("second", "Tumor", ""));

    const auto rows = ui::annotationRows(annotations, "", ui::AnnotationSortKey::Label,
                                         testTypeName);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].id == "first");
    REQUIRE(rows[1].id == "second");
}

TEST_CASE("sorting by classification ignores case", "[ui][AnnotationList]")
{
    std::vector<core::Annotation> annotations;
    annotations.push_back(annotation("1", "", "Zebra"));
    annotations.push_back(annotation("2", "", "apoptosis"));

    const auto rows = ui::annotationRows(annotations, "",
                                         ui::AnnotationSortKey::Classification, testTypeName);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].classification == "apoptosis");
    REQUIRE(rows[1].classification == "Zebra");
}

TEST_CASE("case folding is Unicode-aware, so one category stays one row", "[ui][AnnotationList]")
{
    // The same category in two cases, with a non-ASCII letter. std::tolower
    // folds only ASCII, so these produced two keys and the tally showed two
    // rows for one category -- a clinical count split in half, each half
    // correct about what it counted and neither correct about the category.
    // Written as explicit UTF-8 bytes so the test does not depend on the
    // compiler's source-encoding assumptions.
    const std::string upper = "ZELLK\xC3\x96RPER";  // O with diaeresis
    const std::string lower = "zellk\xC3\xB6rper";

    std::vector<core::Annotation> annotations;
    annotations.push_back(markerWith("1", upper));
    annotations.push_back(markerWith("2", lower));

    const auto tally = ui::categoryTally(annotations, {});

    REQUIRE(tally.size() == 1);
    REQUIRE(tally[0].count == 2);
    REQUIRE(tally[0].category == upper);  // the spelling first seen
}
