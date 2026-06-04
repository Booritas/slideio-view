#include "slideio/viewer/ui/SettingsDialog.h"

#include "slideio/viewer/ui/LogsTab.h"
#include "slideio/viewer/ui/PerformanceTab.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct SettingsDialog::Impl
{
    QTabWidget* tabs = nullptr;
    LogsTab* logsTab = nullptr;
    PerformanceTab* performanceTab = nullptr;
};

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle("Settings");
    setModal(true);

    m_impl->logsTab = new LogsTab(this);
    m_impl->performanceTab = new PerformanceTab(this);
    m_impl->tabs = new QTabWidget(this);
    m_impl->tabs->addTab(m_impl->logsTab, "Logs");
    m_impl->tabs->addTab(m_impl->performanceTab, "Performance");

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_impl->tabs);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        m_impl->logsTab->apply();
        m_impl->performanceTab->apply();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        m_impl->logsTab->apply();
        m_impl->performanceTab->apply();
    });
}

SettingsDialog::~SettingsDialog() = default;

} // namespace slideio::viewer::ui
