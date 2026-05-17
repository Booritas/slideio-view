#include "slideio/viewer/ui/ChannelMixerPanel.h"
#include "slideio/viewer/ui/ChannelHistogramView.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleValidator>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <optional>

namespace slideio::viewer::ui
{

struct ChannelMixerPanel::Impl
{
    struct ChannelRow
    {
        QToolButton*         disclosure = nullptr;
        QCheckBox*           checkBox = nullptr;
        QLabel*              nameLabel = nullptr;
        QPushButton*         colorButton = nullptr;
        QSlider*             intensitySlider = nullptr;
        QWidget*             expandedContainer = nullptr;  // null until first expanded
        bool                 expanded = false;
        ChannelHistogramView* histogramView = nullptr;
        QLineEdit*            minEdit = nullptr;
        QLineEdit*            maxEdit = nullptr;
        bool                  logScale = true;
    };

    ChannelMixerPanel* owner = nullptr;
    std::vector<core::ChannelInfo> channels;
    std::vector<ChannelRow> rows;

    QScrollArea* scrollArea = nullptr;
    QWidget* contentWidget = nullptr;
    QVBoxLayout* contentLayout = nullptr;

    QCheckBox* masterCheckBox = nullptr;
    QLabel*    visibleCountLabel = nullptr;
    std::optional<std::vector<bool>> preToggleVisibility;

    void buildUi()
    {
        scrollArea = new QScrollArea(owner);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);

        contentWidget = new QWidget(scrollArea);
        contentWidget->setStyleSheet("background-color: #2D2D2D;");
        contentLayout = new QVBoxLayout(contentWidget);
        contentLayout->setContentsMargins(4, 4, 4, 4);
        contentLayout->setSpacing(4);
        contentLayout->addStretch();

        scrollArea->setWidget(contentWidget);
        owner->setWidget(scrollArea);
    }

    void clearRows()
    {
        rows.clear();

        // Replace the content widget entirely — Qt destroys all child widgets
        delete contentWidget;
        contentWidget = new QWidget(scrollArea);
        contentWidget->setStyleSheet("background-color: #2D2D2D;");
        contentLayout = new QVBoxLayout(contentWidget);
        contentLayout->setContentsMargins(4, 4, 4, 4);
        contentLayout->setSpacing(4);
        scrollArea->setWidget(contentWidget);
    }

    void rebuildRows()
    {
        std::vector<bool> previousExpanded(rows.size());
        std::vector<bool> previousLogScale(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            previousExpanded[i] = rows[i].expanded;
            previousLogScale[i] = rows[i].logScale;
        }

        clearRows();
        rows.reserve(channels.size());

        // --- Master toggle header ---
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 4);
        headerLayout->setSpacing(4);

        masterCheckBox = new QCheckBox(contentWidget);
        masterCheckBox->setTristate(true);
        masterCheckBox->setStyleSheet("QCheckBox { color: #CCC; font-weight: bold; }");
        masterCheckBox->setText("All channels");
        QObject::connect(masterCheckBox, &QCheckBox::clicked, owner, [this](bool /*checked*/) {
            onMasterClicked();
        });
        headerLayout->addWidget(masterCheckBox);
        headerLayout->addStretch();

        visibleCountLabel = new QLabel(contentWidget);
        visibleCountLabel->setStyleSheet("color:#888; font-size:11px;");
        headerLayout->addWidget(visibleCountLabel);

        contentLayout->addLayout(headerLayout);

        // Separator
        auto* sep = new QFrame(contentWidget);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color:#444;");
        contentLayout->addWidget(sep);

        refreshMasterState();

