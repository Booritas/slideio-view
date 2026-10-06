#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/core/ColorManagement.h"
#include "slideio/viewer/core/Types.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace slideio::viewer;

namespace
{

// The image corpus is not part of the repository. Tests that need it are
// skipped rather than failed when it is absent, matching how SlideIO's own
// suite handles the same problem.
std::string imagePath(const std::string& relative)
{
    const char* root = std::getenv("SLIDEIO_VIEWER_TEST_IMAGES");
    return root ? std::string(root) + "/" + relative : std::string();
}

bool haveImage(const std::string& path)
{
    if (path.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

} // namespace

TEST_CASE("A slide with an embedded RGB profile offers colour management",
          "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);
}

TEST_CASE("A slide with no profile and no default offers nothing", "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/img_2448x2448_3x8bit_SRC_RGB_ducks.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::NoProfile);
}

TEST_CASE("A fluorescence slide is never colour-managed", "[infra][ColorManagement]")
{
    // Review Focus 1. A 3-channel 8-bit fluorescence image passes SlideIO's own
    // bind check, which counts channels and types but cannot know the channels
    // are intensities. Binding it would ICC-transform them as though they were
    // R/G/B. The viewer-side gate has to reject it before transformScene runs.
    const std::string path = imagePath("czi/08_18_2018_enc_1001_633.czi");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement
            == core::ColorManagementAvailability::NotColorimetric);
}

TEST_CASE("Colour management is off until it is asked for", "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.colorMode() == core::ColorMode::Raw);
}

TEST_CASE("A default profile does not displace a profile the slide embeds",
          "[infra][ColorManagement]")
{
    // SlideIO's override wins unconditionally over an embedded profile
    // (colormanagement.cpp:48). A setting that means "use this when a slide has
    // none" must therefore not be passed through for a slide that has one --
    // otherwise configuring a default would silently change how every
    // profile-carrying slide renders.
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    std::vector<uint8_t> someOtherProfile(128, 0);  // never reaches SlideIO

    infra::SlideIOAdapter adapter(path, 0, "", someOtherProfile);
    adapter.setColorMode(core::ColorMode::Managed);

    const core::ColorProfileInfo active = adapter.activeColorProfileInfo();
    REQUIRE(active.present);
    REQUIRE(active.source == core::ColorProfileSource::Embedded);
    REQUIRE(active.description == "GIMP built-in sRGB");
}

TEST_CASE("Colour management changes the pixels of a profiled slide",
          "[infra][ColorManagement]")
{
    // gdal/colors.png is the wrong fixture for this: its embedded "GIMP
    // built-in sRGB" profile has standard sRGB/Rec.709 primaries, and Managed
    // mode always targets sRGB, so converting it is a no-op (see the identity
    // test below). An Aperio-scanned slide carries its scanner's own ICC
    // profile, which does disagree with sRGB, so this is where a real
    // conversion shows up.
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    // Level 2 is this slide's coarsest pyramid level (3 levels total): a real,
    // single tile, read fast.
    const core::TileKey key(2, 0, 0);
    const core::TileData raw = adapter.readTile(key);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData managed = adapter.readTile(key);

    REQUIRE_FALSE(raw.isError());
    REQUIRE_FALSE(managed.isError());
    REQUIRE(raw.buffer().size() == managed.buffer().size());
    REQUIRE(raw.buffer() != managed.buffer());
}

TEST_CASE("Colour managing an sRGB-profiled slide is an identity transform",
          "[infra][ColorManagement]")
{
    // Managed mode always targets sRGB (buildManagedScene in SlideIOAdapter.cpp
    // calls cm.setTarget(ColorTarget::sRGB)). gdal/colors.png embeds "GIMP
    // built-in sRGB", whose primaries and transfer curve already are sRGB, so
    // converting it to sRGB has nothing to remove. This is expected behaviour,
    // not a bug: do not "fix" raw/managed to differ here. The slide that
    // proves conversion actually happens is svs/JP2K-33003-1.svs, above.
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    const core::TileKey key(0, 0, 0);
    const core::TileData raw = adapter.readTile(key);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData managed = adapter.readTile(key);

    REQUIRE_FALSE(raw.isError());
    REQUIRE_FALSE(managed.isError());
    REQUIRE(raw.buffer() == managed.buffer());
}

TEST_CASE("A configured default profile is reported as the bound profile",
          "[infra][ColorManagement]")
{
    // This asserts provenance metadata (source == Supplied), not pixels -- it
    // is not proof that colour management converts anything. See "Colour
    // management changes the pixels of a profiled slide" for that.
    const std::string slide = imagePath("gdal/img_2448x2448_3x8bit_SRC_RGB_ducks.png");
    const std::string profileSource = imagePath("gdal/colors.png");
    if (!haveImage(slide) || !haveImage(profileSource)) { SKIP("test image corpus not available"); }

    // Borrow a real profile from a slide that has one, rather than ship an .icc
    // fixture: it keeps the corpus the single source of test data.
    std::vector<uint8_t> profileBytes;
    {
        infra::SlideIOAdapter donor(profileSource, 0, "");
        REQUIRE(donor.slideInfo().colorProfileInfo.present);
        profileBytes = donor.embeddedProfileBytes();
    }
    REQUIRE_FALSE(profileBytes.empty());

    infra::SlideIOAdapter adapter(slide, 0, "", profileBytes);
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    adapter.setColorMode(core::ColorMode::Managed);
    const core::ColorProfileInfo active = adapter.activeColorProfileInfo();
    REQUIRE(active.present);
    REQUIRE(active.source == core::ColorProfileSource::Supplied);
}
