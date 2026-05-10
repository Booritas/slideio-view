#pragma once

#include "slideio/viewer/core/Types.h"

#include <QDockWidget>

#include <memory>

namespace slideio::viewer::ui
{

// Read-only docking panel that shows the parsed metadata for the current
// slide and active scene as a two-column tree (Property / Value). The two
// top-level items "Slide" and "Scene" expand into the corresponding
// core::MetadataNode trees carried on core::SlideInfo.
class MetadataPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit MetadataPanel(QWidget* parent = nullptr);
    ~MetadataPanel() override;

    MetadataPanel(const MetadataPanel&) = delete;
    MetadataPanel& operator=(const MetadataPanel&) = delete;

    void setSlideInfo(const core::SlideInfo& info);
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
