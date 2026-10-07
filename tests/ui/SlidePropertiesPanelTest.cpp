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

TEST_CASE("the origin row is relabelled when colour management is off",
          "[ui][SlideProperties]")
{
    // Review Finding 2. colorProfileOrigin is stamped at open and is
    // mode-independent, so with colour management off the row would otherwise
    // say "Per-slide override" right next to "Source: Embedded" while the
    // screen shows unconverted scanner RGB -- two neighbouring rows
    // contradicting each other. The label itself must say which situation it
    // describes rather than asserting the managed one unconditionally.
    REQUIRE(std::string(ui::originRowLabel(core::ColorMode::Raw)) == "Origin when managed");
    REQUIRE(std::string(ui::originRowLabel(core::ColorMode::Managed)) == "Origin");
}

TEST_CASE("a displaced embedded profile is called out", "[ui][SlideProperties]")
{
    const std::string note = ui::displacedProfileNote(true, "Aperio GX");
    REQUIRE(note.find("Aperio GX") != std::string::npos);

    REQUIRE(ui::displacedProfileNote(false, "Aperio GX").empty());
}

TEST_CASE("the displaced note names the embedded profile, not the active one",
          "[ui][SlideProperties]")
{
    // Review Finding 1. The active profile under an override is the override
    // itself, not what the slide embeds -- passing it as the "embedded"
    // description would tell the user their own supplied profile is the one
    // that got displaced. The note must be built from the embedded
    // description alone, and must not know or care what the active profile is.
    const std::string note = ui::displacedProfileNote(true, "Aperio GX");
    REQUIRE(note.find("Aperio GX") != std::string::npos);
    REQUIRE(note.find("GIMP") == std::string::npos);
}

TEST_CASE("a slide with nothing embedded reports no displacement even with an override applied",
          "[ui][SlideProperties]")
{
    // Review Finding 1. displacedEmbeddedProfile is false whenever the slide
    // embeds nothing (SlideIOAdapter only sets it when colorProfileInfo.present
    // was true), so the note must stay empty here -- but this is exactly the
    // shape of input the bug handed a plausible-looking wrong value for, so it
    // is pinned explicitly rather than left to follow from the other cases.
    REQUIRE(ui::displacedProfileNote(false, "").empty());
    REQUIRE(ui::displacedProfileNote(false, "GIMP built-in sRGB").empty());
}
