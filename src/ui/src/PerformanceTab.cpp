#include "slideio/viewer/ui/PerformanceTab.h"

#include "slideio/viewer/ui/ReadingSettings.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct PerformanceTab::Impl
{
    QSpinBox* threadsSpin = nullptr;
};

PerformanceTab::PerformanceTab(QWidget* parent)
    : QWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->threadsSpin = new QSpinBox(this);
    m_impl->threadsSpin->setRange(kMinThreadPoolSize, kMaxThreadPoolSize);
    m_impl->threadsSpin->setValue(readThreadPoolSize());

    auto* form = new QFormLayout;
    form->addRow("Reading threads:", m_impl->threadsSpin);

    auto* group = new QGroupBox("Image reading", this);
    group->setLayout(form);

    auto* note = new QLabel("Applies to the next opened slide.", this);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addWidget(note);
    layout->addStretch();
}

PerformanceTab::~PerformanceTab() = default;

void PerformanceTab::apply()
{
    saveThreadPoolSize(m_impl->threadsSpin->value());
}

} // namespace slideio::viewer::ui
