#include "slideio/viewer/ui/LayersPanel.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/ui/LayersPanelPolicy.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace slideio::viewer::ui
{

namespace
{

constexpr int VisibleColumn = static_cast<int>(LayerColumn::Visible);
constexpr int LockedColumn = static_cast<int>(LayerColumn::Locked);
constexpr int NameColumn = static_cast<int>(LayerColumn::Name);
constexpr int CountColumn = static_cast<int>(LayerColumn::Count);
constexpr int OpacityColumn = static_cast<int>(LayerColumn::Opacity);

constexpr int kLayerIdRole = Qt::UserRole + 1;

/// "Layer <n>" with the smallest n no layer is already called.
std::string freshLayerName(const std::vector<core::AnnotationLayer>& layers)
{
    std::set<std::string> names;
    for (const core::AnnotationLayer& layer : layers) {
        names.insert(layer.name);
    }
    for (int n = 1;; ++n) {
        const std::string name = "Layer " + std::to_string(n);
        if (names.count(name) == 0) {
            return name;
        }
    }
}

/// An id no layer has: "layer-<n>".
std::string freshLayerId(const std::vector<core::AnnotationLayer>& layers)
{
    for (int n = 1;; ++n) {
        const std::string id = "layer-" + std::to_string(n);
        if (core::findLayer(layers, id) == nullptr) {
            return id;
        }
    }
}

} // namespace

struct LayersPanel::Impl
{
    app::AnnotationModel* model = nullptr;
    bool editingAllowed = false;
    QTreeWidget* tree = nullptr;
    QPushButton* newButton = nullptr;
    QPushButton* deleteButton = nullptr;
    QPushButton* mergeButton = nullptr;
    QPushButton* upButton = nullptr;
    QPushButton* downButton = nullptr;
    bool rebuilding = false;
    bool rebuildQueued = false;
};

LayersPanel::LayersPanel(QWidget* parent)
    : QDockWidget(tr("Layers"), parent)
    , m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("LayersPanel"));
    auto* contents = new QWidget(this);
    auto* layout = new QVBoxLayout(contents);

    m_impl->tree = new QTreeWidget(contents);
    m_impl->tree->setColumnCount(5);
    m_impl->tree->setHeaderLabels({tr("Show"), tr("Lock"), tr("Layer"), tr("Count"), tr("Opacity")});
    m_impl->tree->setRootIsDecorated(false);
    m_impl->tree->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    m_impl->tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_impl->tree);

    auto* buttons = new QHBoxLayout;
    m_impl->newButton = new QPushButton(tr("New"), contents);
    m_impl->deleteButton = new QPushButton(tr("Delete"), contents);
    m_impl->mergeButton = new QPushButton(tr("Merge into\xE2\x80\xA6"), contents);
    m_impl->upButton = new QPushButton(tr("Up"), contents);
    m_impl->downButton = new QPushButton(tr("Down"), contents);
    for (QPushButton* button : {m_impl->newButton, m_impl->deleteButton, m_impl->mergeButton, m_impl->upButton,
                                m_impl->downButton}) {
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    setWidget(contents);

    // The row the user picks is the active layer: where the next shape goes.
    // A press on the eye or padlock makes the row current too, but only
    // selects it for the buttons -- see columnActivatesLayer.
    connect(m_impl->tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        if (m_impl->rebuilding || m_impl->model == nullptr || current == nullptr) {
            return;
        }
        const int column = m_impl->tree->currentColumn();
        if (column < 0 || columnActivatesLayer(static_cast<LayerColumn>(column))) {
            m_impl->model->setActiveLayer(current->data(NameColumn, kLayerIdRole).toString().toStdString());
        }
        updateButtons();
    });
    // The current row changes no item when its own name is clicked after its
    // eye was: that press still has to make it active.
    connect(m_impl->tree, &QTreeWidget::itemPressed, this, [this](QTreeWidgetItem* item, int column) {
        if (m_impl->rebuilding || m_impl->model == nullptr || item == nullptr
            || !columnActivatesLayer(static_cast<LayerColumn>(column))) {
            return;
        }
        m_impl->model->setActiveLayer(item->data(NameColumn, kLayerIdRole).toString().toStdString());
        updateButtons();
    });

    // Check boxes and an edited name: one structure edit through setLayers.
    connect(m_impl->tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_impl->rebuilding || m_impl->model == nullptr) {
            return;
        }
        // On a read-only scene only the eye works: Qt makes a whole row
        // checkable or not, so a lock change there is put straight back.
        if (!m_impl->editingAllowed && column != VisibleColumn) {
            scheduleRebuild();
            return;
        }
        const std::string id = item->data(NameColumn, kLayerIdRole).toString().toStdString();
        std::vector<core::AnnotationLayer> layers = m_impl->model->layers();
        for (core::AnnotationLayer& layer : layers) {
            if (layer.id != id) {
                continue;
            }
            if (column == VisibleColumn) {
                layer.visible = item->checkState(VisibleColumn) == Qt::Checked;
            } else if (column == LockedColumn) {
                layer.locked = item->checkState(LockedColumn) == Qt::Checked;
            } else if (column == NameColumn) {
                const QString name = item->text(NameColumn).trimmed();
                if (!name.isEmpty()) {
                    layer.name = name.toStdString();
                }
            }
        }
        // A refused list (an empty name) is put back by the rebuild.
        if (!m_impl->model->setLayers(std::move(layers))) {
            scheduleRebuild();
        }
    });

    connect(m_impl->newButton, &QPushButton::clicked, this, [this]() {
        if (m_impl->model == nullptr) {
            return;
        }
        std::vector<core::AnnotationLayer> layers = m_impl->model->layers();
        const core::AnnotationLayer layer{freshLayerId(layers), freshLayerName(layers)};
        layers.push_back(layer);  // new layers go on top
        if (m_impl->model->setLayers(std::move(layers))) {
            m_impl->model->setActiveLayer(layer.id);
        }
    });
    connect(m_impl->deleteButton, &QPushButton::clicked, this, [this]() {
        const std::string id = selectedLayerId();
        if (m_impl->model == nullptr
            || !canDeleteLayer(m_impl->model->layers(), m_impl->model->annotations(), id)) {
            return;
        }
        std::vector<core::AnnotationLayer> layers = m_impl->model->layers();
        layers.erase(std::remove_if(layers.begin(), layers.end(),
                                    [&](const core::AnnotationLayer& layer) { return layer.id == id; }),
                     layers.end());
        m_impl->model->setLayers(std::move(layers));
    });
    connect(m_impl->mergeButton, &QPushButton::clicked, this, [this]() {
        const std::string source = selectedLayerId();
        if (m_impl->model == nullptr || source.empty()) {
            return;
        }
        QMenu menu(this);
        for (const std::string& target : mergeTargets(m_impl->model->layers(), source)) {
            const core::AnnotationLayer* layer = core::findLayer(m_impl->model->layers(), target);
            QAction* action = menu.addAction(QString::fromStdString(layer->name));
            connect(action, &QAction::triggered, this, [this, source, target]() { emit mergeRequested(source, target); });
        }
        menu.exec(m_impl->mergeButton->mapToGlobal(QPoint(0, m_impl->mergeButton->height())));
    });
    for (const auto& [button, delta] : {std::pair{m_impl->upButton, +1}, std::pair{m_impl->downButton, -1}}) {
        connect(button, &QPushButton::clicked, this, [this, delta = delta]() {
            if (m_impl->model == nullptr) {
                return;
            }
            m_impl->model->setLayers(withLayerMoved(m_impl->model->layers(), selectedLayerId(), delta));
        });
    }

    updateButtons();
}

