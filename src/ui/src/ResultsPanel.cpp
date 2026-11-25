#include "slideio/viewer/ui/ResultsPanel.h"

#include "slideio/viewer/app/ResultsOverlayService.h"
#include "slideio/viewer/ui/ResultsPanelText.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace slideio::viewer::ui
{

namespace
{

constexpr int kRefreshDelayMs = 50;
/// An opacity spin is saved this long after its last step, or when its editing finishes.
constexpr int kOpacitySaveDelayMs = 500;
constexpr int kSwatchSize = 14;

QColor toQColor(core::Color c)
{
    return QColor(c.r, c.g, c.b);
}

core::Color fromQColor(const QColor& c)
{
    return core::Color{static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()),
                       static_cast<uint8_t>(c.blue()), 255};
}

QPixmap swatch(core::Color colour)
{
    QPixmap pixmap(kSwatchSize, kSwatchSize);
    pixmap.fill(toQColor(colour));
    return pixmap;
}

QString utf8(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString objectsText(int64_t n)
{
    return utf8(groupedCount(n)) + (n == 1 ? QStringLiteral(" object") : QStringLiteral(" objects"));
}

bool contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

struct ResultsPanel::Impl
{
    app::ResultsOverlayService* service = nullptr;
    QString workspacePath;
    QString slidePath;
    int scene = 0;

    QLabel* header = nullptr;
    QPushButton* reloadButton = nullptr;
    QPushButton* closeButton = nullptr;
    QScrollArea* scroll = nullptr;
    QLabel* details = nullptr;

    /// One per service layer, rebuilt with the rows.
    std::vector<QLabel*> statusLines;

    bool rebuildQueued = false;
    QTimer* refreshTimer = nullptr;
    QTimer* opacitySaveTimer = nullptr;
};

ResultsPanel::ResultsPanel(QWidget* parent)
    : QDockWidget(tr("Results"), parent)
    , m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("ResultsPanel"));
    auto* contents = new QWidget(this);
    auto* layout = new QVBoxLayout(contents);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* top = new QHBoxLayout();
    m_impl->header = new QLabel(contents);
    m_impl->header->setWordWrap(true);
    m_impl->header->setTextFormat(Qt::PlainText);
    m_impl->header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(m_impl->header, 1);
    m_impl->reloadButton = new QPushButton(tr("Reload"), contents);
    m_impl->reloadButton->setToolTip(tr("Re-read the workspace's layers, classes and counts"));
    m_impl->closeButton = new QPushButton(tr("Close"), contents);
    m_impl->closeButton->setToolTip(tr("Detach the processing results from this scene"));
    top->addWidget(m_impl->reloadButton, 0, Qt::AlignTop);
    top->addWidget(m_impl->closeButton, 0, Qt::AlignTop);
    layout->addLayout(top);

    m_impl->scroll = new QScrollArea(contents);
    m_impl->scroll->setWidgetResizable(true);
    m_impl->scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_impl->scroll, 1);

    m_impl->details = new QLabel(contents);
    m_impl->details->setWordWrap(true);
    m_impl->details->setTextFormat(Qt::PlainText);
    m_impl->details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_impl->details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_impl->details);

    setWidget(contents);

    m_impl->refreshTimer = new QTimer(this);
    m_impl->refreshTimer->setSingleShot(true);
    m_impl->refreshTimer->setInterval(kRefreshDelayMs);
    connect(m_impl->refreshTimer, &QTimer::timeout, this, [this]() { refresh(); });

    m_impl->opacitySaveTimer = new QTimer(this);
    m_impl->opacitySaveTimer->setSingleShot(true);
    m_impl->opacitySaveTimer->setInterval(kOpacitySaveDelayMs);
    connect(m_impl->opacitySaveTimer, &QTimer::timeout, this, &ResultsPanel::layerViewChanged);

    connect(m_impl->reloadButton, &QPushButton::clicked, this, &ResultsPanel::reloadRequested);
    connect(m_impl->closeButton, &QPushButton::clicked, this, &ResultsPanel::closeRequested);

    rebuild();
}

