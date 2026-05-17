#include "slideio/viewer/ui/MetadataPanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

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

// Hide rows whose Property and Value columns do not contain `needle`,
// keeping ancestor rows of any matching descendant visible. Returns true
// if the item or any descendant should remain visible.
bool applyFilterToItem(QTreeWidgetItem* item, const QString& needle)
{
    bool descendantVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (applyFilterToItem(item->child(i), needle)) {
            descendantVisible = true;
        }
    }
    const bool selfMatches = needle.isEmpty()
        || item->text(0).contains(needle, Qt::CaseInsensitive)
        || item->text(1).contains(needle, Qt::CaseInsensitive);
    const bool visible = selfMatches || descendantVisible;
    item->setHidden(!visible);
    return visible;
}

QJsonValue metadataNodeToJson(const slideio::viewer::core::MetadataNode& node)
{
    using Type = slideio::viewer::core::MetadataNode::Type;
    switch (node.type) {
    case Type::Null:
        return QJsonValue(QJsonValue::Null);
    case Type::Bool:
        return QJsonValue(node.value == "true");
    case Type::Int: {
        // SlideIOAdapter writes via std::to_string, so this round-trips exactly.
        const QString text = QString::fromStdString(node.value);
        bool ok = false;
        const qint64 v = text.toLongLong(&ok);
        return ok ? QJsonValue(v) : QJsonValue(text);
    }
    case Type::Double: {
        // SlideIOAdapter formats with %.15g; parse back to a numeric JSON value.
        const QString text = QString::fromStdString(node.value);
        bool ok = false;
        const double v = text.toDouble(&ok);
        return ok ? QJsonValue(v) : QJsonValue(text);
    }
    case Type::String:
        return QJsonValue(QString::fromStdString(node.value));
    case Type::Array: {
        QJsonArray arr;
        for (const auto& child : node.children) {
            arr.append(metadataNodeToJson(child));
        }
        return arr;
    }
    case Type::Object: {
        QJsonObject obj;
        for (const auto& child : node.children) {
            obj.insert(QString::fromStdString(child.name), metadataNodeToJson(child));
        }
        return obj;
    }
    }
    return QJsonValue(QJsonValue::Null);
}

} // namespace

namespace slideio::viewer::ui
{

struct MetadataPanel::Impl
{
    QLineEdit* filterEdit = nullptr;
    QTreeWidget* tree = nullptr;
    // Retained from the last setSlideInfo() so "Copy metadata as JSON" can
    // serialize the original typed tree instead of the display-string view
    // exposed by the QTreeWidget.
    core::MetadataNode slideMetadata;
    core::MetadataNode sceneMetadata;
    core::MetadataNode channelMetadata;
    bool hasInfo = false;

    void applyFilter();
};

void MetadataPanel::Impl::applyFilter()
{
    const QString needle = filterEdit->text().trimmed();
    const int topCount = tree->topLevelItemCount();
    for (int i = 0; i < topCount; ++i) {
        applyFilterToItem(tree->topLevelItem(i), needle);
    }
    if (needle.isEmpty()) {
        // Restore default state: top-level roots expanded, descendants collapsed.
        tree->collapseAll();
        for (int i = 0; i < topCount; ++i) {
            tree->topLevelItem(i)->setExpanded(true);
        }
    } else {
        tree->expandAll();
    }
}

MetadataPanel::MetadataPanel(QWidget* parent)
    : QDockWidget("Metadata", parent)
    , m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("MetadataPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_impl->filterEdit = new QLineEdit(container);
    m_impl->filterEdit->setPlaceholderText(QStringLiteral("Filter…"));
    m_impl->filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_impl->filterEdit);

    m_impl->tree = new QTreeWidget(container);
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
    layout->addWidget(m_impl->tree);

    setWidget(container);

    connect(m_impl->filterEdit, &QLineEdit::textChanged, this,
        [this](const QString&) { m_impl->applyFilter(); });

    m_impl->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_impl->tree, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            QMenu menu(m_impl->tree);
            const bool hasContent = m_impl->tree->topLevelItemCount() > 0;
            QAction* copyTextAction = menu.addAction(QStringLiteral("Copy metadata as text"));
            copyTextAction->setEnabled(hasContent);
            QAction* copyJsonAction = menu.addAction(QStringLiteral("Copy metadata as JSON"));
            copyJsonAction->setEnabled(hasContent && m_impl->hasInfo);
            QAction* picked = menu.exec(m_impl->tree->viewport()->mapToGlobal(pos));
            if (picked == copyTextAction) {
                QStringList lines;
                const int topCount = m_impl->tree->topLevelItemCount();
                for (int i = 0; i < topCount; ++i) {
                    appendItemAsText(m_impl->tree->topLevelItem(i), 0, lines);
                }
                QApplication::clipboard()->setText(lines.join(QChar('\n')));
            } else if (picked == copyJsonAction) {
                QJsonObject root;
                root.insert(QStringLiteral("Slide"), metadataNodeToJson(m_impl->slideMetadata));
                root.insert(QStringLiteral("Scene"), metadataNodeToJson(m_impl->sceneMetadata));
                root.insert(QStringLiteral("Channels"), metadataNodeToJson(m_impl->channelMetadata));
                const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
                QApplication::clipboard()->setText(QString::fromUtf8(json));
            }
        });
}

MetadataPanel::~MetadataPanel() = default;

void MetadataPanel::clear()
{
    m_impl->tree->clear();
    m_impl->slideMetadata = {};
    m_impl->sceneMetadata = {};
    m_impl->channelMetadata = {};
    m_impl->hasInfo = false;
    m_impl->filterEdit->clear();
}

void MetadataPanel::setSlideInfo(const core::SlideInfo& info)
{
    m_impl->tree->clear();
    m_impl->slideMetadata = info.slideMetadata;
    m_impl->sceneMetadata = info.sceneMetadata;
    m_impl->channelMetadata = info.channelMetadata;
    m_impl->hasInfo = true;
    addRootNode(m_impl->tree, QStringLiteral("Slide"), m_impl->slideMetadata);
    addRootNode(m_impl->tree, QStringLiteral("Scene"), m_impl->sceneMetadata);
    addRootNode(m_impl->tree, QStringLiteral("Channels"), m_impl->channelMetadata);
    m_impl->applyFilter();
}

} // namespace slideio::viewer::ui
