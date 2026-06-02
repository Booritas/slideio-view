#pragma once

#include <QDialog>

#include <memory>

namespace slideio::viewer::ui
{

// Modal, tabbed application-settings dialog. Currently exposes a single "Logs"
// tab. OK/Apply persist each tab's settings and apply them live; Cancel
// discards. Built to accept additional tabs without changing the shell.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

    SettingsDialog(const SettingsDialog&) = delete;
    SettingsDialog& operator=(const SettingsDialog&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