ResultsPanel::~ResultsPanel() = default;

void ResultsPanel::setService(app::ResultsOverlayService* service)
{
    m_impl->service = service;
    scheduleRebuild();
}

void ResultsPanel::setAttachment(const QString& workspacePath, const QString& slidePath, int scene)
{
    m_impl->workspacePath = workspacePath;
    m_impl->slidePath = slidePath;
    m_impl->scene = scene;
    scheduleRebuild();
}

void ResultsPanel::scheduleRebuild()
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

void ResultsPanel::scheduleRefresh()
{
    if (!m_impl->refreshTimer->isActive()) {
        m_impl->refreshTimer->start();
    }
}

void ResultsPanel::flushPendingSave()
{
    if (m_impl->opacitySaveTimer->isActive()) {
        m_impl->opacitySaveTimer->stop();
        emit layerViewChanged();
    }
}

void ResultsPanel::rebuild()
{
    app::ResultsOverlayService* service = m_impl->service;
    const bool attached = service != nullptr && service->attached();
    m_impl->statusLines.clear();
    m_impl->reloadButton->setEnabled(attached);
    m_impl->closeButton->setEnabled(attached);

    auto* body = new QWidget();
    auto* rows = new QVBoxLayout(body);
    rows->setContentsMargins(0, 0, 0, 0);

    if (!attached) {
        m_impl->header->setText(tr("No processing results attached."));
        auto* hint = new QLabel(
            tr("File > Open Processing Results\xE2\x80\xA6 shows a slideio-processor workspace's results over "
               "the open scene."),
            body);
        hint->setWordWrap(true);
        rows->addWidget(hint);
        rows->addStretch(1);
        m_impl->scroll->setWidget(body);   // deletes the previous body
        refresh();
        return;
    }

    m_impl->header->setText(tr("Results: %1\nslide: %2, scene %3")
                                .arg(m_impl->workspacePath, m_impl->slidePath)
                                .arg(m_impl->scene));

    const auto& layers = service->layers();
    if (layers.empty()) {
        rows->addWidget(new QLabel(tr("no results"), body));
    }

    const QString legendTip =
        tr("An object with two classes counts in both rows, so the rows can add up to more than the "
           "layer's total.");

    for (std::size_t li = 0; li < layers.size(); ++li) {
        const int layer = static_cast<int>(li);
        const core::ResultLayerInfo& info = layers[li];
        const core::ResultsLayerView& view = service->layerView(layer);

        auto* row = new QHBoxLayout();
        auto* visible = new QCheckBox(utf8(info.name), body);
        visible->setChecked(view.visible);
        row->addWidget(visible, 1);

        auto* colour = new QToolButton(body);
        colour->setIcon(swatch(service->layerColour(layer)));
        colour->setToolTip(tr("Colour of this layer's objects that are not coloured by class"));
        row->addWidget(colour);

        auto* fill = new QCheckBox(tr("fill"), body);
        fill->setChecked(view.fill);
        row->addWidget(fill);

        auto* opacity = new QSpinBox(body);
        opacity->setRange(0, 100);
        opacity->setSuffix(QStringLiteral(" %"));
        opacity->setValue(static_cast<int>(std::lround(view.fillOpacity * 100.0)));
        opacity->setEnabled(view.fill);
        opacity->setToolTip(tr("Fill opacity"));
        row->addWidget(opacity);

        auto* count = new QLabel(objectsText(info.objectCount), body);
        row->addWidget(count);

        const std::string badge = stageStateBadge(info.stageState);
        if (!badge.empty()) {
            auto* badgeLabel = new QLabel(utf8(badge), body);
            badgeLabel->setStyleSheet(badge == "failed" ? QStringLiteral("color: #c0392b; font-weight: bold;")
                                                        : QStringLiteral("color: #b9770e; font-weight: bold;"));
            badgeLabel->setToolTip(badge == "failed"
                                       ? tr("The stage that produced this layer failed; its objects are what "
                                            "the store holds")
                                       : tr("The stage that produced this layer is stale; its objects may be "
                                            "out of date"));
            row->addWidget(badgeLabel);
        }
        rows->addLayout(row);

        auto* legend = new QGridLayout();
        legend->setContentsMargins(24, 0, 0, 0);
        auto* byClass = new QCheckBox(tr("colour by class"), body);
        byClass->setChecked(view.colourByClass);
        legend->addWidget(byClass, 0, 0, 1, 4);

        const std::vector<core::Color> classColours = service->classColours(layer);
        int gridRow = 1;
        auto addLegendRow = [&](const std::string& className, const QString& shownName, core::Color rowColour,
                                const QString& rowCount, bool colourEditable) {
            auto* shown = new QCheckBox(body);
            shown->setChecked(!contains(view.hiddenClasses, className));
            shown->setToolTip(legendTip);
            legend->addWidget(shown, gridRow, 0);
            if (colourEditable) {
                auto* button = new QToolButton(body);
                button->setIcon(swatch(rowColour));
                button->setToolTip(tr("Change this class's colour on every slide"));
                legend->addWidget(button, gridRow, 1);
                connect(button, &QToolButton::clicked, this, [this, className, rowColour, shownName]() {
                    const QColor chosen =
                        QColorDialog::getColor(toQColor(rowColour), this, tr("Colour of %1").arg(shownName));
                    if (!chosen.isValid()) {
                        return;
                    }
                    emit classColourChosen(className, fromQColor(chosen));
                    scheduleRebuild();
                });
            } else {
                auto* plain = new QLabel(body);
                plain->setPixmap(swatch(rowColour));
                plain->setToolTip(tr("Unclassified objects are drawn in the layer's colour"));
                legend->addWidget(plain, gridRow, 1, Qt::AlignCenter);
            }
            auto* name = new QLabel(shownName, body);
            name->setToolTip(legendTip);
            legend->addWidget(name, gridRow, 2);
            auto* number = new QLabel(rowCount, body);
            number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            number->setToolTip(legendTip);
            legend->addWidget(number, gridRow, 3);
            connect(shown, &QCheckBox::toggled, this, [this, layer, className](bool on) {
                if (m_impl->service == nullptr || !m_impl->service->attached()) {
                    return;
                }
                core::ResultsLayerView next = m_impl->service->layerView(layer);
                auto& hidden = next.hiddenClasses;
                hidden.erase(std::remove(hidden.begin(), hidden.end(), className), hidden.end());
                if (!on) {
                    hidden.push_back(className);
                }
                m_impl->service->setLayerView(layer, next);
                emit layerViewChanged();
                scheduleRefresh();   // hiding a class can clear the selection
            });
            ++gridRow;
        };

        for (std::size_t ci = 0; ci < info.classes.size(); ++ci) {
            const core::ResultClassInfo& cls = info.classes[ci];
            const core::Color c = ci < classColours.size() ? classColours[ci] : service->layerColour(layer);
            addLegendRow(cls.name, utf8(cls.name), c, utf8(groupedCount(cls.objectCount)), true);
        }
        addLegendRow(app::ResultsOverlayService::kUnclassifiedName, tr("Unclassified"), service->layerColour(layer),
                     utf8(groupedCount(info.unclassifiedCount)), false);
        legend->setColumnStretch(2, 1);
        rows->addLayout(legend);

        auto* status = new QLabel(body);
        status->setContentsMargins(24, 0, 0, 6);
        status->setWordWrap(true);
        status->hide();
        rows->addWidget(status);
        m_impl->statusLines.push_back(status);

        // Every control writes the layer's whole view back through the service.
        auto edit = [this, layer](auto change) {
            if (m_impl->service == nullptr || !m_impl->service->attached()) {
                return;
            }
            core::ResultsLayerView next = m_impl->service->layerView(layer);
            change(next);
            m_impl->service->setLayerView(layer, next);
            emit layerViewChanged();
            scheduleRefresh();
        };
        connect(visible, &QCheckBox::toggled, this, [edit](bool on) {
            edit([on](core::ResultsLayerView& v) { v.visible = on; });
        });
        connect(fill, &QCheckBox::toggled, this, [edit, opacity](bool on) {
            opacity->setEnabled(on);
            edit([on](core::ResultsLayerView& v) { v.fill = on; });
        });
        // Every step repaints; the settings are written once the spinning stops.
        connect(opacity, qOverload<int>(&QSpinBox::valueChanged), this, [this, layer](int percent) {
            if (m_impl->service == nullptr || !m_impl->service->attached()) {
                return;
            }
            core::ResultsLayerView next = m_impl->service->layerView(layer);
            next.fillOpacity = percent / 100.0;
            m_impl->service->setLayerView(layer, next);
            emit layerViewPreviewed();
            m_impl->opacitySaveTimer->start();
        });
        connect(opacity, &QSpinBox::editingFinished, this, [this]() { flushPendingSave(); });
        connect(byClass, &QCheckBox::toggled, this, [edit](bool on) {
            edit([on](core::ResultsLayerView& v) { v.colourByClass = on; });
        });
        connect(colour, &QToolButton::clicked, this, [this, edit, layer, name = utf8(info.name)]() {
            if (m_impl->service == nullptr || !m_impl->service->attached()) {
                return;
            }
            const QColor chosen = QColorDialog::getColor(toQColor(m_impl->service->layerColour(layer)), this,
                                                         tr("Colour of %1").arg(name));
            if (!chosen.isValid()) {
                return;
            }
            edit([c = fromQColor(chosen)](core::ResultsLayerView& v) { v.colour = c; });
            scheduleRebuild();
        });
    }
    rows->addStretch(1);
    m_impl->scroll->setWidget(body);   // deletes the previous body
    refresh();
}

