#pragma once

#include <QDialog>

#include <memory>

namespace slideio::viewer::ui
{

// Help > License: the viewer's own licence and the components it ships with,
// under a notice that stays in view whichever tab is open.
class LicenseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseDialog(QWidget* parent = nullptr);
    ~LicenseDialog() override;

    LicenseDialog(const LicenseDialog&) = delete;
    LicenseDialog& operator=(const LicenseDialog&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
