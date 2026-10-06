#include "slideio/viewer/core/ColorManagement.h"

namespace slideio::viewer::core
{

namespace
{

constexpr size_t kIccHeaderSize = 128;
constexpr size_t kIccSizeOffset = 0;
constexpr size_t kIccDataSpaceOffset = 16;
constexpr size_t kIccSignatureOffset = 36;

uint32_t readBigEndian32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return (static_cast<uint32_t>(bytes[offset]) << 24)
         | (static_cast<uint32_t>(bytes[offset + 1]) << 16)
         | (static_cast<uint32_t>(bytes[offset + 2]) << 8)
         | static_cast<uint32_t>(bytes[offset + 3]);
}

} // namespace

bool isColorimetricForIcc(int numChannels, DataType dataType, bool fluorescenceHint)
{
    return numChannels == 3
        && (dataType == DataType::Byte || dataType == DataType::UInt16)
        && !fluorescenceHint;
}

std::string colorManagementUnavailableReason(ColorManagementAvailability availability,
                                             const std::string& detail)
{
    switch (availability) {
    case ColorManagementAvailability::Available:
        return {};
    case ColorManagementAvailability::NotColorimetric:
        return "Color management applies to RGB brightfield slides only";
    case ColorManagementAvailability::NoProfile:
        return "This slide embeds no ICC profile, and no default profile is set";
    case ColorManagementAvailability::BindFailed:
        return detail.empty() ? "This slide's color profile could not be used"
                              : "This slide's color profile could not be used: " + detail;
    }
    return {};
}

IccHeaderSummary inspectIccHeader(const std::vector<uint8_t>& bytes)
{
    IccHeaderSummary summary;
    if (bytes.size() < kIccHeaderSize) {
        return summary;
    }
    if (bytes[kIccSignatureOffset] != 'a' || bytes[kIccSignatureOffset + 1] != 'c'
        || bytes[kIccSignatureOffset + 2] != 's' || bytes[kIccSignatureOffset + 3] != 'p') {
        return summary;
    }

    const uint32_t declared = readBigEndian32(bytes, kIccSizeOffset);
    // A header claiming more bytes than the buffer holds is the signature of a
    // truncated file, which lcms2 would reject later and less legibly.
    if (declared < kIccHeaderSize || declared > bytes.size()) {
        return summary;
    }

    summary.plausible = true;
    summary.declaredSize = declared;
    summary.dataSpace.assign(bytes.begin() + kIccDataSpaceOffset,
                             bytes.begin() + kIccDataSpaceOffset + 4);
    return summary;
}

} // namespace slideio::viewer::core