void ResultsPanel::refresh()
{
    app::ResultsOverlayService* service = m_impl->service;
    const bool attached = service != nullptr && service->attached();
    if (!attached) {
        // The service detached on its own (an error) since the last rebuild.
        if (!m_impl->statusLines.empty()) {
            scheduleRebuild();
        }
        m_impl->details->clear();
        m_impl->details->hide();
        return;
    }
    if (m_impl->statusLines.size() != service->layers().size()) {
        scheduleRebuild();
        return;
    }

    using Mode = app::ResultsOverlayService::LayerMode;
    for (std::size_t li = 0; li < m_impl->statusLines.size(); ++li) {
        const int layer = static_cast<int>(li);
        QLabel* status = m_impl->statusLines[li];
        const auto& state = service->layerState(layer);
        QString text;
        if (service->layerView(layer).visible) {
            switch (state.mode) {
            case Mode::Hidden:
                // A visible layer is Hidden until its first query answer.
                text = tr("loading\xE2\x80\xA6");
                break;
            case Mode::Outlines:
                break;
            case Mode::Overview:
                text = utf8(inViewText(state.inView));
                break;
            case Mode::BuildingOverview:
                // No scan queued: the layer was within budget when read, so
                // only a Reload builds its overview.
                text = utf8(state.scanQueued ? buildingText(state.scanFraction) : tooManyToOutlineText());
                break;
            }
        }
        status->setText(text);
        status->setVisible(!text.isEmpty());
    }

    const auto& selectedId = service->selectedId();
    if (!selectedId) {
        m_impl->details->clear();
        m_impl->details->hide();
        return;
    }
    const auto& details = service->selectedDetails();
    QString text;
    if (!details) {
        text = tr("Selected: loading details\xE2\x80\xA6");
    } else {
        QStringList classes;
        for (const std::string& c : details->classes) {
            classes << utf8(c);
        }
        const QString classText = classes.isEmpty() ? tr("Unclassified") : classes.join(QStringLiteral(", "));
        text = tr("Selected: %1 \xC2\xB7 %2 \xC2\xB7 stage \"%3\"")
                   .arg(utf8(details->layer), classText, utf8(details->stage));
        for (const core::ResultMeasurement& m : details->measurements) {
            text += QStringLiteral("\n  ") + utf8(measurementLine(m));
        }
    }
    m_impl->details->setText(text);
    m_impl->details->show();
}

} // namespace slideio::viewer::ui
