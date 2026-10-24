#pragma once

#include "slideio/viewer/core/Calibration.h"

#include <QWidget>

namespace slideio::viewer::ui
{

class ScaleBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScaleBarWidget(QWidget* parent = nullptr);
    ~ScaleBarWidget() override;

    /// metersPerPixelNative: the calibrated level-0 pixel size.
    /// viewportScale:        viewport.scale() (1.0 = native); <= 0 means no
    ///                       slide, and hides the widget.
    /// source:               None draws "Uncalibrated" with a warning icon in
    ///                       place of a bar; Manual marks the length "(manual)".
    void setScale(double metersPerPixelNative, double viewportScale, core::CalibrationSource source);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_metersPerPixelNative = 0.0;
    double m_viewportScale        = 0.0;
    core::CalibrationSource m_source = core::CalibrationSource::None;

    // Half the QSlider handle width (see ZoomIndicatorWidget stylesheet: handle
    // "width: 12px; border-radius: 6px"). Aligns the bar with the slider groove.
    static constexpr int kHandleInset = 6;
    static constexpr int kBarY        = 4;   // y-position of the line
    static constexpr int kCapHalfLen  = 3;   // end caps span [kBarY-3, kBarY+3]
    static constexpr int kLabelGap    = 6;   // px between line and label top
    static constexpr int kLabelHeight = 14;  // px reserved for the label
};

} // namespace slideio::viewer::ui
