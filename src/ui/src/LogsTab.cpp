#include "slideio/viewer/ui/LogsTab.h"

#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/LogSettings.h"

#include <spdlog/common.h> // spdlog::level::level_enum

#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct LogsTab::Impl
{
    QLineEdit* pathEdit = nullptr;
    QComboBox* appLevelCombo = nullptr;
    QComboBox* perfCombo = nullptr;
};

LogsTab::LogsTab(QWidget* parent)
    : QWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    // --- Log file group ---
    m_impl->pathEdit = new QLineEdit(this);
    m_impl->pathEdit->setReadOnly(true);
    m_impl->pathEdit->setText(logFilePath());

    auto* openFileButton = new QPushButton("Open Log File", this);
    auto* openFolderButton = new QPushButton("Open Folder", this);

    auto* fileButtons = new QHBoxLayout;
    fileButtons->addWidget(openFileButton);
    fileButtons->addWidget(openFolderButton);
    fileButtons->addStretch();

    auto* fileLayout = new QVBoxLayout;
    fileLayout->addWidget(m_impl->pathEdit);
    fileLayout->addLayout(fileButtons);

    auto* fileGroup = new QGroupBox("Log file", this);
    fileGroup->setLayout(fileLayout);

    // --- Levels group ---
    m_impl->appLevelCombo = new QComboBox(this);
    m_impl->appLevelCombo->addItem("Trace", "trace");
    m_impl->appLevelCombo->addItem("Debug", "debug");
    m_impl->appLevelCombo->addItem("Info", "info");
    m_impl->appLevelCombo->addItem("Warning", "warn");
    m_impl->appLevelCombo->addItem("Error", "error");
    m_impl->appLevelCombo->addItem("Off", "off");

    m_impl->perfCombo = new QComboBox(this);
    m_impl->perfCombo->addItem("Do not log", QVariant(false));
    m_impl->perfCombo->addItem("Log performance data", QVariant(true));

    auto* levelsForm = new QFormLayout;
    levelsForm->addRow("Application log level:", m_impl->appLevelCombo);
    levelsForm->addRow("Performance logging:", m_impl->perfCombo);

    auto* levelsGroup = new QGroupBox("Logging levels", this);
    levelsGroup->setLayout(levelsForm);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(fileGroup);
    layout->addWidget(levelsGroup);
    layout->addStretch();

    // --- Load current selections from QSettings ---
    // Select the persisted level; if none is saved (findData returns -1),
    // fall back to "debug" looked up by data so this does not depend on
    // the combo's item order.
    const QString appLevelData = QString::fromStdString(readAppLevelSetting());
    int appIdx = m_impl->appLevelCombo->findData(appLevelData);
    if (appIdx < 0) {
        appIdx = m_impl->appLevelCombo->findData(QString("debug"));
    }
    m_impl->appLevelCombo->setCurrentIndex(appIdx >= 0 ? appIdx : 0);

    // This reflects the *persisted* perf setting. When SLIDEIO_PERF_LOG is set,
    // it overrides the persisted value at startup only (see main.cpp) — so the
    // live logger state can differ from what is shown here during such a run.
    // The dialog intentionally edits the persisted setting, not the session
    // override; do not "fix" this to read the live logger level.
    int perfIdx = m_impl->perfCombo->findData(readPerfEnabledSetting());
    m_impl->perfCombo->setCurrentIndex(perfIdx >= 0 ? perfIdx : 0 /* Do not log */);

    // --- Open buttons (reuse the MainWindow open-log pattern) ---
    connect(openFileButton, &QPushButton::clicked, this, [this]() {
        const QString path = logFilePath();
        if (!QFile::exists(path)) {
            QMessageBox::information(this, "Open Log File",
                QString("Log file does not exist yet:\n%1").arg(path));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            QMessageBox::warning(this, "Open Log File",
                QString("Failed to open log file:\n%1").arg(path));
        }
    });

    connect(openFolderButton, &QPushButton::clicked, this, [this]() {
        const QString dir = logDirectory();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            QMessageBox::warning(this, "Open Folder",
                QString("Failed to open log folder:\n%1").arg(dir));
        }
    });
}

LogsTab::~LogsTab() = default;

void LogsTab::apply()
{
    const spdlog::level::level_enum level =
        levelFromString(m_impl->appLevelCombo->currentData().toString().toStdString());
    const bool perfEnabled = m_impl->perfCombo->currentData().toBool();

    saveLogSettings(level, perfEnabled);
    applyAppLevel(level);
    applyPerfEnabled(perfEnabled);
}

} // namespace slideio::viewer::ui
