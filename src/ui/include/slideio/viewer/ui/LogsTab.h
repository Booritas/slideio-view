#pragma once

#include <QWidget>

#include <memory>

namespace slideio::viewer::ui
{

// The "Logs" tab of the Settings dialog: shows the log file location (with
// open buttons), an application log-level dropdown, and a performance-logging
// on/off dropdown. Selections are loaded from QSettings on construction and
// written back (and applied to the live loggers) by apply().
class LogsTab : public QWidget
{
    Q_OBJECT

public:
    explicit LogsTab(QWidget* parent = nullptr);
    ~LogsTab() override;

    LogsTab(const LogsTab&) = delete;
    LogsTab& operator=(const LogsTab&) = delete;

    // Persist the current selections to QSettings and apply them to the live
    // "viewer" and "perf" loggers.
    void apply();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