        for (size_t i = 0; i < channels.size(); ++i) {
            const auto& ch = channels[i];
            ChannelRow row;

            if (i < previousLogScale.size()) {
                row.logScale = previousLogScale[i];
            }

            // Disclosure triangle button
            row.disclosure = new QToolButton(contentWidget);
            row.disclosure->setArrowType(Qt::RightArrow);
            row.disclosure->setAutoRaise(true);
            row.disclosure->setFixedSize(12, 12);
            row.disclosure->setStyleSheet("QToolButton { border: none; }");
            QObject::connect(row.disclosure, &QToolButton::clicked, owner, [this, i]() {
                toggleRowExpanded(i);
            });

            auto* hLayout = new QHBoxLayout();
            hLayout->setContentsMargins(0, 0, 0, 0);
            hLayout->setSpacing(4);

            hLayout->addWidget(row.disclosure);

            // Visibility checkbox
            row.checkBox = new QCheckBox(contentWidget);
            row.checkBox->setChecked(ch.visible);
            QObject::connect(row.checkBox, &QCheckBox::toggled, owner, [this, i](bool checked) {
                if (i < channels.size()) {
                    channels[i].visible = checked;
                    refreshMasterState();
                    emit owner->channelSettingsChanged(channels);
                }
            });
            hLayout->addWidget(row.checkBox);

            // Channel name label
            QString name = ch.name.empty()
                ? QString("Channel %1").arg(static_cast<int>(i) + 1)
                : QString::fromStdString(ch.name);
            row.nameLabel = new QLabel(name, contentWidget);
            row.nameLabel->setStyleSheet("color: #CCCCCC;");
            hLayout->addWidget(row.nameLabel, 1);

            // Color button
            row.colorButton = new QPushButton(contentWidget);
            applyColorButtonStyle(row.colorButton, ch.colorR, ch.colorG, ch.colorB);
            QObject::connect(row.colorButton, &QPushButton::clicked, owner, [this, i]() {
                onColorButtonClicked(i);
            });
            hLayout->addWidget(row.colorButton);

            // Intensity slider (0–400, maps to 0.0–4.0; 1.0× at the 25% mark)
            row.intensitySlider = new QSlider(Qt::Horizontal, contentWidget);
            row.intensitySlider->setRange(0, 400);
            row.intensitySlider->setValue(static_cast<int>(ch.intensity * 100.0f));
            row.intensitySlider->setFixedWidth(80);
            row.intensitySlider->setStyleSheet(
                "QSlider::groove:horizontal { background: #555; height: 4px; border-radius: 2px; }"
                "QSlider::handle:horizontal { background: #CCC; width: 12px; margin: -4px 0; border-radius: 6px; }");
            QObject::connect(row.intensitySlider, &QSlider::valueChanged, owner, [this, i](int value) {
                if (i < channels.size()) {
                    channels[i].intensity = static_cast<float>(value) / 100.0f;
                    emit owner->channelSettingsChanged(channels);
                }
            });
            hLayout->addWidget(row.intensitySlider);

            contentLayout->addLayout(hLayout);

            if (i < previousExpanded.size() && previousExpanded[i]) {
                row.expanded = true;
                row.disclosure->setArrowType(Qt::DownArrow);
                row.expandedContainer = buildExpandedContainer(i, row);
                contentLayout->addWidget(row.expandedContainer);
            }

            rows.push_back(row);
        }