LayersPanel::~LayersPanel() = default;

void LayersPanel::setAnnotationModel(app::AnnotationModel* model)
{
    m_impl->model = model;
    // DEFERRED, never immediate: most of these signals come back from this
    // panel's own tree -- a row clicked, a box ticked, a name edited -- and an
    // immediate rebuild clears the tree while QTreeWidget is still inside that
    // item's signal, so Qt goes on to use the item it just deleted. That
    // crashed the viewer in Qt6Widgets during the 2026-10-24 smoke run. One
    // queued rebuild also coalesces a burst of signals into one.
    const auto rebuildNow = [this]() { scheduleRebuild(); };
    connect(model, &app::AnnotationModel::layersChanged, this, rebuildNow);
    connect(model, &app::AnnotationModel::activeLayerChanged, this, rebuildNow);
    connect(model, &app::AnnotationModel::annotationAdded, this, rebuildNow);
    connect(model, &app::AnnotationModel::annotationRemoved, this, rebuildNow);
    connect(model, &app::AnnotationModel::annotationsBulkChanged, this, rebuildNow);
    connect(model, &app::AnnotationModel::modelReset, this, rebuildNow);
    connect(model, &app::AnnotationModel::cleared, this, rebuildNow);
    rebuild();
}

void LayersPanel::scheduleRebuild()
{
    if (m_impl->rebuildQueued) {
        return;
    }
    m_impl->rebuildQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_impl->rebuildQueued = false;
            rebuild();
        },
        Qt::QueuedConnection);
}

