#include "slideio/viewer/ui/MetadataPanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace
{

// Convert a leaf node's stored value to display text. Object/Array nodes
// instead get a synthesized summary ("{N keys}" / "[N items]"). Null gets
// a literal "(null)" so empty leaves are visually distinguishable.
QString valueText(const slideio::viewer::core::MetadataNode& node)
{
    using Type = slideio::viewer::core::MetadataNode::Type;
    switch (node.type) {
    case Type::Null:
        return QStringLiteral("(null)");
    case Type::Bool:
    case Type::Int:
    case Type::Double:
    case Type::String:
        return QString::fromStdString(node.value);
    case Type::Array:
        return QStringLiteral("[%1 items]").arg(node.children.size());
    case Type::Object:
        return QStringLiteral("{%1 keys}").arg(node.children.size());
    }
    return {};
}

void addNode(QTreeWidgetItem* parent,
             const slideio::viewer::core::MetadataNode& node)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, QString::fromStdString(node.name));
    item->setText(1, valueText(node));
    for (const auto& child : node.children) {
        addNode(item, child);
    }
}

// Add a top-level "Slide" or "Scene" parent for the given subtree. If the
// subtree is Null at its root, show a single "(no metadata)" parent with
// no children. Otherwise the parent shows the synthesized summary
// ("{N keys}" / "[N items]") and is expanded by default.
void addRootNode(QTreeWidget* tree, const QString& title,
                 const slideio::viewer::core::MetadataNode& root)
{
    using Type = slideio::viewer::core::MetadataNode::Type;
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, title);
    if (root.type == Type::Null && root.children.empty()) {
        item->setText(1, QStringLiteral("(no metadata)"));
        return;
    }
    item->setText(1, valueText(root));
    for (const auto& child : root.children) {
        addNode(item, child);
    }
    item->setExpanded(true);
}

void appendItemAsText(const QTreeWidgetItem* item, int depth, QStringList& out)
{
    QString indent(depth * 2, QLatin1Char(' '));
    out << QStringLiteral("%1%2: %3").arg(indent, item->text(0), item->text(1));
    for (int i = 0; i < item->childCount(); ++i) {
        appendItemAsText(item->child(i), depth + 1, out);
    }
}

} // namespace

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

    m_impl->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_impl->tree, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            QMenu menu(m_impl->tree);
            QAction* copyAction = menu.addAction(QStringLiteral("Copy metadata as text"));
            copyAction->setEnabled(m_impl->tree->topLevelItemCount() > 0);
            QAction* picked = menu.exec(m_impl->tree->viewport()->mapToGlobal(pos));
            if (picked != copyAction) return;

            QStringList lines;
            const int topCount = m_impl->tree->topLevelItemCount();
            for (int i = 0; i < topCount; ++i) {
                appendItemAsText(m_impl->tree->topLevelItem(i), 0, lines);
            }
            QApplication::clipboard()->setText(lines.join(QChar('\n')));
        });
}

MetadataPanel::~MetadataPanel() = default;

void MetadataPanel::clear()
{
    m_impl->tree->clear();
}

void MetadataPanel::setSlideInfo(const core::SlideInfo& info)
{
    m_impl->tree->clear();
    addRootNode(m_impl->tree, QStringLiteral("Slide"), info.slideMetadata);
    addRootNode(m_impl->tree, QStringLiteral("Scene"), info.sceneMetadata);
    addRootNode(m_impl->tree, QStringLiteral("Channels"), info.channelMetadata);
}

} // namespace slideio::viewer::ui
