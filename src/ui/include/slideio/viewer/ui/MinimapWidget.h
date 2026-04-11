#pragma once

#include "slideio/viewer/core/Viewport.h"

#include <QImage>
#include <QWidget>

#include <memory>

namespace slideio::viewer::core
{
class ITileCache;
class TilePyramid;
} // namespace slideio::viewer::core

namespace slideio::viewer::ui
{

class MinimapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MinimapWidget(QWidget* parent = nullptr);
    ~MinimapWidget() override;

    void setViewport(const core::Viewport& vp, int slideWidth, int slideHeight);
    void setThumbnail(const QImage& thumbnail);
    void clearThumbnail();

signals:
    void navigationRequested(double slideCenterX, double slideCenterY);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void navigateToScreenPos(const QPoint& pos);
    QRectF viewportRectInWidget() const;

    static constexpr int kBaseSize = 200;

    QImage m_thumbnail;
    core::Viewport m_viewport;
    int m_slideWidth;
    int m_slideHeight;
    bool m_isDragging;
    double m_displayScale;
    QPointF m_displayOffset;
};

} // namespace slideio::viewer::ui
