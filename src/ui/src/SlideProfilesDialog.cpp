#include "slideio/viewer/ui/SlideProfilesDialog.h"

#include "slideio/viewer/core/ColorManagement.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace slideio::viewer::ui
{

namespace
{

// Matches AboutDialog's kDialogStyle -- this application is dark-themed and its
// dialogs style themselves rather than relying on a global palette. Without
// this the table would come up in the platform's light theme in the middle of
// a dark application. Extended here for the table and its header, which
// AboutDialog has no need of.
const char* const kDialogStyle =
    "QDialog { background: #2D2D2D; }"
    "QLabel { color: #CCCCCC; }"
    "QTableWidget { background: #252525; color: #CCCCCC; border: 1px solid #555555;"
    " gridline-color: #555555; alternate-background-color: #2D2D2D; }"
    "QTableWidget::item:selected { background: #4A90D9; color: #FFFFFF; }"
    "QHeaderView::section { background: #3C3C3C; color: #CCCCCC; padding: 4px 8px;"
    " border: 1px solid #555555; }"
    "QPushButton { background: #3C3C3C; color: #CCCCCC; border: 1px solid #555555; padding: 5px 14px; }"
    "QPushButton:hover { background: #4A4A4A; }"
    "QPushButton:pressed { background: #4A90D9; color: #FFFFFF; }";

} // namespace

SlideProfileRow buildSlideProfileRow(const core::ColorProfileOverride& entry)
{
    SlideProfileRow row;
    row.slideName = entry.slideDisplayName.empty() ? entry.slideId : entry.slideDisplayName;
    row.profilePath = entry.profilePath;
    row.displacesEmbedded = entry.displacedEmbedded;

    QFile file(QString::fromStdString(entry.profilePath));
    const bool readable = file.open(QIODevice::ReadOnly);

    std::vector<uint8_t> bytes;
    if (readable) {
        const QByteArray raw = file.readAll();
        bytes.assign(raw.begin(), raw.end());
    }

    // The same classification the open path uses, so the dialog and the slide
    // can never disagree about whether a profile is usable.
    switch (core::classifyDefaultProfile(readable, core::inspectIccHeader(bytes))) {
        case core::DefaultProfileStatus::Ok:           row.status = "OK";            break;
        case core::DefaultProfileStatus::Unreadable:   row.status = "File missing";  break;
        case core::DefaultProfileStatus::NotAProfile:  row.status = "Not a profile"; break;
        case core::DefaultProfileStatus::NotRgb:       row.status = "Not RGB";       break;
    }
    return row;
}

struct SlideProfilesDialog::Impl
{
    SlideProfilesDialog* owner = nullptr;
    core::IColorProfileOverrideStore* store = nullptr;
    QTableWidget* table = nullptr;
    std::vector<std::string> rowSlideIds; // parallel to the table's rows

    void reload()
    {
        const std::vector<core::ColorProfileOverride> entries = store->all();
        rowSlideIds.clear();
        table->setRowCount(static_cast<int>(entries.size()));

        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const SlideProfileRow row = buildSlideProfileRow(entries[static_cast<size_t>(i)]);
            rowSlideIds.push_back(entries[static_cast<size_t>(i)].slideId);

            table->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(row.slideName)));
            table->setItem(i, 1, new QTableWidgetItem(
                QDir::toNativeSeparators(QString::fromStdString(row.profilePath))));
            table->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(row.status)));
            table->setItem(i, 3, new QTableWidgetItem(
                row.displacesEmbedded ? QObject::tr("displaces embedded") : QString()));
        }

        // Default column widths are too narrow for a slide name or a full path;
        // widen the first three to fit what was just loaded (the fourth already
        // stretches to fill the rest of the table).
        table->resizeColumnsToContents();
        table->setColumnWidth(0, std::max(table->columnWidth(0), 160));
        table->setColumnWidth(1, std::max(table->columnWidth(1), 300));
    }

    void removeSelected()
    {
        const int selected = table->currentRow();
        if (selected < 0 || selected >= static_cast<int>(rowSlideIds.size())) {
            return;
        }
        store->remove(rowSlideIds[static_cast<size_t>(selected)]);
        reload();
        emit owner->overridesChanged();
    }

    void removeAll()
    {
        if (QMessageBox::question(
                owner, QObject::tr("Slide ICC Profiles"),
                QObject::tr("Remove every stored per-slide color profile?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        // all() returns by value, so removing while iterating this copy is safe.
        for (const auto& entry : store->all()) {
            store->remove(entry.slideId);
        }
        reload();
        emit owner->overridesChanged();
    }
};

SlideProfilesDialog::SlideProfilesDialog(core::IColorProfileOverrideStore& store, QWidget* parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;
    m_impl->store = &store;

    setWindowTitle(tr("Slide ICC Profiles"));
    setStyleSheet(QString::fromLatin1(kDialogStyle));
    resize(900, 360);

    m_impl->table = new QTableWidget(0, 4, this);
    m_impl->table->setHorizontalHeaderLabels(
        {tr("Slide"), tr("Profile"), tr("Status"), QString()});
    m_impl->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_impl->table->setAlternatingRowColors(true);
    m_impl->table->horizontalHeader()->setStretchLastSection(true);

    auto* removeBtn = new QPushButton(tr("Remove"), this);
    auto* removeAllBtn = new QPushButton(tr("Remove All"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);

    connect(removeBtn, &QPushButton::clicked, this, [this]() { m_impl->removeSelected(); });
    connect(removeAllBtn, &QPushButton::clicked, this, [this]() { m_impl->removeAll(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(removeBtn);
    buttons->addWidget(removeAllBtn);
    buttons->addStretch();
    buttons->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_impl->table);
    layout->addLayout(buttons);

    m_impl->reload();
}

SlideProfilesDialog::~SlideProfilesDialog() = default;

} // namespace slideio::viewer::ui
