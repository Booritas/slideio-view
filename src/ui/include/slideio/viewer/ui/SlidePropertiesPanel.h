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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
