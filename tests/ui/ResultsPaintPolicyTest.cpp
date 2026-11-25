#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slideio/viewer/ui/ResultsPaintPolicy.h"

#include <vector>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;
using Catch::Matchers::WithinAbs;

namespace
{

constexpr core::Color kLayer{10, 20, 30, 255};
const std::vector<core::Color> kClasses = {core::Color{200, 0, 0, 255}, core::Color{0, 200, 0, 255}};

bool same(core::Color a, core::Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

core::ResultObject withClasses(std::vector<int> classes)
{
    core::ResultObject object;
    object.id = "o";
    object.layer = 0;
    object.classes = std::move(classes);
    return object;
}

} // namespace

TEST_CASE("an object with a class takes its first class's colour", "[ui][ResultsPaintPolicy]")
{
    core::ResultsLayerView view;
    view.colourByClass = true;
    CHECK(same(objectColour(withClasses({1, 0}), view, kClasses, kLayer), kClasses[1]));
    CHECK(same(objectColour(withClasses({0}), view, kClasses, kLayer), kClasses[0]));
}

TEST_CASE("an unclassified object takes the layer colour", "[ui][ResultsPaintPolicy]")
{
    core::ResultsLayerView view;
    view.colourByClass = true;
    CHECK(same(objectColour(withClasses({}), view, kClasses, kLayer), kLayer));
}

TEST_CASE("a class the palette does not list falls back to the layer colour", "[ui][ResultsPaintPolicy]")
{
    core::ResultsLayerView view;
    view.colourByClass = true;
    CHECK(same(objectColour(withClasses({5}), view, kClasses, kLayer), kLayer));
    CHECK(same(objectColour(withClasses({-1}), view, kClasses, kLayer), kLayer));
}

TEST_CASE("colour-by-class off paints every object in the layer colour", "[ui][ResultsPaintPolicy]")
{
    core::ResultsLayerView view;
    view.colourByClass = false;
    CHECK(same(objectColour(withClasses({1}), view, kClasses, kLayer), kLayer));
    CHECK(same(objectColour(withClasses({}), view, kClasses, kLayer), kLayer));
}

TEST_CASE("overview squares take the dominant class, grey for Other, layer colour for Unclassified",
          "[ui][ResultsPaintPolicy]")
{
    CHECK(same(overviewColour(1, kClasses, kLayer), kClasses[1]));
    CHECK(same(overviewColour(core::OverviewGrid::kOtherClasses, kClasses, kLayer),
               core::Color{0x9E, 0x9E, 0x9E, 255}));
    CHECK(same(overviewColour(core::OverviewGrid::kUnclassified, kClasses, kLayer), kLayer));
    CHECK(same(overviewColour(7, kClasses, kLayer), kLayer));
}

TEST_CASE("class visibility follows the hidden class names", "[ui][ResultsPaintPolicy]")
{
    core::ResultLayerInfo info;
    info.classes = {core::ResultClassInfo{"tumour", 3}, core::ResultClassInfo{"stroma", 2}};
    core::ResultsLayerView view;
    view.hiddenClasses = {"stroma"};
    ClassVisibility visibility = classVisibility(info, view);
    CHECK(visibility.classVisible == std::vector<bool>{true, false});
    CHECK(visibility.unclassifiedVisible);

    view.hiddenClasses = {""};
    visibility = classVisibility(info, view);
    CHECK(visibility.classVisible == std::vector<bool>{true, true});
    CHECK_FALSE(visibility.unclassifiedVisible);
}

namespace
{

core::OverviewGrid::Cell cellAt(int column, int row, int dominant, double opacity)
{
    core::OverviewGrid::Cell cell;
    cell.column = column;
    cell.row = row;
    cell.dominant = dominant;
    cell.opacity = opacity;
    return cell;
}

} // namespace

TEST_CASE("the overview image has one premultiplied pixel per square", "[ui][ResultsPaintPolicy]")
{
    // A 4 x 2 scene gives 1-unit squares: a 4 x 2 image.
    const core::OverviewGrid grid(core::Rect<double>{0.0, 0.0, 4.0, 2.0}, {});
    const std::vector<core::OverviewGrid::Cell> cells = {
        cellAt(1, 0, 1, 0.5), cellAt(3, 0, core::OverviewGrid::kOtherClasses, 1.0),
        cellAt(0, 1, core::OverviewGrid::kUnclassified, 1.0)};
    const std::vector<uint32_t> pixels = overviewPixels(grid, cells, true, kClasses, kLayer);
    REQUIRE(pixels.size() == 8);
    CHECK(pixels[0] == 0u);                 // no cell: transparent
    CHECK(pixels[1] == 0x80006400u);        // class 1 (0,200,0) at half opacity, premultiplied
    CHECK(pixels[2] == 0u);
    CHECK(pixels[3] == 0xFF9E9E9Eu);        // Other classes: grey
    CHECK(pixels[4] == 0xFF0A141Eu);        // Unclassified: the layer colour
    CHECK(pixels[7] == 0u);
}

TEST_CASE("the overview image ignores classes when colour-by-class is off", "[ui][ResultsPaintPolicy]")
{
    const core::OverviewGrid grid(core::Rect<double>{0.0, 0.0, 4.0, 2.0}, {});
    const std::vector<core::OverviewGrid::Cell> cells = {
        cellAt(1, 0, 1, 1.0), cellAt(3, 0, core::OverviewGrid::kOtherClasses, 1.0)};
    const std::vector<uint32_t> pixels = overviewPixels(grid, cells, false, kClasses, kLayer);
    REQUIRE(pixels.size() == 8);
    CHECK(pixels[1] == 0xFF0A141Eu);
    CHECK(pixels[3] == 0xFF0A141Eu);
}

TEST_CASE("the overview span covers the whole squares the view touches", "[ui][ResultsPaintPolicy]")
{
    // 2048 x 1024 at (100, 50): 2-unit squares, 1024 x 512 of them.
    const core::OverviewGrid grid(core::Rect<double>{100.0, 50.0, 2048.0, 1024.0}, {});

    const CellSpan inside = overviewSpan(grid, core::Rect<double>{103.0, 51.0, 4.0, 2.0});
    CHECK(inside.column == 1);
    CHECK(inside.row == 0);
    CHECK(inside.columns == 3);   // x 103..107 touches squares 1, 2, 3
    CHECK(inside.rows == 2);      // y 51..53 touches rows 0, 1

    const CellSpan whole = overviewSpan(grid, core::Rect<double>{-1000.0, -1000.0, 1e6, 1e6});
    CHECK(whole.column == 0);
    CHECK(whole.row == 0);
    CHECK(whole.columns == 1024);
    CHECK(whole.rows == 512);

    CHECK(overviewSpan(grid, core::Rect<double>{5000.0, 50.0, 10.0, 10.0}).empty());
    CHECK(overviewSpan(grid, core::Rect<double>{0.0, 0.0, 50.0, 50.0}).empty());
}

TEST_CASE("an overview cell covers its square of the scene", "[ui][ResultsPaintPolicy]")
{
    // A 2048-wide scene gives 2-unit squares (1024 on the long side).
    const core::OverviewGrid grid(core::Rect<double>{100.0, 50.0, 2048.0, 1024.0}, {});
    core::OverviewGrid::Cell cell;
    cell.column = 3;
    cell.row = 5;
    const core::Rect<double> rect = overviewCellRect(grid, cell);
    CHECK_THAT(rect.x, WithinAbs(106.0, 1e-9));
    CHECK_THAT(rect.y, WithinAbs(60.0, 1e-9));
    CHECK_THAT(rect.width, WithinAbs(2.0, 1e-9));
    CHECK_THAT(rect.height, WithinAbs(2.0, 1e-9));
}
