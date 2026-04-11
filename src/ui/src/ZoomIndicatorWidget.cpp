#include "slideio/viewer/ui/ZoomIndicatorWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace slideio::viewer::ui
{

ZoomIndicatorWidget::ZoomIndicatorWidget(QWidget* parent)
    : QWidget(parent)
    , m_magnificationLabel(new QLabel("0.0x", this))
    , m_percentageLabel(new QLabel("0%", this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_baseMagnification(0.0)
    , m_currentScale(1.0)
    , m_updatingSlider(false)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(200);
    setFixedHeight(60);

    m_magnificationLabel->setStyleSheet(
        "QLabel { color: #FFFFFF; font-size: 12px; font-weight: bold; background: transparent; }");
    m_percentageLabel->setStyleSheet(
        "QLabel { color: #CCCCCC; font-size: 10px; background: transparent; }");

    m_slider->setMinimum(kSliderMinimum);
    m_slider->setMaximum(kSliderMaximum);
    m_slider->setValue(kSliderMaximum / 2);
    m_slider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  border: 1px solid #555555;"
        "  height: 4px;"
        "  background: #404040;"
        "  margin: 2px 0;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #CCCCCC;"
        "  border: 1px solid #888888;"
        "  width: 12px;"
        "  margin: -4px 0;"
        "  border-radius: 6px;"
        "}");

    auto* labelsLayout = new QHBoxLayout();
    labelsLayout->setContentsMargins(0, 0, 0, 0);
    labelsLayout->addWidget(m_magnificationLabel);
    labelsLayout->addStretch();
    labelsLayout->addWidget(m_percentageLabel);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 4, 8, 4);
    mainLayout->setSpacing(2);
    mainLayout->addLayout(labelsLayout);
    mainLayout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, &ZoomIndicatorWidget::onSliderValueChanged);
}

ZoomIndicatorWidget::~ZoomIndicatorWidget() = default;

void ZoomIndicatorWidget::setZoomLevel(double scale, double baseMagnification)
{
    m_currentScale = scale;
    m_baseMagnification = baseMagnification;

    double percentage = scale * 100.0;
    m_percentageLabel->setText(QString::number(percentage, 'f', 1) + "%");

    if (baseMagnification > 0.0) {
        double magnification = scale * baseMagnification;
        m_magnificationLabel->setText(QString::number(magnification, 'f', 1) + "x");
    } else {
        m_magnificationLabel->setText(QString::number(percentage, 'f', 1) + "%");
    }

    m_updatingSlider = true;
    m_slider->setValue(scaleToSliderValue(scale));
    m_updatingSlider = false;
}

void ZoomIndicatorWidget::onSliderValueChanged(int value)
{
    if (m_updatingSlider) {
        return;
    }

    double newScale = sliderValueToScale(value);
    m_currentScale = newScale;

    double percentage = newScale * 100.0;
    m_percentageLabel->setText(QString::number(percentage, 'f', 1) + "%");

    if (m_baseMagnification > 0.0) {
        double magnification = newScale * m_baseMagnification;
        m_magnificationLabel->setText(QString::number(magnification, 'f', 1) + "x");
    } else {
        m_percentageLabel->setText(QString::number(percentage, 'f', 1) + "%");
    }

    emit zoomChanged(newScale);
}

double ZoomIndicatorWidget::sliderValueToScale(int value) const
{
    // Logarithmic mapping: slider position [0, 1000] -> scale [kMinScale, kMaxScale]
    double t = static_cast<double>(value - kSliderMinimum) /
               static_cast<double>(kSliderMaximum - kSliderMinimum);

    double logMin = std::log(kMinScale);
    double logMax = std::log(kMaxScale);
    double logScale = logMin + t * (logMax - logMin);

    return std::exp(logScale);
}

int ZoomIndicatorWidget::scaleToSliderValue(double scale) const
{
    // Inverse of sliderValueToScale
    scale = std::max(kMinScale, std::min(kMaxScale, scale));

    double logMin = std::log(kMinScale);
    double logMax = std::log(kMaxScale);
    double logScale = std::log(scale);

    double t = (logScale - logMin) / (logMax - logMin);

    return kSliderMinimum + static_cast<int>(std::round(t * (kSliderMaximum - kSliderMinimum)));
}

} // namespace slideio::viewer::ui
