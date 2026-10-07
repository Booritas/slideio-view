#pragma once

#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"

#include <QDockWidget>

#include <memory>
#include <string>

namespace slideio::viewer::ui
{

// Display name for ColorProfileOrigin, for the "Origin" row: ColorProfileSource
// reports a global default and a per-slide override identically (both
// `Supplied`), so this is the only place that tells the user which is in play.
const char* colorProfileOriginName(core::ColorProfileOrigin origin);

// Label for the "Origin" row. The row names the provenance of the profile that
// would be bound were colour management on, which colorMode == Managed matches
// -- but with it Raw, the slide is showing unconverted pixels, and an
// unqualified "Origin" label would assert a provenance for what's on screen
// that isn't true. Free function so the label can be asserted on without a
// live panel.
const char* originRowLabel(core::ColorMode colorMode);

// Non-empty only when `displaced` is set, naming `embeddedDescription` as the
// embedded profile that a per-slide override pushed aside. Takes the raw
// values rather than a SlideInfo so the caller cannot hand it an active
// profile's description by mistake -- that was the bug: `embeddedDescription`
// must come from the raw scene (slideInfo().colorProfileInfo.description),
// never from the profile currently active, which under an override is the
// override itself. Free function (rather than a panel method) so it can be
// asserted on without a live panel.
std::string displacedProfileNote(bool displaced, const std::string& embeddedDescription);

// Read-only docking panel that shows the metadata for the currently-open slide
// (file name, driver, dimensions, magnification, channels, Z/T, compression,
// pyramid levels, …) in a two-column tree.
class SlidePropertiesPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit SlidePropertiesPanel(QWidget* parent = nullptr);
    ~SlidePropertiesPanel() override;

    SlidePropertiesPanel(const SlidePropertiesPanel&) = delete;
    SlidePropertiesPanel& operator=(const SlidePropertiesPanel&) = delete;

    void setSlideInfo(const core::SlideInfo& info);
    void clear();

    // Replaces the colour-profile rows with the profile of the scene currently
    // being read, leaving every other row as setSlideInfo left it. `origin` and
    // `displacedEmbedded` come from the SlideInfo captured at open -- the active
    // scene's ColorProfileInfo carries neither -- so the caller passes
    // slideInfo().colorProfileOrigin / displacedEmbeddedProfile alongside it.
    // `embeddedDescription` is slideInfo().colorProfileInfo.description, which
    // SlideIOAdapter populates from the raw scene before any override or
    // default is bound, and never overwrites -- so it stays the embedded
    // profile's own description even while `info` (the active profile) is an
    // override. `colorMode` is the viewport's current ColorMode: with it Raw,
    // the slide is showing unconverted pixels, so the "Origin" row is relabelled
    // to say it describes what would apply if colour management were on, not
    // what is on screen now.
    void setActiveColorProfile(const core::ColorProfileInfo& info,
                               core::ColorProfileOrigin origin, bool displacedEmbedded,
                               const std::string& embeddedDescription,
                               core::ColorMode colorMode);

private:
    // Removes any existing top-level "Color profile" item, then adds a fresh
    // one built from `info`. Shared by setSlideInfo (the profile at open time)
    // and setActiveColorProfile (the profile of the scene currently selected
    // for reading, which can differ after a colour-management toggle).
    void rebuildColorProfileRows(const core::ColorProfileInfo& info,
                                 core::ColorProfileOrigin origin, bool displacedEmbedded,
                                 const std::string& embeddedDescription,
                                 core::ColorMode colorMode);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
