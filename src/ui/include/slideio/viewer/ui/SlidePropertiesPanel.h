#pragma once

#include "slideio/viewer/core/Types.h"

#include <QDockWidget>

#include <memory>

namespace slideio::viewer::ui
{

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
    // being read, leaving every other row as setSlideInfo left it.
    void setActiveColorProfile(const core::ColorProfileInfo& info);

private:
    // Removes any existing top-level "Color profile" item, then adds a fresh
    // one built from `info`. Shared by setSlideInfo (the profile at open time)
    // and setActiveColorProfile (the profile of the scene currently selected
    // for reading, which can differ after a colour-management toggle).
    void rebuildColorProfileRows(const core::ColorProfileInfo& info);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
