#include "slideio/viewer/ui/MetadataPanel.h"

#include <QHeaderView>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace slideio::viewer::ui
{

struct MetadataPanel::Impl
{
    QTreeWidget* tree = nullptr;
};

MetadataPanel::MetadataPanel(QWidget* parent)
    : QDockWidget("Metadata", parent)
    , m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("MetadataPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_impl->tree = new QTreeWidget(this);
    m_impl->tree->setColumnCount(2);
    m_impl->tree->setHeaderLabels(QStringList() << "Property" << "Value");
    m_impl->tree->setRootIsDecorated(true);
    m_impl->tree->setUniformRowHeights(true);
    m_impl->tree->setAlternatingRowColors(true);
    m_impl->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_impl->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_impl->tree->header()->setStretchLastSection(true);
    setWidget(m_impl->tree);
}

MetadataPanel::~MetadataPanel() = default;

void MetadataPanel::clear()
{
    m_impl->tree->clear();
}

void MetadataPanel::setSlideInfo(const core::SlideInfo& /*info*/)
{
    // Population implemented in the next task.
    m_impl->tree->clear();
}

} // namespace slideio::viewer::ui
