#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// One user decision: this slide is to be displayed through this ICC profile.
struct ColorProfileOverride
{
    std::string slideId;            ///< computeSlideId result
    std::string profilePath;        ///< absolute path to the .icc/.icm file
    std::string slideDisplayName;   ///< last-known filename, for the dialog only
    bool displacedEmbedded = false; ///< set over a slide that embeds a profile
};

/// Persistence of the user's per-slide profile choices.
///
/// `slideDisplayName` is carried for display alone and is never matched
/// against, so a renamed slide shows a stale name in the manage dialog rather
/// than losing its override.
class IColorProfileOverrideStore
{
public:
    virtual ~IColorProfileOverrideStore() = default;

    virtual std::optional<ColorProfileOverride> find(const std::string& slideId) const = 0;
    virtual void set(const ColorProfileOverride& entry) = 0;
    virtual void remove(const std::string& slideId) = 0;
    virtual std::vector<ColorProfileOverride> all() const = 0;
};

/// The profile bytes handed to an adapter, and the authority they carry.
///
/// The distinction is the whole feature: a global default stands in only for a
/// slide that embeds nothing, while a per-slide override displaces whatever the
/// slide embeds. `isSlideOverride` is what buildManagedScene consults to tell
/// the two apart.
struct SuppliedColorProfile
{
    std::vector<uint8_t> bytes;
    bool isSlideOverride = false;
};

} // namespace slideio::viewer::core
