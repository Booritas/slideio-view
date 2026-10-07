#pragma once

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

// Non-empty only when `info.displacedEmbeddedProfile` is set, naming the
// embedded profile that a per-slide override pushed aside. Free function
// (rather than a panel method) so it can be asserted on without a live panel.
std::string displacedProfileNote(const core::SlideInfo& info);

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
    void setActiveColorProfile(const core::ColorProfileInfo& info,
                               core::ColorProfileOrigin origin, bool displacedEmbedded);

private:
    // Removes any existing top-level "Color profile" item, then adds a fresh
    // one built from `info`. Shared by setSlideInfo (the profile at open time)
    // and setActiveColorProfile (the profile of the scene currently selected
    // for reading, which can differ after a colour-management toggle).
    void rebuildColorProfileRows(const core::ColorProfileInfo& info,
                                 core::ColorProfileOrigin origin, bool displacedEmbedded);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