        contentLayout->addStretch();
    }

    void toggleRowExpanded(size_t index)
    {
        if (index >= rows.size()) return;
        rows[index].expanded = !rows[index].expanded;
        rebuildRows();
    }

    QWidget* buildExpandedContainer(size_t index, ChannelRow& row)
    {
        Q_ASSERT(index < channels.size());
        const auto& ch = channels[index];

        auto* frame = new QFrame(contentWidget);
        frame->setStyleSheet(
            "QFrame { background-color: #252525; margin-left: 24px; padding: 6px; }");
        auto* vLayout = new QVBoxLayout(frame);
        vLayout->setContentsMargins(6, 6, 6, 6);
        vLayout->setSpacing(4);

        // --- Histogram view ---
        row.histogramView = new ChannelHistogramView(frame);
        row.histogramView->setHistogram(ch.histogram);
        row.histogramView->setChannelColor(ch.colorR, ch.colorG, ch.colorB);
        row.histogramView->setDisplayRange(ch.displayRange.displayMin,
                                           ch.displayRange.displayMax);
        row.histogramView->setLogScale(row.logScale);
        vLayout->addWidget(row.histogramView);

        // --- Min/Max textbox row ---
        auto* editRow = new QHBoxLayout();
        editRow->setSpacing(4);

        auto* minLabel = new QLabel("Min", frame);
        minLabel->setStyleSheet("color: #CCC; font-size: 11px;");
        editRow->addWidget(minLabel);

        row.minEdit = new QLineEdit(frame);
        row.minEdit->setValidator(new QDoubleValidator(row.minEdit));
        row.minEdit->setText(QString::number(ch.displayRange.displayMin));
        row.minEdit->setFixedWidth(64);
        row.minEdit->setStyleSheet(
            "QLineEdit { background:#333; color:#CCC; border:1px solid #555; padding:1px 4px; }");
        editRow->addWidget(row.minEdit);

        editRow->addStretch();

        auto* maxLabel = new QLabel("Max", frame);
        maxLabel->setStyleSheet("color: #CCC; font-size: 11px;");
        editRow->addWidget(maxLabel);

        row.maxEdit = new QLineEdit(frame);
        row.maxEdit->setValidator(new QDoubleValidator(row.maxEdit));
        row.maxEdit->setText(QString::number(ch.displayRange.displayMax));
        row.maxEdit->setFixedWidth(64);
        row.maxEdit->setStyleSheet(
            "QLineEdit { background:#333; color:#CCC; border:1px solid #555; padding:1px 4px; }");
        editRow->addWidget(row.maxEdit);

        auto* autoBtn = new QPushButton("Auto", frame);
        autoBtn->setEnabled(ch.autoDisplayRange.autoDetected);
        autoBtn->setStyleSheet(
            "QPushButton { background:#444; color:#CCC; border:1px solid #666; padding:2px 8px; }"
            "QPushButton:disabled { color:#666; }");
        editRow->addWidget(autoBtn);
        QObject::connect(autoBtn, &QPushButton::clicked, owner, [this, index]() {
            applyAutoRange(index);
        });

        auto* resetBtn = new QPushButton("Reset", frame);
        resetBtn->setStyleSheet(
            "QPushButton { background:#444; color:#CCC; border:1px solid #666; padding:2px 8px; }");
        editRow->addWidget(resetBtn);
        QObject::connect(resetBtn, &QPushButton::clicked, owner, [this, index]() {
            applyResetRange(index);
        });

        auto* logBtn = new QToolButton(frame);
        logBtn->setText(row.logScale ? "Log" : "Lin");
        logBtn->setCheckable(true);
        logBtn->setChecked(row.logScale);
        logBtn->setStyleSheet(
            "QToolButton { background:#444; color:#CCC; border:1px solid #666; padding:2px 8px; }"
            "QToolButton:checked { background:#555; }");
        editRow->addWidget(logBtn);
        QObject::connect(logBtn, &QToolButton::toggled, owner, [this, index, logBtn](bool checked) {
            if (index >= rows.size()) return;
            rows[index].logScale = checked;
            logBtn->setText(checked ? "Log" : "Lin");
            if (rows[index].histogramView) {
                rows[index].histogramView->setLogScale(checked);
            }
        });

        vLayout->addLayout(editRow);

        // --- Wire histogram drag -> textboxes (live) ---
        QObject::connect(row.histogramView, &ChannelHistogramView::displayRangeChanged,
                         owner, [this, index](double minV, double maxV) {
            if (index >= rows.size()) return;
            if (rows[index].minEdit) rows[index].minEdit->setText(QString::number(minV));
            if (rows[index].maxEdit) rows[index].maxEdit->setText(QString::number(maxV));
        });

        // --- Wire histogram release -> commit (emits channelSettingsChanged) ---
        QObject::connect(row.histogramView, &ChannelHistogramView::displayRangeCommitted,
                         owner, [this, index](double minV, double maxV) {
            commitDisplayRange(index, minV, maxV);
        });

        // --- Wire textbox edits ---
        QObject::connect(row.minEdit, &QLineEdit::editingFinished, owner, [this, index]() {
            commitMinFromEdit(index);
        });
        QObject::connect(row.maxEdit, &QLineEdit::editingFinished, owner, [this, index]() {
            commitMaxFromEdit(index);
        });

        return frame;
    }

    void commitDisplayRange(size_t index, double minV, double maxV)
    {
        if (index >= channels.size() || index >= rows.size()) return;
        channels[index].displayRange.displayMin = minV;
        channels[index].displayRange.displayMax = maxV;
        channels[index].userOverrideRange = true;
        if (rows[index].minEdit) rows[index].minEdit->setText(QString::number(minV));
        if (rows[index].maxEdit) rows[index].maxEdit->setText(QString::number(maxV));
        emit owner->channelSettingsChanged(channels);
    }

    void commitMinFromEdit(size_t index)
    {
        if (index >= channels.size() || !rows[index].minEdit) return;
        bool ok = false;
        double v = rows[index].minEdit->text().toDouble(&ok);
        if (!ok) {
            rows[index].minEdit->setText(QString::number(channels[index].displayRange.displayMin));
            return;
        }
        const double eps = epsilonFor(channels[index].dataType);
        if (v >= channels[index].displayRange.displayMax) {
            v = channels[index].displayRange.displayMax - eps;
        }
        channels[index].displayRange.displayMin = v;
        channels[index].userOverrideRange = true;
        rows[index].minEdit->setText(QString::number(v));
        if (rows[index].histogramView) {
            rows[index].histogramView->setDisplayRange(v, channels[index].displayRange.displayMax);
        }
        emit owner->channelSettingsChanged(channels);
    }

    void commitMaxFromEdit(size_t index)
    {
        if (index >= channels.size() || !rows[index].maxEdit) return;
        bool ok = false;
        double v = rows[index].maxEdit->text().toDouble(&ok);
        if (!ok) {
            rows[index].maxEdit->setText(QString::number(channels[index].displayRange.displayMax));
            return;
        }
        const double eps = epsilonFor(channels[index].dataType);
        if (v <= channels[index].displayRange.displayMin) {
            v = channels[index].displayRange.displayMin + eps;
        }
        channels[index].displayRange.displayMax = v;
        channels[index].userOverrideRange = true;
        rows[index].maxEdit->setText(QString::number(v));
        if (rows[index].histogramView) {
            rows[index].histogramView->setDisplayRange(channels[index].displayRange.displayMin, v);
        }
        emit owner->channelSettingsChanged(channels);
    }

    void applyAutoRange(size_t index)
    {
        if (index >= channels.size() || index >= rows.size()) return;
        auto& ch = channels[index];
        if (!ch.autoDisplayRange.autoDetected) return;
        ch.displayRange = ch.autoDisplayRange;
        ch.userOverrideRange = false;
        syncRowControls(index);
        emit owner->channelSettingsChanged(channels);
    }

    void applyResetRange(size_t index)
    {
        if (index >= channels.size() || index >= rows.size()) return;
        auto& ch = channels[index];
        const auto [rMin, rMax] = dataTypeFullRange(ch.dataType);
        ch.displayRange.displayMin = rMin;
        ch.displayRange.displayMax = rMax;
        ch.displayRange.autoDetected = false;
        ch.userOverrideRange = true;
        syncRowControls(index);
        emit owner->channelSettingsChanged(channels);
    }

    void onMasterClicked()
    {
        // The Qt click handler fires AFTER the widget's check-state is automatically
        // toggled by tri-state cycle, but we ignore the new visual state and re-derive
        // the action from `channels` (the source of truth).
        Qt::CheckState pre = currentMasterState();
        if (pre == Qt::Checked || pre == Qt::PartiallyChecked) {
            // -> all-off, snapshot first
            std::vector<bool> snapshot(channels.size());
            for (size_t i = 0; i < channels.size(); ++i) snapshot[i] = channels[i].visible;
            preToggleVisibility = std::move(snapshot);
            for (auto& ch : channels) ch.visible = false;
        } else {
            // pre == Unchecked
            if (preToggleVisibility.has_value() &&
                preToggleVisibility->size() == channels.size()) {
                for (size_t i = 0; i < channels.size(); ++i) {
                    channels[i].visible = (*preToggleVisibility)[i];
                }
                preToggleVisibility.reset();
            } else {
                for (auto& ch : channels) ch.visible = true;
            }
        }

        // Reflect into per-row checkboxes WITHOUT re-emitting their toggled signals.
        for (size_t i = 0; i < rows.size() && i < channels.size(); ++i) {
            if (rows[i].checkBox) {
                QSignalBlocker block(rows[i].checkBox);
                rows[i].checkBox->setChecked(channels[i].visible);
            }
        }

        refreshMasterState();
        emit owner->channelSettingsChanged(channels);
    }

    Qt::CheckState currentMasterState() const
    {
        if (channels.empty()) return Qt::Unchecked;
        size_t onCount = 0;
        for (const auto& ch : channels) if (ch.visible) ++onCount;
        if (onCount == 0) return Qt::Unchecked;
        if (onCount == channels.size()) return Qt::Checked;
        return Qt::PartiallyChecked;
    }

    void refreshMasterState()
    {
        if (!masterCheckBox || !visibleCountLabel) return;
        QSignalBlocker block(masterCheckBox);
        masterCheckBox->setCheckState(currentMasterState());

        size_t visibleCount = 0;
        for (const auto& ch : channels) if (ch.visible) ++visibleCount;
        visibleCountLabel->setText(QString("%1 visible").arg(visibleCount));
    }

    void syncRowControls(size_t index)
    {
        if (index >= rows.size()) return;
        auto& row = rows[index];
        const auto& ch = channels[index];
        if (row.minEdit) row.minEdit->setText(QString::number(ch.displayRange.displayMin));
        if (row.maxEdit) row.maxEdit->setText(QString::number(ch.displayRange.displayMax));
        if (row.histogramView) {
            row.histogramView->setDisplayRange(ch.displayRange.displayMin,
                                               ch.displayRange.displayMax);
        }
    }

    static std::pair<double, double> dataTypeFullRange(core::DataType dt)
    {
        switch (dt) {
            case core::DataType::Byte:    return {0.0, 255.0};
            case core::DataType::Int8:    return {-128.0, 127.0};
            case core::DataType::UInt16:  return {0.0, 65535.0};
            case core::DataType::Int16:   return {0.0, 32767.0};
            case core::DataType::UInt32:  return {0.0, 4294967295.0};
            case core::DataType::Int32:   return {0.0, 2147483647.0};
            case core::DataType::Int64:   return {-9223372036854775808.0, 9223372036854775807.0};
            case core::DataType::UInt64:  return {0.0, 18446744073709551615.0};
            case core::DataType::Float16: return {0.0, 1.0};
            case core::DataType::Float32: return {0.0, 1.0};
            case core::DataType::Float64: return {0.0, 1.0};
            default:                      return {0.0, 255.0};
        }
    }

    static double epsilonFor(core::DataType dt)
    {
        switch (dt) {
            case core::DataType::Float16:
            case core::DataType::Float32:
            case core::DataType::Float64:
                return 1e-6;
            default:
                return 1.0;  // integer types snap by 1
        }
    }

    void applyColorButtonStyle(QPushButton* button, float r, float g, float b)
    {
        int ri = static_cast<int>(std::round(r * 255.0f));
        int gi = static_cast<int>(std::round(g * 255.0f));
        int bi = static_cast<int>(std::round(b * 255.0f));
        button->setStyleSheet(
            QString("QPushButton { background-color: rgb(%1,%2,%3); border: 1px solid #888; "
                    "min-width: 32px; max-width: 32px; min-height: 24px; max-height: 24px; }")
                .arg(ri).arg(gi).arg(bi));
    }

    void onColorButtonClicked(size_t index)
    {
        if (index >= channels.size()) {
            return;
        }

        const auto& ch = channels[index];
        int ri = static_cast<int>(std::round(ch.colorR * 255.0f));
        int gi = static_cast<int>(std::round(ch.colorG * 255.0f));
        int bi = static_cast<int>(std::round(ch.colorB * 255.0f));

        QColor initial(ri, gi, bi);
        QColor chosen = QColorDialog::getColor(initial, owner, "Select Channel Color");
        if (!chosen.isValid()) {
            return;
        }

        channels[index].colorR = static_cast<float>(chosen.red()) / 255.0f;
        channels[index].colorG = static_cast<float>(chosen.green()) / 255.0f;
        channels[index].colorB = static_cast<float>(chosen.blue()) / 255.0f;

        applyColorButtonStyle(rows[index].colorButton,
                              channels[index].colorR,
                              channels[index].colorG,
                              channels[index].colorB);

        if (rows[index].histogramView) {
            rows[index].histogramView->setChannelColor(
                channels[index].colorR,
                channels[index].colorG,
                channels[index].colorB);
        }

        emit owner->channelSettingsChanged(channels);
    }
};

ChannelMixerPanel::ChannelMixerPanel(QWidget* parent)
    : QDockWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;

    setWindowTitle("Channels");
    setMinimumWidth(200);
    setFeatures(QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);

    m_impl->buildUi();
}

ChannelMixerPanel::~ChannelMixerPanel() = default;

void ChannelMixerPanel::setChannels(const std::vector<core::ChannelInfo>& channels)
{
    m_impl->channels = channels;
    m_impl->preToggleVisibility.reset();
    m_impl->rebuildRows();
}

void ChannelMixerPanel::clearChannels()
{
    m_impl->channels.clear();
    m_impl->preToggleVisibility.reset();
    m_impl->clearRows();
}

std::vector<core::ChannelInfo> ChannelMixerPanel::channelSettings() const
{
    return m_impl->channels;
}

} // namespace slideio::viewer::ui
