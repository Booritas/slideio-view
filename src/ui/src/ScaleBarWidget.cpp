#include "slideio/viewer/ui/ScaleBarWidget.h"

#include "slideio/viewer/core/ScaleBar.h"

#include <QFont>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QString>

namespace slideio::viewer::ui
{

namespace
{

QString formatLabel(double lengthMicrons)
{
    // Sub-um lengths get one decimal; whole-um lengths print as integer.
    if (lengthMicrons < 1.0) {
        return QString::fromUtf8("%1 \xC2\xB5m")
            .arg(lengthMicrons, 0, 'f', 1);
    }
    return QString::fromUtf8("%1 \xC2\xB5m")
        .arg(lengthMicrons, 0, 'f', 0);
}

} // namespace

ScaleBarWidget::ScaleBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(kBarY + kCapHalfLen + kLabelGap + kLabelHeight + 1); // 28 px
}

ScaleBarWidget::~ScaleBarWidget() = default;

void ScaleBarWidget::setScale(double metersPerPixelNative, double viewportScale)
{
    m_metersPerPixelNative = metersPerPixelNative;
    m_viewportScale        = viewportScale;

    const bool valid = metersPerPixelNative > 0.0 && viewportScale > 0.0;
    setVisible(valid);
    if (valid) {
        update();
    }
}

void ScaleBarWidget::paintEvent(QPaintEvent* /*event*/)
{
    if (m_metersPerPixelNative <= 0.0 || m_viewportScale <= 0.0) {
        return;
    }

    const double micronsPerScreenPixel =
        m_metersPerPixelNative * 1.0e6 / m_viewportScale;

    const int maxBarPx = width() - 2 * kHandleInset;
    if (maxBarPx <= 0) {
        return;
    }

    const auto tick = core::pickScaleBarLength(micronsPerScreenPixel, maxBarPx);
    if (tick.lengthMicrons <= 0.0 || tick.pixelWidth <= 0) {
        return;
    }

    const int x0 = kHandleInset;
    const int x1 = x0 + tick.pixelWidth;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false); // crisp 1px lines

    QPen pen(QColor("#CCCCCC"));
    pen.setWidth(1);
    painter.setPen(pen);

    // Horizontal line.
    painter.drawLine(x0, kBarY, x1, kBarY);

    // End caps.
    painter.drawLine(x0, kBarY - kCapHalfLen, x0, kBarY + kCapHalfLen);
    painter.drawLine(x1, kBarY - kCapHalfLen, x1, kBarY + kCapHalfLen);

    // Label centered under the bar.
    QFont labelFont = painter.font();
    labelFont.setPixelSize(10);
    painter.setFont(labelFont);

    const QString text  = formatLabel(tick.lengthMicrons);
    const int labelY    = kBarY + kCapHalfLen + kLabelGap;
    const QRect textRect(x0, labelY, x1 - x0, kLabelHeight);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);
}

} // namespace slideio::viewer::ui
