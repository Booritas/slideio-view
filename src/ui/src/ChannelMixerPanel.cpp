#include "slideio/viewer/ui/ChannelMixerPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace slideio::viewer::ui
{

struct ChannelMixerPanel::Impl
{
    struct ChannelRow
    {
        QToolButton* disclosure = nullptr;
        QCheckBox*   checkBox = nullptr;
        QLabel*      nameLabel = nullptr;
        QPushButton* colorButton = nullptr;
        QSlider*     intensitySlider = nullptr;
        QWidget*     expandedContainer = nullptr;  // null until first expanded
        bool         expanded = false;
    };

    ChannelMixerPanel* owner = nullptr;
    std::vector<core::ChannelInfo> channels;
    std::vector<ChannelRow> rows;

    QScrollArea* scrollArea = nullptr;
    QWidget* contentWidget = nullptr;
    QVBoxLayout* contentLayout = nullptr;

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
        for (size_t i = 0; i < rows.size(); ++i) previousExpanded[i] = rows[i].expanded;

        clearRows();
        rows.reserve(channels.size());

        for (size_t i = 0; i < channels.size(); ++i) {
            const auto& ch = channels[i];
            ChannelRow row;

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
                // Placeholder — Task 9 fills this in with the histogram view.
                auto* placeholder = new QLabel("(expanded)", contentWidget);
                placeholder->setStyleSheet("color:#666; margin-left: 24px;");
                row.expandedContainer = placeholder;
                contentLayout->addWidget(placeholder);
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
    m_impl->rebuildRows();
}

void ChannelMixerPanel::clearChannels()
{
    m_impl->channels.clear();
    m_impl->clearRows();
}

std::vector<core::ChannelInfo> ChannelMixerPanel::channelSettings() const
{
    return m_impl->channels;
}

} // namespace slideio::viewer::ui
