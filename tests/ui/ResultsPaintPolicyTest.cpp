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

TEST_CASE("the cells cache view grows by half the view each side", "[ui][ResultsPaintPolicy]")
{
    const core::Rect<double> grown = grownView(core::Rect<double>{100.0, 200.0, 40.0, 20.0});
    CHECK_THAT(grown.x, WithinAbs(80.0, 1e-9));
    CHECK_THAT(grown.y, WithinAbs(190.0, 1e-9));
    CHECK_THAT(grown.width, WithinAbs(80.0, 1e-9));
    CHECK_THAT(grown.height, WithinAbs(40.0, 1e-9));
}

TEST_CASE("a view inside the cached rectangle reuses it", "[ui][ResultsPaintPolicy]")
{
    const core::Rect<double> outer{0.0, 0.0, 100.0, 100.0};
    CHECK(rectContains(outer, core::Rect<double>{10.0, 10.0, 50.0, 50.0}));
    CHECK(rectContains(outer, outer));
    CHECK_FALSE(rectContains(outer, core::Rect<double>{60.0, 10.0, 50.0, 50.0}));
    CHECK_FALSE(rectContains(outer, core::Rect<double>{-1.0, 10.0, 50.0, 50.0}));
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
