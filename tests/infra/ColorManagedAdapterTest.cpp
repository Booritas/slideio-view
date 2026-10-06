#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/core/ColorManagement.h"
#include "slideio/viewer/core/Types.h"

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
