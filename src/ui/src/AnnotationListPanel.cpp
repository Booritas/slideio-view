#include "slideio/viewer/ui/AnnotationListPanel.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/ui/AnnotationListPolicy.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QComboBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kIdRole = Qt::UserRole;
}

struct AnnotationListPanel::Impl
{
    app::AnnotationModel* model = nullptr;
    QLineEdit* filter = nullptr;
    QComboBox* sort = nullptr;
    QTreeWidget* tree = nullptr;

    /// The id applySelection last highlighted. The list scrolls to the selected
    /// row only when this changes, so a model change that leaves the selection
    /// alone does not yank the list back from wherever the user scrolled it.
    std::string lastScrolledId;
};

AnnotationListPanel::AnnotationListPanel(QWidget* parent)
    : QDockWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle(QString::fromStdString(annotationDockTitle(0, 0)));

    auto* contents = new QWidget(this);
    auto* layout = new QVBoxLayout(contents);

    m_impl->filter = new QLineEdit(contents);
    m_impl->filter->setPlaceholderText(tr("Filter by label or classification"));
    m_impl->filter->setClearButtonEnabled(true);
    layout->addWidget(m_impl->filter);

    m_impl->sort = new QComboBox(contents);
    m_impl->sort->addItem(tr("Creation order"), static_cast<int>(AnnotationSortKey::CreationOrder));
    m_impl->sort->addItem(tr("Label"), static_cast<int>(AnnotationSortKey::Label));
    m_impl->sort->addItem(tr("Classification"), static_cast<int>(AnnotationSortKey::Classification));
    m_impl->sort->addItem(tr("Type"), static_cast<int>(AnnotationSortKey::Type));
    m_impl->sort->addItem(tr("Last modified"), static_cast<int>(AnnotationSortKey::ModifiedTime));
    layout->addWidget(m_impl->sort);

    m_impl->tree = new QTreeWidget(contents);
    m_impl->tree->setColumnCount(3);
    m_impl->tree->setHeaderLabels({tr("Label"), tr("Type"), tr("Classification")});
    m_impl->tree->setRootIsDecorated(false);
    m_impl->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->tree->header()->setStretchLastSection(true);
    layout->addWidget(m_impl->tree);

    setWidget(contents);

    connect(m_impl->filter, &QLineEdit::textChanged, this, [this](const QString&) { rebuild(); });
    connect(m_impl->sort, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { rebuild(); });

    connect(m_impl->tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_impl->model == nullptr) {
            return;
        }
        const QList<QTreeWidgetItem*> selected = m_impl->tree->selectedItems();
        m_impl->model->setSelected(selected.isEmpty()
                                       ? std::string{}
                                       : selected.first()->data(0, kIdRole).toString().toStdString());
    });

    connect(m_impl->tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item != nullptr) {
                    emit annotationActivated(item->data(0, kIdRole).toString().toStdString());
                }
            });
}

AnnotationListPanel::~AnnotationListPanel() = default;

void AnnotationListPanel::setAnnotationModel(app::AnnotationModel* model)
{
    if (m_impl->model == model) {
        return;
    }
    if (m_impl->model != nullptr) {
        disconnect(m_impl->model, nullptr, this, nullptr);
    }
    m_impl->model = model;

    if (m_impl->model != nullptr) {
        // Every signal that changes what the rows should say rebuilds them.
        // selectionChanged deliberately does not: it moves a highlight, and
        // rebuilding would destroy the item under the user's cursor mid-click.
        const auto rebuildRows = [this]() { rebuild(); };
        connect(m_impl->model, &app::AnnotationModel::annotationAdded, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::annotationRemoved, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::annotationChanged, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::cleared, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::modelReset, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::selectionChanged, this,
                [this](const std::string& id) { applySelection(id); });
    }
    rebuild();
}

void AnnotationListPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    // rebuild() does nothing while hidden, so whatever changed in the model
    // since the dock was last visible is applied now.
    //
    // The scroll memory is dropped so a later selection change scrolls rather
    // than deciding the selection has not moved. It does NOT bring the selected
    // row into view on opening: rebuild() restores the scroll offset the list
    // had when it was last visible, which happens after applySelection and
    // therefore wins. Reopening where the user left it is the behaviour; an
    // earlier version of this comment claimed the opposite.
    m_impl->lastScrolledId.clear();
    rebuild();
}

void AnnotationListPanel::rebuild()
{
    // Hidden docks skip the work: a drag in the viewport emits annotationChanged
    // per mouse-move, and rebuilding (clear, N items, a sort) for each would be
    // paid by a panel nobody can see. showEvent() catches up. The title goes
    // unrefreshed too, which costs nothing -- it is not visible either.
    if (!isVisible()) {
        return;
    }

    const QSignalBlocker blocker(m_impl->tree);
    // clear() resets the scroll position; kept so a rebuild does not move the list.
    const int scrollValue = m_impl->tree->verticalScrollBar()->value();
    m_impl->tree->clear();

    if (m_impl->model == nullptr) {
        setWindowTitle(QString::fromStdString(annotationDockTitle(0, 0)));
        return;
    }

    const auto sortKey = static_cast<AnnotationSortKey>(m_impl->sort->currentData().toInt());
    const auto typeName = [](core::AnnotationType type) { return shapeTypeName(type).toStdString(); };

    const std::vector<core::Annotation>& annotations = m_impl->model->annotations();
    const auto rows = annotationRows(annotations, m_impl->filter->text().toStdString(), sortKey,
                                     typeName);

    for (const AnnotationRow& row : rows) {
        auto* item = new QTreeWidgetItem(m_impl->tree);
        item->setText(0, QString::fromStdString(row.label));
        item->setText(1, QString::fromStdString(row.type));
        item->setText(2, QString::fromStdString(row.classification));
        item->setData(0, kIdRole, QString::fromStdString(row.id));
    }

    // Deliberately untranslated: annotationDockTitle() must stay Qt-free so it
    // can be tested, which leaves it no way to call tr(). Translating the title
    // would mean moving the formatting into this panel.
    setWindowTitle(QString::fromStdString(annotationDockTitle(annotations.size(), rows.size())));
    applySelection(m_impl->model->selectedId());
    m_impl->tree->verticalScrollBar()->setValue(scrollValue);
}

void AnnotationListPanel::applySelection(const std::string& id)
{
    // Blocked so highlighting the model's selection does not look like the user
    // clicking a row, which would call setSelected again. setSelected returns
    // early on an unchanged id so the loop would terminate anyway, but relying
    // on that makes this panel's correctness a fact about another class.
    const QSignalBlocker blocker(m_impl->tree);
    m_impl->tree->clearSelection();
    if (id.empty()) {
        m_impl->lastScrolledId.clear();
        return;
    }
    const bool selectionMoved = (id != m_impl->lastScrolledId);
    m_impl->lastScrolledId = id;
    const QString target = QString::fromStdString(id);
    for (int i = 0; i < m_impl->tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_impl->tree->topLevelItem(i);
        if (item->data(0, kIdRole).toString() == target) {
            item->setSelected(true);
            if (selectionMoved) {
                m_impl->tree->scrollToItem(item);
            }
            return;
        }
    }
}

void AnnotationListPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete && m_impl->tree->hasFocus()) {
        const QList<QTreeWidgetItem*> selected = m_impl->tree->selectedItems();
        if (!selected.isEmpty()) {
            emit deleteRequested(selected.first()->data(0, kIdRole).toString().toStdString());
            event->accept();
            return;
        }
    }
    QDockWidget::keyPressEvent(event);
}

} // namespace slideio::viewer::ui
