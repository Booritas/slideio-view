#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ColorManagement.h"

#include <cstring>

using namespace slideio::viewer::core;

namespace
{

// A minimal but structurally valid ICC v4 header. Only the fields
// inspectIccHeader reads are filled: size at 0, data colour space at 16,
// and the 'acsp' signature at 36. Everything else stays zero, which is
// exactly the shape of input the function has to tolerate.
std::vector<uint8_t> makeIccHeader(const char* dataSpace = "RGB ", uint32_t declaredSize = 128)
{
    std::vector<uint8_t> bytes(128, 0);
    bytes[0] = static_cast<uint8_t>((declaredSize >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((declaredSize >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((declaredSize >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(declaredSize & 0xFF);
    std::memcpy(bytes.data() + 16, dataSpace, 4);
    std::memcpy(bytes.data() + 36, "acsp", 4);
    return bytes;
}

} // namespace

TEST_CASE("inspectIccHeader accepts a well-formed RGB profile header", "[core][ColorManagement]")
{
    const IccHeaderSummary s = inspectIccHeader(makeIccHeader());
    REQUIRE(s.plausible);
    REQUIRE(s.dataSpace == "RGB ");
    REQUIRE(s.declaredSize == 128);
}

TEST_CASE("inspectIccHeader reports the data space of a non-RGB profile", "[core][ColorManagement]")
{
    // Reported rather than rejected: the caller decides. ColorManagement
    // only binds RGB, but inspectIccHeader is not the place that knows that.
    const IccHeaderSummary s = inspectIccHeader(makeIccHeader("CMYK"));
    REQUIRE(s.plausible);
    REQUIRE(s.dataSpace == "CMYK");
}

TEST_CASE("inspectIccHeader rejects a buffer shorter than the ICC header", "[core][ColorManagement]")
{
    std::vector<uint8_t> truncated = makeIccHeader();
    truncated.resize(127);
    REQUIRE_FALSE(inspectIccHeader(truncated).plausible);
}

TEST_CASE("inspectIccHeader rejects empty input", "[core][ColorManagement]")
{
    REQUIRE_FALSE(inspectIccHeader({}).plausible);
}

TEST_CASE("inspectIccHeader rejects a buffer without the acsp signature", "[core][ColorManagement]")
{
    std::vector<uint8_t> notIcc = makeIccHeader();
    notIcc[36] = 'x';
    REQUIRE_FALSE(inspectIccHeader(notIcc).plausible);
}

TEST_CASE("inspectIccHeader rejects a header claiming more bytes than it has",
          "[core][ColorManagement]")
{
    // A truncated download is the realistic way this happens.
    const IccHeaderSummary s = inspectIccHeader(makeIccHeader("RGB ", 4096));
    REQUIRE_FALSE(s.plausible);
}

TEST_CASE("colorManagementUnavailableReason explains a non-colorimetric slide",
          "[core][ColorManagement]")
{
    const std::string r =
        colorManagementUnavailableReason(ColorManagementAvailability::NotColorimetric, "");
    REQUIRE(r.find("RGB brightfield") != std::string::npos);
}

TEST_CASE("colorManagementUnavailableReason explains a slide with no profile",
          "[core][ColorManagement]")
{
    const std::string r =
        colorManagementUnavailableReason(ColorManagementAvailability::NoProfile, "");
    REQUIRE(r.find("no ICC profile") != std::string::npos);
}

TEST_CASE("colorManagementUnavailableReason passes through SlideIO's own message",
          "[core][ColorManagement]")
{
    // SlideIO's bind errors are specific ("the source profile describes CMYK
    // data, not RGB"). Reproducing that judgement here would only let the two
    // drift, so the detail is surfaced verbatim.
    const std::string r = colorManagementUnavailableReason(
        ColorManagementAvailability::BindFailed, "the source profile describes CMYK data, not RGB");
    REQUIRE(r.find("CMYK") != std::string::npos);
}

TEST_CASE("colorManagementUnavailableReason is empty when colour management is available",
          "[core][ColorManagement]")
{
    REQUIRE(colorManagementUnavailableReason(ColorManagementAvailability::Available, "").empty());
}

TEST_CASE("isColorimetricForIcc accepts 8-bit RGB brightfield", "[core][ColorManagement]")
{
    REQUIRE(isColorimetricForIcc(3, DataType::Byte, false));
}

TEST_CASE("isColorimetricForIcc accepts 16-bit RGB brightfield", "[core][ColorManagement]")
{
    // Review Focus 2. SlideInfo::isBrightfield is false for this case -- it
    // only admits 3-channel Byte -- but ColorManagement binds DT_UInt16 just
    // as happily, so gating on isBrightfield would deny colour management to
    // slides that can have it.
    REQUIRE(isColorimetricForIcc(3, DataType::UInt16, false));
}

TEST_CASE("isColorimetricForIcc rejects three fluorescence channels", "[core][ColorManagement]")
{
    // Review Focus 1, and the one case where being wrong is silently wrong.
    // Three 8-bit fluorescence channels satisfy every check SlideIO itself
    // makes -- it counts channels and inspects data types, which cannot reveal
    // that the values are intensities rather than colour. Binding would ICC
    // transform them as though they were R/G/B and produce confident nonsense.
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::Byte, true));
}

TEST_CASE("isColorimetricForIcc rejects channel counts other than three",
          "[core][ColorManagement]")
{
    REQUIRE_FALSE(isColorimetricForIcc(1, DataType::Byte, false));
    REQUIRE_FALSE(isColorimetricForIcc(4, DataType::Byte, false));
    REQUIRE_FALSE(isColorimetricForIcc(0, DataType::Byte, false));
}

TEST_CASE("isColorimetricForIcc rejects data types ColorManagement cannot bind",
          "[core][ColorManagement]")
{
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::Float32, false));
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::Int16, false));
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::None, false));
}
