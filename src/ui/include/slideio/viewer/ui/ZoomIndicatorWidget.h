#pragma once

#include "slideio/viewer/core/Calibration.h"

#include <QWidget>

class QLabel;
class QSlider;

namespace slideio::viewer::ui
{

class ScaleBarWidget;

class ZoomIndicatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZoomIndicatorWidget(QWidget* parent = nullptr);
    ~ZoomIndicatorWidget() override;

    void setZoomLevel(double scale, double baseMagnification);
    /// The open scene's calibration; the scale bar draws it, or "Uncalibrated".
    void setCalibration(const core::Calibration& calibration);
    /// No slide open: the scale bar is hidden rather than saying "Uncalibrated".
    void clearCalibration();

signals:
    void zoomChanged(double newScale);

private slots:
    void onSliderValueChanged(int value);

private:
    double sliderValueToScale(int value) const;
    int scaleToSliderValue(double scale) const;

    QLabel* m_magnificationLabel;
    QLabel* m_percentageLabel;
    ScaleBarWidget* m_scaleBar;
    QSlider* m_slider;

    double m_baseMagnification;
    double m_currentScale;
    double m_metersPerPixel;
    core::CalibrationSource m_calibrationSource = core::CalibrationSource::None;
    bool m_hasSlide = false;
    bool m_updatingSlider;

    static constexpr int kSliderMinimum = 0;
    static constexpr int kSliderMaximum = 1000;
    static constexpr double kMinScale = 0.001;
    static constexpr double kMaxScale = 100.0;
};

} // namespace slideio::viewer::ui
