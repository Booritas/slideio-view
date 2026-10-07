#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/SlidePropertiesPanel.h"

#include <string>

using namespace slideio::viewer;

TEST_CASE("profile origin is named for the user", "[ui][SlideProperties]")
{
    REQUIRE(std::string(ui::colorProfileOriginName(core::ColorProfileOrigin::Library))
            == "Slide");
    REQUIRE(std::string(ui::colorProfileOriginName(core::ColorProfileOrigin::DefaultSetting))
            == "Default setting");
    REQUIRE(std::string(ui::colorProfileOriginName(core::ColorProfileOrigin::SlideOverride))
            == "Per-slide override");
}

TEST_CASE("a displaced embedded profile is called out", "[ui][SlideProperties]")
{
    core::SlideInfo info;
    info.colorProfileOrigin = core::ColorProfileOrigin::SlideOverride;
    info.displacedEmbeddedProfile = true;
    info.colorProfileInfo.present = true;
    info.colorProfileInfo.description = "Aperio GX";

    const std::string note = ui::displacedProfileNote(info);
    REQUIRE(note.find("Aperio GX") != std::string::npos);

    info.displacedEmbeddedProfile = false;
    REQUIRE(ui::displacedProfileNote(info).empty());
}
