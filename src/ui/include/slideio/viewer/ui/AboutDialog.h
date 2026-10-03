#pragma once

#include "slideio/viewer/ui/GpuInfo.h"

#include <QDialog>

#include <memory>

namespace slideio::viewer::ui
{

// Shows what this build is: application and library versions, the machine and
// renderer it is running on, the licences of what it ships, and a button that
// puts the diagnostic part of that on the clipboard for a bug report.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    // gpu comes from ViewportWidget::glInfo(); pass a default-constructed one
    // when there is no viewport to ask.
    explicit AboutDialog(const GpuInfo& gpu, QWidget* parent = nullptr);
    ~AboutDialog() override;

    AboutDialog(const AboutDialog&) = delete;
    AboutDialog& operator=(const AboutDialog&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
