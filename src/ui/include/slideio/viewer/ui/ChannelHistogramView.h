#pragma once

#include "slideio/viewer/core/Types.h"

#include <QWidget>

namespace slideio::viewer::ui
{

class ChannelHistogramView : public QWidget
{
    Q_OBJECT
public:
    explicit ChannelHistogramView(QWidget* parent = nullptr);

    void setHistogram(const core::ChannelHistogram& h);
    void setDisplayRange(double minVal, double maxVal);
    void setChannelColor(float r, float g, float b);
    void setLogScale(bool log);

signals:
    void displayRangeChanged(double minVal, double maxVal);
    void displayRangeCommitted(double minVal, double maxVal);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    enum class Handle { None, Min, Max };

    Handle hitHandle(int x) const;
    int valueToX(double v) const;
    double xToValue(int x) const;

    core::ChannelHistogram m_histogram;
    double m_displayMin = 0.0;
    double m_displayMax = 0.0;
    float m_colorR = 1.0f;
    float m_colorG = 1.0f;
    float m_colorB = 1.0f;
    bool m_logScale = true;
    bool m_displayRangeSet = false;
    Handle m_dragHandle = Handle::None;
};

} // namespace slideio::viewer::ui
