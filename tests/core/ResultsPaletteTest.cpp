#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/ResultsPalette.h"

#include <set>
#include <tuple>

using namespace slideio::viewer::core;

namespace
{
bool same(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
}

TEST_CASE("PaletteStartsWithAnnotationColours", "[results]")
{
    const auto& p = resultsPalette();
    REQUIRE(p.size() >= 10);
    CHECK(p[0].b > p[0].r);                                       // blue
    CHECK((p[1].r == 0xE6 && p[1].g == 0x7E && p[1].b == 0x22));  // orange
    CHECK((p[2].r > 0xE0 && p[2].g > 0xE0 && p[2].b < 0x80));     // yellow
    CHECK((p[3].r > p[3].g && p[3].b > p[3].g));                  // purple
    CHECK((p[4].r < 0x40 && p[4].g > 0xA0 && p[4].b > 0xA0));     // cyan
    std::set<std::tuple<int, int, int>> distinct;
    for (const auto& c : p) {
        distinct.insert({c.r, c.g, c.b});
    }
    CHECK(distinct.size() == p.size());
}

TEST_CASE("ClassColourUsesOverrideThenCycles", "[results]")
{
    const Color red{0xFF, 0, 0, 0xFF};
    const std::map<std::string, Color> overrides{{caseFoldKey("tumour"), red}};
    CHECK(same(resultClassColour("Tumour", 0, overrides), red));

    const int n = static_cast<int>(resultsPalette().size());
    CHECK(same(resultClassColour("Stroma", n + 1, {}), resultsPalette()[1]));
    CHECK(same(resultLayerColour(n + 2), resultsPalette()[2]));
}
