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

/// Whether nominating an ICC profile for one slide could ever change how that
/// slide is displayed.
///
/// False only for NotColorimetric. Those channels are intensities rather than
/// colour, and the viewer refuses to transform them whatever profile it is
/// handed, so an override stored against such a slide is an entry that reads
/// everywhere as configured and can never apply.
///
/// NoProfile is deliberately true: a slide that embeds nothing, with no default
/// to stand in, is precisely the case a per-slide override exists for, and
/// supplying one is what makes colour management available at the next open.
/// Gating on Available instead would make the feature unreachable for exactly
/// the slides that need it. BindFailed likewise -- a different profile may bind
/// where the present one did not.
bool slideProfileOverrideCanApply(ColorManagementAvailability availability);

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

/// State of the user's configured default ICC profile, as found on disk now.
///
/// The setting stores a path, so the file it names can change after it was
/// chosen. Anything but Ok means the bytes must not be handed to SlideIO: it
/// treats an unusable override as absence, and the resulting message blames
/// the slide for embedding no profile -- which is true, and is precisely why
/// the default was being consulted.
enum class DefaultProfileStatus
{
    Ok,
    Unreadable,    ///< the path could not be opened at all
    NotAProfile,   ///< the bytes are not an ICC profile
    NotRgb,        ///< a valid profile, but not of RGB data
};

/// Classify a configured default profile. `readable` says whether the file was
/// opened; `summary` is meaningful only when it was.
DefaultProfileStatus classifyDefaultProfile(bool readable, const IccHeaderSummary& summary);

/// One sentence naming the default-profile setting and what is wrong with it,
/// for the user who would otherwise be told their slide is at fault. Empty for
/// DefaultProfileStatus::Ok.
std::string defaultProfileProblemText(DefaultProfileStatus status, const std::string& path);

/// The same, for a profile the user nominated for one particular slide.
///
/// A separate function rather than a parameter on the one above because the
/// repair is in a different place: the default profile is a single setting in
/// the View menu, while a per-slide override is only reachable through the
/// "Manage Slide ICC Profiles" dialog. Telling the user to fix the default
/// setting points them at a setting they may never have touched. Empty for
/// DefaultProfileStatus::Ok.
std::string slideProfileProblemText(DefaultProfileStatus status, const std::string& path);

} // namespace slideio::viewer::core
