#pragma once

#include <QWidget>

namespace slideio::viewer::ui
{

class ScaleBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScaleBarWidget(QWidget* parent = nullptr);
    ~ScaleBarWidget() override;

    /// metersPerPixelNative: SlideInfo::resolutionX (level 0 pixel size).
    /// viewportScale:        viewport.scale() (1.0 = native).
    /// Either <= 0 hides the widget; otherwise the widget is visible and repaints.
    void setScale(double metersPerPixelNative, double viewportScale);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_metersPerPixelNative = 0.0;
    double m_viewportScale        = 0.0;

    // Half the QSlider handle width (see ZoomIndicatorWidget stylesheet: handle
    // "width: 12px; border-radius: 6px"). Aligns the bar with the slider groove.
    static constexpr int kHandleInset = 6;
    static constexpr int kBarY        = 4;   // y-position of the line
    static constexpr int kCapHalfLen  = 3;   // end caps span [kBarY-3, kBarY+3]
    static constexpr int kLabelGap    = 6;   // px between line and label top
    static constexpr int kLabelHeight = 14;  // px reserved for the label
};

} // namespace slideio::viewer::ui