void LayersPanel::setEditingAllowed(bool allowed)
{
    m_impl->editingAllowed = allowed;
    scheduleRebuild();
}

void LayersPanel::rebuild()
{
    if (m_impl->model == nullptr) {
        return;
    }
    m_impl->rebuilding = true;
    const QSignalBlocker blocker(m_impl->tree);
    m_impl->tree->clear();
    QTreeWidgetItem* activeItem = nullptr;
    for (const LayerRow& row :
         layerRows(m_impl->model->layers(), m_impl->model->annotations(), m_impl->model->activeLayerId())) {
        auto* item = new QTreeWidgetItem(m_impl->tree);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        if (m_impl->editingAllowed) {
            flags |= Qt::ItemIsEditable;
        }
        item->setFlags(flags);
        item->setCheckState(VisibleColumn, row.visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(LockedColumn, row.locked ? Qt::Checked : Qt::Unchecked);
        item->setText(NameColumn, QString::fromStdString(row.name));
        item->setData(NameColumn, kLayerIdRole, QString::fromStdString(row.id));
        item->setText(CountColumn, QString::number(row.count));
        if (row.active) {
            QFont font = item->font(NameColumn);
            font.setBold(true);
            item->setFont(NameColumn, font);
            activeItem = item;
        }

        auto* slider = new QSlider(Qt::Horizontal, m_impl->tree);
        slider->setRange(0, 100);
        slider->setValue(static_cast<int>(row.opacity * 100.0 + 0.5));
        slider->setEnabled(m_impl->editingAllowed);
        const std::string id = row.id;
        // Applied once per gesture, not on every step of a drag -- each
        // setLayers repaints and marks the scene dirty: on release after a
        // drag, and at once for a click on the track, the keyboard or the
        // wheel, which change the value without any release.
        const auto apply = [this, slider, id]() {
            std::vector<core::AnnotationLayer> layers = m_impl->model->layers();
            for (core::AnnotationLayer& layer : layers) {
                if (layer.id == id) {
                    layer.opacity = slider->value() / 100.0;
                }
            }
            m_impl->model->setLayers(std::move(layers));
        };
        connect(slider, &QSlider::sliderReleased, this, apply);
        connect(slider, &QSlider::valueChanged, this, [slider, apply](int) {
            if (!slider->isSliderDown()) {
                apply();
            }
        });
        m_impl->tree->setItemWidget(item, OpacityColumn, slider);
    }
    if (activeItem != nullptr) {
        m_impl->tree->setCurrentItem(activeItem);
    }
    m_impl->rebuilding = false;
    updateButtons();
}

std::string LayersPanel::selectedLayerId() const
{
    const QTreeWidgetItem* item = m_impl->tree->currentItem();
    return item == nullptr ? std::string() : item->data(NameColumn, kLayerIdRole).toString().toStdString();
}

void LayersPanel::updateButtons()
{
    const bool ready = m_impl->model != nullptr && m_impl->editingAllowed;
    const std::string id = selectedLayerId();
    const auto& layers = ready ? m_impl->model->layers() : std::vector<core::AnnotationLayer>{};
    m_impl->newButton->setEnabled(ready);
    m_impl->deleteButton->setEnabled(ready && canDeleteLayer(layers, m_impl->model->annotations(), id));
    m_impl->mergeButton->setEnabled(ready && !id.empty() && layers.size() > 1);
    m_impl->upButton->setEnabled(ready && canMoveLayerUp(layers, id));
    m_impl->downButton->setEnabled(ready && canMoveLayerDown(layers, id));
}

} // namespace slideio::viewer::ui
