#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/SlideProfilesDialog.h"

using namespace slideio::viewer;

TEST_CASE("a row reports a missing profile file", "[ui][SlideProfiles]")
{
    core::ColorProfileOverride entry;
    entry.slideId = "a3f8c2e109b74d21";
    entry.profilePath = "//unreachable/never/here.icc";
    entry.slideDisplayName = "case001_HE.svs";
    entry.displacedEmbedded = true;

    const ui::SlideProfileRow row = ui::buildSlideProfileRow(entry);

    REQUIRE(row.slideName == "case001_HE.svs");
    REQUIRE(row.status == "File missing");
    REQUIRE(row.displacesEmbedded);
}

TEST_CASE("a row falls back to the slide id when the name is unknown",
          "[ui][SlideProfiles]")
{
    core::ColorProfileOverride entry;
    entry.slideId = "a3f8c2e109b74d21";
    entry.profilePath = "//unreachable/never/here.icc";

    const ui::SlideProfileRow row = ui::buildSlideProfileRow(entry);
    REQUIRE(row.slideName == "a3f8c2e109b74d21");
}
