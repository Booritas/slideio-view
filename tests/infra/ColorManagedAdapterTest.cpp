#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/core/ColorManagement.h"
#include "slideio/viewer/core/ColorProfileOverride.h"
#include "slideio/viewer/core/Types.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

// Catch2 stringifies both operands of a failed assertion, and these buffers are
// ~196 KB each -- asserting on them directly means a genuine regression crashes
// the reporter instead of reporting. Compare scalar summaries so a failure stays
// readable, and so the message says how far apart the buffers are. Guarded
// against mismatched sizes even though callers already assert equality first.
size_t countDifferingBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    const size_t n = std::min(a.size(), b.size());
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) ++count;
    }
    return count;
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

    core::SuppliedColorProfile someOtherProfile;
    someOtherProfile.bytes.assign(128, 0);  // never reaches SlideIO
    someOtherProfile.isSlideOverride = false;

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
    // single tile, read fast. The mode travels on the key, which is what the
    // adapter selects the scene by; setColorMode is called alongside only
    // because that is what the UI does.
    const core::TileKey rawKey(2, 0, 0, 0, 0, core::ColorMode::Raw);
    const core::TileKey managedKey(2, 0, 0, 0, 0, core::ColorMode::Managed);
    const core::TileData raw = adapter.readTile(rawKey);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData managed = adapter.readTile(managedKey);

    REQUIRE_FALSE(raw.isError());
    REQUIRE_FALSE(managed.isError());
    REQUIRE(raw.buffer().size() == managed.buffer().size());
    REQUIRE(countDifferingBytes(raw.buffer(), managed.buffer()) > 0);
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

    const core::TileKey rawKey(0, 0, 0, 0, 0, core::ColorMode::Raw);
    const core::TileKey managedKey(0, 0, 0, 0, 0, core::ColorMode::Managed);
    const core::TileData raw = adapter.readTile(rawKey);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData managed = adapter.readTile(managedKey);

    REQUIRE_FALSE(raw.isError());
    REQUIRE_FALSE(managed.isError());
    REQUIRE(raw.buffer().size() == managed.buffer().size());
    REQUIRE(countDifferingBytes(raw.buffer(), managed.buffer()) == 0);
}

TEST_CASE("A tile's pixels follow its key's colour mode, not the adapter's current mode",
          "[infra][ColorManagement]")
{
    // The regression this guards: readTile used to pick the scene from the
    // adapter's live atomic mode while TileLoadScheduler cached the result
    // under the requested key. A mode flip between enqueue and read therefore
    // filed one rendition's pixels under the other's key -- permanently, until
    // eviction -- which is exactly the mismatch that putting ColorMode in
    // TileKey exists to prevent. Reading the same key under both live modes
    // must now give byte-identical results.
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    const core::TileKey managedKey(2, 0, 0, 0, 0, core::ColorMode::Managed);

    // A managed key read while the adapter still says Raw -- the open-time
    // window, before MainWindow's slideOpened handler flips the mode.
    adapter.setColorMode(core::ColorMode::Raw);
    const core::TileData duringRaw = adapter.readTile(managedKey);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData duringManaged = adapter.readTile(managedKey);

    REQUIRE_FALSE(duringRaw.isError());
    REQUIRE_FALSE(duringManaged.isError());
    REQUIRE(duringRaw.buffer().size() == duringManaged.buffer().size());
    REQUIRE(countDifferingBytes(duringRaw.buffer(), duringManaged.buffer()) == 0);

    // And the managed key really did produce managed pixels in both cases, so
    // the agreement above is not two raw reads agreeing with each other.
    const core::TileKey rawKey(2, 0, 0, 0, 0, core::ColorMode::Raw);
    adapter.setColorMode(core::ColorMode::Raw);
    const core::TileData rawPixels = adapter.readTile(rawKey);
    REQUIRE_FALSE(rawPixels.isError());
    REQUIRE(rawPixels.buffer().size() == duringRaw.buffer().size());
    REQUIRE(countDifferingBytes(rawPixels.buffer(), duringRaw.buffer()) > 0);
}

TEST_CASE("A raw key reads raw pixels even while the adapter is in managed mode",
          "[infra][ColorManagement]")
{
    // The converse of the case above: an in-flight raw read that completes
    // after a toggle to Managed must still deliver raw pixels, because a raw
    // key is what the cache will file it under.
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    const core::TileKey rawKey(2, 0, 0, 0, 0, core::ColorMode::Raw);

    adapter.setColorMode(core::ColorMode::Raw);
    const core::TileData duringRaw = adapter.readTile(rawKey);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData duringManaged = adapter.readTile(rawKey);

    REQUIRE_FALSE(duringRaw.isError());
    REQUIRE_FALSE(duringManaged.isError());
    REQUIRE(duringRaw.buffer().size() == duringManaged.buffer().size());
    REQUIRE(countDifferingBytes(duringRaw.buffer(), duringManaged.buffer()) == 0);
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

    core::SuppliedColorProfile asDefault;
    asDefault.bytes = profileBytes;
    asDefault.isSlideOverride = false;

    infra::SlideIOAdapter adapter(slide, 0, "", asDefault);
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    adapter.setColorMode(core::ColorMode::Managed);
    const core::ColorProfileInfo active = adapter.activeColorProfileInfo();
    REQUIRE(active.present);
    REQUIRE(active.source == core::ColorProfileSource::Supplied);
}

