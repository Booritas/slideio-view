#pragma once

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
    void setResolution(double metersPerPixel);

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
    bool m_updatingSlider;

    static constexpr int kSliderMinimum = 0;
    static constexpr int kSliderMaximum = 1000;
    static constexpr double kMinScale = 0.001;
    static constexpr double kMaxScale = 100.0;
};

} // namespace slideio::viewer::ui
