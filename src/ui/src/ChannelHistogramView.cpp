#include "slideio/viewer/ui/ChannelHistogramView.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kHandleHitWidth = 8;
constexpr int kHandleVisualWidth = 2;
constexpr int kFixedHeight = 60;

double clampToRange(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
} // namespace

ChannelHistogramView::ChannelHistogramView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(kFixedHeight);
    setMaximumHeight(kFixedHeight);
    setCursor(Qt::ArrowCursor);
}

void ChannelHistogramView::setHistogram(const core::ChannelHistogram& h)
{
    m_histogram = h;
    if (m_displayMin == 0.0 && m_displayMax == 0.0) {
        m_displayMin = h.rangeMin;
        m_displayMax = h.rangeMax;
    }
    update();
}

void ChannelHistogramView::setDisplayRange(double minVal, double maxVal)
{
    m_displayMin = minVal;
    m_displayMax = maxVal;
    update();
}

void ChannelHistogramView::setChannelColor(float r, float g, float b)
{
    m_colorR = r;
    m_colorG = g;
    m_colorB = b;
    update();
}

void ChannelHistogramView::setLogScale(bool log)
{
    m_logScale = log;
    update();
}

QSize ChannelHistogramView::sizeHint() const
{
    return {200, kFixedHeight};
}

int ChannelHistogramView::valueToX(double v) const
{
    const double span = m_histogram.rangeMax - m_histogram.rangeMin;
    if (span <= 0.0 || width() <= 0) return 0;
    const double frac = (v - m_histogram.rangeMin) / span;
    const double xd = clampToRange(frac, 0.0, 1.0) * (width() - 1);
    return static_cast<int>(std::round(xd));
}

double ChannelHistogramView::xToValue(int x) const
{
    if (width() <= 1) return m_histogram.rangeMin;
    const double frac = clampToRange(
        static_cast<double>(x) / static_cast<double>(width() - 1), 0.0, 1.0);
    return m_histogram.rangeMin + frac * (m_histogram.rangeMax - m_histogram.rangeMin);
}

ChannelHistogramView::Handle ChannelHistogramView::hitHandle(int x) const
{
    const int minX = valueToX(m_displayMin);
    const int maxX = valueToX(m_displayMax);
    if (std::abs(x - minX) <= kHandleHitWidth / 2 + 2) return Handle::Min;
    if (std::abs(x - maxX) <= kHandleHitWidth / 2 + 2) return Handle::Max;
    return Handle::None;
}

void ChannelHistogramView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x25, 0x25, 0x25));

    if (!m_histogram.valid || m_histogram.bins.empty()) {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Histogram unavailable"));
        return;
    }

    // Find max bin count for normalization.
    uint32_t maxCount = 0;
    for (uint32_t v : m_histogram.bins) maxCount = std::max(maxCount, v);
    if (maxCount == 0) maxCount = 1;

    const int numBins = static_cast<int>(m_histogram.bins.size());
    const QColor fillColor(
        static_cast<int>(std::round(m_colorR * 255.0f)),
        static_cast<int>(std::round(m_colorG * 255.0f)),
        static_cast<int>(std::round(m_colorB * 255.0f)),
        102);  // ~40% alpha
    const QColor strokeColor(
        static_cast<int>(std::round(m_colorR * 255.0f)),
        static_cast<int>(std::round(m_colorG * 255.0f)),
        static_cast<int>(std::round(m_colorB * 255.0f)));

    // Build histogram polygon.
    QPainterPath path;
    path.moveTo(0, h);
    for (int i = 0; i < numBins; ++i) {
        const double xd = static_cast<double>(i) / (numBins - 1) * (w - 1);
        double frac;
        if (m_logScale) {
            const double num = std::log(1.0 + static_cast<double>(m_histogram.bins[i]));
            const double den = std::log(1.0 + static_cast<double>(maxCount));
            frac = (den > 0.0) ? (num / den) : 0.0;
        } else {
            frac = static_cast<double>(m_histogram.bins[i]) / static_cast<double>(maxCount);
        }
        const double yd = h - 1 - frac * (h - 2);
        path.lineTo(xd, yd);
    }
    path.lineTo(w - 1, h);
    path.closeSubpath();

    p.fillPath(path, fillColor);
    p.setPen(QPen(strokeColor, 1.0));
    p.drawPath(path);

    // Clipped-region overlays (outside [displayMin, displayMax]).
    const int minX = valueToX(m_displayMin);
    const int maxX = valueToX(m_displayMax);
    p.fillRect(QRect(0, 0, minX, h), QColor(0, 0, 0, 64));
    p.fillRect(QRect(maxX + 1, 0, w - maxX - 1, h), QColor(0, 0, 0, 64));

    // Handle lines.
    QPen handlePen(QColor(0xFF, 0xCC, 0x33), kHandleVisualWidth);
    p.setPen(handlePen);
    p.drawLine(minX, 0, minX, h - 1);
    p.drawLine(maxX, 0, maxX, h - 1);
}

void ChannelHistogramView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_dragHandle = hitHandle(event->pos().x());
    if (m_dragHandle != Handle::None) {
        setCursor(Qt::SplitHCursor);
    }
}

void ChannelHistogramView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragHandle == Handle::None) {
        // Hover affordance only.
        setCursor(hitHandle(event->pos().x()) == Handle::None
                  ? Qt::ArrowCursor : Qt::SplitHCursor);
        return;
    }

    const double newVal = xToValue(event->pos().x());
    if (m_dragHandle == Handle::Min) {
        double clamped = clampToRange(newVal, m_histogram.rangeMin,
                                      m_displayMax - 1e-6);
        if (clamped >= m_displayMax) clamped = m_displayMax - 1e-6;
        m_displayMin = clamped;
    } else {
        double clamped = clampToRange(newVal, m_displayMin + 1e-6,
                                      m_histogram.rangeMax);
        if (clamped <= m_displayMin) clamped = m_displayMin + 1e-6;
        m_displayMax = clamped;
    }
    update();
    emit displayRangeChanged(m_displayMin, m_displayMax);
}

void ChannelHistogramView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_dragHandle != Handle::None) {
        m_dragHandle = Handle::None;
        setCursor(hitHandle(event->pos().x()) == Handle::None
                  ? Qt::ArrowCursor : Qt::SplitHCursor);
        emit displayRangeCommitted(m_displayMin, m_displayMax);
    }
}

} // namespace slideio::viewer::ui
