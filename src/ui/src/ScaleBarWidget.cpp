#include "slideio/viewer/ui/ScaleBarWidget.h"

#include "slideio/viewer/core/ScaleBar.h"
#include "slideio/viewer/ui/CalibrationPolicy.h"

#include <QFont>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QStyle>

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

void ScaleBarWidget::setScale(double metersPerPixelNative, double viewportScale, core::CalibrationSource source)
{
    m_metersPerPixelNative = metersPerPixelNative;
    m_viewportScale        = viewportScale;
    m_source               = source;

    // An uncalibrated slide keeps the widget, to say so; only no slide hides it.
    const bool slideOpen = viewportScale > 0.0;
    const bool valid = slideOpen && (source == core::CalibrationSource::None || metersPerPixelNative > 0.0);
    setVisible(valid);
    if (valid) {
        update();
    }
}

void ScaleBarWidget::paintEvent(QPaintEvent* /*event*/)
{
    if (m_viewportScale <= 0.0) {
        return;
    }
    if (m_source == core::CalibrationSource::None) {
        // No bar at all: a bar would have to claim a length (UI design 11.5).
        QPainter painter(this);
        const int iconSize = kLabelHeight;
        style()->standardIcon(QStyle::SP_MessageBoxWarning)
            .paint(&painter, QRect(kHandleInset, kBarY, iconSize, iconSize));
        QFont labelFont = painter.font();
        labelFont.setPixelSize(10);
        painter.setFont(labelFont);
        painter.setPen(QColor("#CCCCCC"));
        painter.drawText(QRect(kHandleInset + iconSize + 4, kBarY, width(), iconSize),
                         Qt::AlignLeft | Qt::AlignVCenter, tr("Uncalibrated"));
        return;
    }
    if (m_metersPerPixelNative <= 0.0) {
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

    const QString text  = formatLabel(tick.lengthMicrons) + QString::fromStdString(scaleBarSuffix(m_source));
    const int labelY    = kBarY + kCapHalfLen + kLabelGap;
    // Centred under the bar where it fits, but kept inside the widget: a short
    // bar near the left edge with " (manual)" appended would otherwise start
    // the label off-screen and drop its leading digit.
    const int textWidth = painter.fontMetrics().horizontalAdvance(text);
    const int left = scaleBarLabelLeft((x0 + x1) / 2, textWidth, width());
    const QRect textRect(left, labelY, width() - left, kLabelHeight);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, text);
}

} // namespace slideio::viewer::ui
