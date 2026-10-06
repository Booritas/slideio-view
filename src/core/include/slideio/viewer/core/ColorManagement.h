#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Whether ICC conversion means anything for a scene of this shape.
///
/// Stricter than the check ColorManagement::bindToSource makes, deliberately.
/// That one counts channels and inspects data types, which three 8-bit
/// fluorescence channels satisfy -- it has no way to know the values are
/// intensities rather than colour, and would transform them as though they
/// were R/G/B. Only the viewer holds that fact, so only the viewer can refuse.
///
/// Also note this is not SlideInfo::isBrightfield, which is false for 16-bit
/// RGB brightfield -- a case ColorManagement supports.
bool isColorimetricForIcc(int numChannels, DataType dataType, bool fluorescenceHint);

/// Tooltip text for a disabled "Color management" menu item. Empty when
/// availability is Available.
std::string colorManagementUnavailableReason(ColorManagementAvailability availability,
                                             const std::string& detail);

/// The fields of an ICC profile header this application reads.
struct IccHeaderSummary
{
    bool plausible = false;     ///< parses as an ICC profile header
    std::string dataSpace;      ///< 4-character signature, e.g. "RGB ", "CMYK"
    size_t declaredSize = 0;    ///< profile size the header claims
};

/// Sanity-check a candidate ICC profile, per ICC.1:2010 section 7.2.
///
/// This exists so that choosing a default profile fails at the file dialog,
/// naming the problem, rather than degrading silently to an assumed sRGB
/// several slides later. It is not a parser: lcms2, inside SlideIO, remains
/// the authority on whether a profile is usable.
IccHeaderSummary inspectIccHeader(const std::vector<uint8_t>& bytes);

} // namespace slideio::viewer::core
