#include "slideio/viewer/ui/MinimapWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace slideio::viewer::ui
{

MinimapWidget::MinimapWidget(QWidget* parent)
    : QWidget(parent)
    , m_slideWidth(0)
    , m_slideHeight(0)
    , m_isDragging(false)
    , m_displayScale(1.0)
    , m_displayOffset(0.0, 0.0)
{
    setFixedSize(kBaseSize, kBaseSize);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
}

MinimapWidget::~MinimapWidget() = default;

void MinimapWidget::setViewport(const core::Viewport& vp, int slideWidth, int slideHeight)
{
    m_viewport = vp;
    m_slideWidth = slideWidth;
    m_slideHeight = slideHeight;

    if (m_slideWidth > 0 && m_slideHeight > 0) {
        double aspectRatio = static_cast<double>(m_slideWidth) / static_cast<double>(m_slideHeight);

        int displayWidth = kBaseSize;
        int displayHeight = kBaseSize;

        if (aspectRatio > 1.0) {
            displayHeight = static_cast<int>(std::round(kBaseSize / aspectRatio));
        } else {
            displayWidth = static_cast<int>(std::round(kBaseSize * aspectRatio));
        }

        setFixedSize(displayWidth, displayHeight);

        m_displayScale = static_cast<double>(displayWidth) / static_cast<double>(m_slideWidth);
        m_displayOffset = QPointF(0.0, 0.0);
    }

    update();
}

void MinimapWidget::setThumbnail(const QImage& thumbnail)
{
    m_thumbnail = thumbnail;
    update();
}

void MinimapWidget::clearThumbnail()
{
    m_thumbnail = QImage();
    m_overviewAvailable = true;
    update();
}

void MinimapWidget::setOverviewAvailable(bool available)
{
    if (m_overviewAvailable == available) {
        return;
    }
    m_overviewAvailable = available;
    update();
}

void MinimapWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Background
    painter.fillRect(rect(), QColor(30, 30, 30, 200));

    if (m_slideWidth <= 0 || m_slideHeight <= 0) {
        return;
    }

    // Draw thumbnail
    if (!m_thumbnail.isNull()) {
        QRectF destRect(m_displayOffset.x(), m_displayOffset.y(),
                        width() - 2.0 * m_displayOffset.x(),
                        height() - 2.0 * m_displayOffset.y());
        painter.drawImage(destRect, m_thumbnail);
    } else {
        QRectF slideRect(m_displayOffset.x(), m_displayOffset.y(),
                         width() - 2.0 * m_displayOffset.x(),
                         height() - 2.0 * m_displayOffset.y());
        painter.fillRect(slideRect, QColor(80, 80, 80));

        if (!m_overviewAvailable) {
            // Say why the overview is missing rather than leaving a blank
            // rectangle the user would read as "still loading".
            painter.setPen(QColor(220, 220, 220));
            QFont noticeFont = painter.font();
            noticeFont.setPointSizeF(std::max(6.0, noticeFont.pointSizeF() - 1.0));
            painter.setFont(noticeFont);
            painter.drawText(slideRect.adjusted(4, 4, -4, -4),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             QStringLiteral("Overview unavailable\n(slide has no downsampled level)"));
        }
    }

    // Draw viewport rectangle
    QRectF vpRect = viewportRectInWidget();
    if (vpRect.isValid()) {
        QPen pen(QColor(255, 0, 0), 2.0);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 0, 0, 30));
        painter.drawRect(vpRect);
    }

    // Draw border
    QPen borderPen(QColor(100, 100, 100), 1.0);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void MinimapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        navigateToScreenPos(event->pos());
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void MinimapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        navigateToScreenPos(event->pos());
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void MinimapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void MinimapWidget::navigateToScreenPos(const QPoint& pos)
{
    if (m_slideWidth <= 0 || m_slideHeight <= 0 || m_displayScale <= 0.0) {
        return;
    }

    double slideX = (static_cast<double>(pos.x()) - m_displayOffset.x()) / m_displayScale;
    double slideY = (static_cast<double>(pos.y()) - m_displayOffset.y()) / m_displayScale;

    slideX = std::max(0.0, std::min(slideX, static_cast<double>(m_slideWidth)));
    slideY = std::max(0.0, std::min(slideY, static_cast<double>(m_slideHeight)));

    emit navigationRequested(slideX, slideY);
}

QRectF MinimapWidget::viewportRectInWidget() const
{
    if (m_slideWidth <= 0 || m_slideHeight <= 0) {
        return QRectF();
    }

    auto visRect = m_viewport.visibleSlideRect();

    double x = visRect.x * m_displayScale + m_displayOffset.x();
    double y = visRect.y * m_displayScale + m_displayOffset.y();
    double w = visRect.width * m_displayScale;
    double h = visRect.height * m_displayScale;

    return QRectF(x, y, w, h);
}

} // namespace slideio::viewer::ui
