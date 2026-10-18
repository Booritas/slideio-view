#include "slideio/viewer/ui/AnnotationListPanel.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/ui/AnnotationListPolicy.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QAction>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
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
    QComboBox* counting = nullptr;
    QTreeWidget* tally = nullptr;
    std::vector<std::string> categories;

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

    m_impl->counting = new QComboBox(contents);
    m_impl->counting->setEditable(true);
    m_impl->counting->setInsertPolicy(QComboBox::NoInsert);
    m_impl->counting->setToolTip(tr("New point markers are counted under this category"));
    m_impl->counting->addItem(tr("(none)"));

    m_impl->tally = new QTreeWidget(contents);
    m_impl->tally->setColumnCount(2);
    m_impl->tally->setHeaderHidden(true);
    m_impl->tally->setRootIsDecorated(false);
    m_impl->tally->setSelectionMode(QAbstractItemView::NoSelection);
    m_impl->tally->setFocusPolicy(Qt::NoFocus);
    m_impl->tally->header()->setStretchLastSection(false);
    m_impl->tally->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_impl->tally->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    // The strip must not grow at the list's expense as categories accumulate.
    m_impl->tally->setMaximumHeight(140);

    auto* countingRow = new QFormLayout;
    countingRow->addRow(tr("Counting:"), m_impl->counting);
    layout->insertLayout(0, countingRow);
    layout->insertWidget(1, m_impl->tally);

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

    connect(m_impl->counting, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        // By text, not index: an editable combo keeps its old index while a new name is typed.
        const bool none = text.trimmed().isEmpty() || text == m_impl->counting->itemText(0);
        const std::string category = none ? std::string{} : text.trimmed().toStdString();
        if (m_impl->model != nullptr) {
            m_impl->model->setCurrentCategory(category);
        }
    });

    m_impl->tally->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_impl->tally, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const QTreeWidgetItem* item = m_impl->tally->itemAt(at);
        if (item == nullptr) {
            return;
        }
        const QString label = item->text(0);
        const std::string category = label.toStdString();
        const bool configured = item->data(0, Qt::UserRole).toBool();
        // categoryTally guarantees exactly one Unclassified row and that it is never configured;
        // any real category spelled that way is folded into it, so the label identifies it. Rename
        // on it would bulk-classify every unclassified annotation on the scene.
        const bool unclassified =
            !configured && label.compare(QStringLiteral("Unclassified"), Qt::CaseInsensitive) == 0;

        QMenu menu(m_impl->tally);
        if (!unclassified) {
            QAction* renameAction = menu.addAction(tr("Rename..."));
            connect(renameAction, &QAction::triggered, this,
                    [this, category]() { emit renameCategoryRequested(category); });
        }
        if (configured) {
            const int count = item->text(1).toInt();
            const QString removeText = count == 0
                ? tr("Remove from list")
                : tr("Remove from list (%n annotation(s) keep it)", "", count);
            QAction* removeAction = menu.addAction(removeText);
            connect(removeAction, &QAction::triggered, this,
                    [this, category]() { emit removeCategoryRequested(category); });
        }
        if (!menu.isEmpty()) {
            menu.exec(m_impl->tally->viewport()->mapToGlobal(at));
        }
    });

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
        connect(m_impl->model, &app::AnnotationModel::annotationAdded, this, [this](const std::string& id) {
            rebuild();
            const core::Annotation* added = m_impl->model->find(id);
            // Markers only, matching the model's own stamping rule. Residual, accepted: undoing the
            // deletion of a marker re-enters through the same signal (insertAt), and the model cannot
            // yet tell creation from restoration, so that marker's category is re-offered.
            if (added != nullptr && added->type() == core::AnnotationType::Point
                && !added->properties().classification.empty()) {
                emit categoryUsed(added->properties().classification);
            }
        });
        connect(m_impl->model, &app::AnnotationModel::annotationRemoved, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::annotationChanged, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::cleared, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::modelReset, this, rebuildRows);
        // A bulk retag is not a reset (MainWindow would clear the undo stack), so it has its
        // own signal -- and it must refresh the rows and the tally all the same.
        connect(m_impl->model, &app::AnnotationModel::annotationsBulkChanged, this, rebuildRows);
        connect(m_impl->model, &app::AnnotationModel::selectionChanged, this,
                [this](const std::string& id) { applySelection(id); });
    }
    rebuild();
}

void AnnotationListPanel::setCategories(const std::vector<std::string>& categories)
{
    const QString current = m_impl->counting->currentText();
    const bool wasNone = current.trimmed().isEmpty() || current == m_impl->counting->itemText(0);
    m_impl->categories = categories;

    {
        // Load-bearing, not decoration: rebuilding the items fires currentTextChanged, whose
        // handler writes into the model's current category. Without the blocker, re-feeding the
        // list after a category joins it would silently restamp what the user is counting under.
        const QSignalBlocker blocker(m_impl->counting);
        m_impl->counting->clear();
        m_impl->counting->addItem(tr("(none)"));
        for (const std::string& category : categories) {
            m_impl->counting->addItem(QString::fromStdString(category));
        }
        if (wasNone) {
            m_impl->counting->setCurrentIndex(0);
        } else {
            // setCurrentText alone leaves currentIndex stale on an editable combo.
            const int at = m_impl->counting->findText(current);
            if (at >= 0) {
                m_impl->counting->setCurrentIndex(at);
            } else {
                m_impl->counting->setEditText(current);
            }
        }
    }
    rebuild();
}

void AnnotationListPanel::setCurrentCategory(const std::string& category)
{
    {
        // Blocked: the combo's handler would write the same value back, and on an editable combo
        // setEditText fires it with a half-updated index.
        const QSignalBlocker blocker(m_impl->counting);
        if (category.empty()) {
            m_impl->counting->setCurrentIndex(0);
        } else {
            const QString text = QString::fromStdString(category);
            const int at = m_impl->counting->findText(text, Qt::MatchFixedString);
            if (at >= 0) {
                m_impl->counting->setCurrentIndex(at);
            } else {
                m_impl->counting->setEditText(text);
            }
        }
    }
    if (m_impl->model != nullptr) {
        m_impl->model->setCurrentCategory(category);
    }
}

void AnnotationListPanel::setCountingEnabled(bool enabled)
{
    m_impl->counting->setEnabled(enabled);
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

    // Over the whole model, never the filtered rows: a clinical count that moved when someone
    // typed in the filter box would be a trap.
    m_impl->tally->clear();
    for (const CategoryTallyRow& row : categoryTally(annotations, m_impl->categories)) {
        auto* item = new QTreeWidgetItem(m_impl->tally);
        item->setText(0, QString::fromStdString(row.category));
        item->setText(1, QString::number(static_cast<qulonglong>(row.count)));
        item->setData(0, Qt::UserRole, row.configured);
    }
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