TEST_CASE("a per-slide override displaces an embedded profile",
          "[infra][ColorManagement][override]")
{
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    const std::string donorPath = imagePath("gdal/colors.png");
    if (!haveImage(donorPath)) { SKIP("test image corpus not available"); }

    // Borrow a profile that is valid and is demonstrably not the SVS's own.
    infra::SlideIOAdapter donor(donorPath);
    const std::vector<uint8_t> foreignProfile = donor.embeddedProfileBytes();
    // Not a SKIP: if this slide stops carrying a profile, this test must fail
    // loudly rather than quietly stop testing displacement.
    REQUIRE_FALSE(foreignProfile.empty());

    core::SuppliedColorProfile asDefault;
    asDefault.bytes = foreignProfile;
    asDefault.isSlideOverride = false;

    core::SuppliedColorProfile asOverride;
    asOverride.bytes = foreignProfile;
    asOverride.isSlideOverride = true;

    infra::SlideIOAdapter withDefault(path, 0, "", asDefault);
    infra::SlideIOAdapter withOverride(path, 0, "", asOverride);

    withDefault.setColorMode(core::ColorMode::Managed);
    withOverride.setColorMode(core::ColorMode::Managed);

    // The direct assertion: which profile actually got bound. Comparing pixels
    // only infers displacement; this states it.
    const core::ColorProfileInfo defaulted = withDefault.activeColorProfileInfo();
    const core::ColorProfileInfo overridden = withOverride.activeColorProfileInfo();

    // A default never displaces: the SVS keeps its own scanner profile.
    REQUIRE(defaulted.present);
    REQUIRE(defaulted.description != "GIMP built-in sRGB");

    // An override does: the borrowed profile is what got bound.
    REQUIRE(overridden.present);
    REQUIRE(overridden.description == "GIMP built-in sRGB");
}

TEST_CASE("an override changes the pixels and the reported origin",
          "[infra][ColorManagement][override]")
{
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    const std::string donorPath = imagePath("gdal/colors.png");
    if (!haveImage(donorPath)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter donor(donorPath);
    const std::vector<uint8_t> foreignProfile = donor.embeddedProfileBytes();
    REQUIRE_FALSE(foreignProfile.empty());

    core::SuppliedColorProfile asDefault;
    asDefault.bytes = foreignProfile;
    asDefault.isSlideOverride = false;

    core::SuppliedColorProfile asOverride;
    asOverride.bytes = foreignProfile;
    asOverride.isSlideOverride = true;

    infra::SlideIOAdapter withDefault(path, 0, "", asDefault);
    infra::SlideIOAdapter withOverride(path, 0, "", asOverride);

    withDefault.setColorMode(core::ColorMode::Managed);
    withOverride.setColorMode(core::ColorMode::Managed);

    // Level 2 is this slide's coarsest pyramid level: one real tile, read fast.
    const core::TileKey key(2, 0, 0, 0, 0, core::ColorMode::Managed);
    const core::TileData viaEmbedded = withDefault.readTile(key);
    const core::TileData viaOverride = withOverride.readTile(key);

    REQUIRE_FALSE(viaEmbedded.isError());
    REQUIRE_FALSE(viaOverride.isError());
    REQUIRE(viaEmbedded.buffer().size() == viaOverride.buffer().size());

    // Converting the Aperio profile to sRGB is a real transform; converting
    // "GIMP built-in sRGB" to sRGB is near-identity. So the two reads must
    // differ, and they differ in the informative direction.
    REQUIRE(countDifferingBytes(viaEmbedded.buffer(), viaOverride.buffer()) > 0);

    REQUIRE(withOverride.slideInfo().colorProfileOrigin
            == core::ColorProfileOrigin::SlideOverride);
    REQUIRE(withOverride.slideInfo().displacedEmbeddedProfile);

    // The same bytes supplied as a mere default are never used on this slide,
    // so the origin must stay with the slide's own profile and nothing is
    // recorded as displaced.
    REQUIRE(withDefault.slideInfo().colorProfileOrigin
            == core::ColorProfileOrigin::Library);
    REQUIRE_FALSE(withDefault.slideInfo().displacedEmbeddedProfile);
}
