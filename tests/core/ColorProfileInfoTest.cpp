#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Types.h"

using namespace slideio::viewer::core;

namespace
{

// A profile as a well-behaved scanner embeds it: parsed, described, RGB.
ColorProfileInfo embeddedProfile()
{
    ColorProfileInfo info;
    info.present = true;
    info.source = ColorProfileSource::Embedded;
    info.description = "sRGB IEC61966-2.1";
    info.manufacturer = "Leica Biosystems";
    info.model = "Aperio GT 450";
    info.version = "2.1.0";
    info.dataSpace = IccColorSpace::RGB;
    info.connectionSpace = IccColorSpace::XYZ;
    info.intent = RenderingIntent::RelativeColorimetric;
    info.dataSize = 3144;
    return info;
}

} // namespace

TEST_CASE("colorProfileSummary names the profile and where it came from", "[core][ColorProfileInfo]")
{
    REQUIRE(colorProfileSummary(embeddedProfile()) == "sRGB IEC61966-2.1 (embedded)");
}

TEST_CASE("colorProfileSummary reports a slide with no profile as not color-managed",
          "[core][ColorProfileInfo]")
{
    REQUIRE(colorProfileSummary(ColorProfileInfo{}) == "None (not color-managed)");
}

TEST_CASE("colorProfileSummary distinguishes an assumed profile from an embedded one",
          "[core][ColorProfileInfo]")
{
    // SlideIO reports Assumed when no profile was found and sRGB was substituted
    // under MissingProfilePolicy. The slide itself claims nothing.
    ColorProfileInfo info = embeddedProfile();
    info.source = ColorProfileSource::Assumed;
    REQUIRE(colorProfileSummary(info) == "sRGB IEC61966-2.1 (assumed)");
}

TEST_CASE("colorProfileSummary distinguishes a caller-supplied profile", "[core][ColorProfileInfo]")
{
    ColorProfileInfo info = embeddedProfile();
    info.source = ColorProfileSource::Supplied;
    REQUIRE(colorProfileSummary(info) == "sRGB IEC61966-2.1 (supplied)");
}

TEST_CASE("colorProfileSummary falls back to the device model when the profile has no description",
          "[core][ColorProfileInfo]")
{
    // The desc tag is optional in practice: plenty of scanner profiles carry a
    // model but an empty description, and "(embedded)" alone says nothing.
    ColorProfileInfo info = embeddedProfile();
    info.description.clear();
    REQUIRE(colorProfileSummary(info) == "Aperio GT 450 (embedded)");
}

TEST_CASE("colorProfileSummary falls back to a generic name when nothing identifies the profile",
          "[core][ColorProfileInfo]")
{
    ColorProfileInfo info = embeddedProfile();
    info.description.clear();
    info.model.clear();
    REQUIRE(colorProfileSummary(info) == "ICC profile (embedded)");
}

TEST_CASE("colorProfileSummary reports unparseable profile bytes as absent", "[core][ColorProfileInfo]")
{
    // present == false but source == Embedded is the documented shape for bytes
    // that would not parse: the driver found a tag, lcms2 rejected it. There is
    // no usable colorimetry, so the panel must not imply there is.
    ColorProfileInfo info;
    info.present = false;
    info.source = ColorProfileSource::Embedded;
    info.dataSize = 12;
    REQUIRE(colorProfileSummary(info) == "None (not color-managed)");
}

TEST_CASE("colorProfileSourceName spells out the provenance for the panel", "[core][ColorProfileInfo]")
{
    REQUIRE(std::string(colorProfileSourceName(ColorProfileSource::None)) == "None");
    REQUIRE(std::string(colorProfileSourceName(ColorProfileSource::Embedded)) == "Embedded");
    REQUIRE(std::string(colorProfileSourceName(ColorProfileSource::Assumed)) == "Assumed");
    REQUIRE(std::string(colorProfileSourceName(ColorProfileSource::Supplied)) == "Supplied");
}

TEST_CASE("iccColorSpaceName uses the conventional ICC space abbreviations", "[core][ColorProfileInfo]")
{
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::Unknown)) == "Unknown");
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::Gray)) == "Gray");
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::RGB)) == "RGB");
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::CMYK)) == "CMYK");
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::Lab)) == "Lab");
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::XYZ)) == "XYZ");
    REQUIRE(std::string(iccColorSpaceName(IccColorSpace::YCbCr)) == "YCbCr");
}

TEST_CASE("renderingIntentName spells the intents as prose, not enum identifiers",
          "[core][ColorProfileInfo]")
{
    REQUIRE(std::string(renderingIntentName(RenderingIntent::Perceptual)) == "Perceptual");
    REQUIRE(std::string(renderingIntentName(RenderingIntent::RelativeColorimetric))
            == "Relative colorimetric");
    REQUIRE(std::string(renderingIntentName(RenderingIntent::Saturation)) == "Saturation");
    REQUIRE(std::string(renderingIntentName(RenderingIntent::AbsoluteColorimetric))
            == "Absolute colorimetric");
}
