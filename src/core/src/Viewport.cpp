#include "slideio/viewer/core/Viewport.h"

#include <algorithm>

namespace slideio::viewer::core
{

Viewport::Viewport()
    : m_centerX(0.0)
    , m_centerY(0.0)
    , m_scale(1.0)
    , m_screenWidth(0)
    , m_screenHeight(0)
{
}

Viewport::Viewport(double centerX, double centerY, double scale, int screenWidth, int screenHeight)
    : m_centerX(centerX)
    , m_centerY(centerY)
    , m_scale(scale)
    , m_screenWidth(screenWidth)
    , m_screenHeight(screenHeight)
{
}

double Viewport::centerX() const
{
    return m_centerX;
}

double Viewport::centerY() const
{
    return m_centerY;
}

double Viewport::scale() const
{
    return m_scale;
}

int Viewport::screenWidth() const
{
    return m_screenWidth;
}

int Viewport::screenHeight() const
{
    return m_screenHeight;
}

void Viewport::setCenterX(double x)
{
    m_centerX = x;
}

void Viewport::setCenterY(double y)
{
    m_centerY = y;
}

void Viewport::setScale(double s)
{
    m_scale = s;
}

void Viewport::setScreenSize(int width, int height)
{
    m_screenWidth = width;
    m_screenHeight = height;
}

void Viewport::pan(double screenDx, double screenDy)
{
    // Convert screen pixel delta to slide pixel delta
    // screen_pixels = slide_pixels * scale, so slide_pixels = screen_pixels / scale
    if (m_scale > 0.0) {
        m_centerX -= screenDx / m_scale;
        m_centerY -= screenDy / m_scale;
    }
}

void Viewport::zoomToPoint(double screenX, double screenY, double newScale)
{
    if (m_scale <= 0.0 || newScale <= 0.0) {
        return;
    }

    // Find the slide coordinate under (screenX, screenY) before the zoom
    double slideX = 0.0;
    double slideY = 0.0;
    screenToSlide(screenX, screenY, slideX, slideY);

    // Apply the new scale
    m_scale = newScale;

    // After changing scale, the point (slideX, slideY) should still appear at (screenX, screenY).
    // screenX = (slideX - centerX) * newScale + screenWidth / 2
    // => centerX = slideX - (screenX - screenWidth / 2) / newScale
    m_centerX = slideX - (screenX - m_screenWidth / 2.0) / m_scale;
    m_centerY = slideY - (screenY - m_screenHeight / 2.0) / m_scale;
}

void Viewport::fitToSlide(int slideWidth, int slideHeight)
{
    if (slideWidth <= 0 || slideHeight <= 0 || m_screenWidth <= 0 || m_screenHeight <= 0) {
        return;
    }

    m_centerX = slideWidth / 2.0;
    m_centerY = slideHeight / 2.0;

    double scaleX = static_cast<double>(m_screenWidth) / slideWidth;
    double scaleY = static_cast<double>(m_screenHeight) / slideHeight;
    m_scale = std::min(scaleX, scaleY);
}

void Viewport::fitToRect(const Rect<double>& target, double marginFraction)
{
    if (m_screenWidth <= 0 || m_screenHeight <= 0) {
        return;
    }

    m_centerX = target.x + target.width / 2.0;
    m_centerY = target.y + target.height / 2.0;

    // Clamped so a caller cannot ask for a margin that consumes the whole
    // screen, which would leave no room for the annotation it is trying to show.
    const double margin = std::max(0.0, std::min(marginFraction, 0.45));
    const double usableWidth = m_screenWidth * (1.0 - 2.0 * margin);
    const double usableHeight = m_screenHeight * (1.0 - 2.0 * margin);

    // Each axis contributes a candidate scale only if it has extent. A zero
    // width or height is not a malformed annotation -- a freehand line drawn
    // along one axis produces exactly that -- so it is skipped, not rejected.
    double scale = 0.0;
    if (target.width > 0.0) {
        scale = usableWidth / target.width;
    }
    if (target.height > 0.0) {
        const double scaleY = usableHeight / target.height;
        scale = scale > 0.0 ? std::min(scale, scaleY) : scaleY;
    }

    // Both axes degenerate: a point, or a trace whose vertices coincide. There
    // is no zoom the box implies, so the current one is kept.
    if (scale > 0.0) {
        m_scale = scale;
    }
}

void Viewport::setActualPixels(int slideWidth, int slideHeight)
{
    m_scale = 1.0;
    m_centerX = slideWidth / 2.0;
    m_centerY = slideHeight / 2.0;
}

Rect<double> Viewport::visibleSlideRect() const
{
    Rect<double> rect;
    if (m_scale <= 0.0) {
        return rect;
    }

    double halfScreenW = m_screenWidth / 2.0;
    double halfScreenH = m_screenHeight / 2.0;

    rect.width = m_screenWidth / m_scale;
    rect.height = m_screenHeight / m_scale;
    rect.x = m_centerX - halfScreenW / m_scale;
    rect.y = m_centerY - halfScreenH / m_scale;

    return rect;
}

void Viewport::screenToSlide(double sx, double sy, double& slideX, double& slideY) const
{
    // The center of the screen maps to (m_centerX, m_centerY) in slide coordinates.
    // Each screen pixel = 1/scale slide pixels.
    if (m_scale > 0.0) {
        slideX = m_centerX + (sx - m_screenWidth / 2.0) / m_scale;
        slideY = m_centerY + (sy - m_screenHeight / 2.0) / m_scale;
    } else {
        slideX = m_centerX;
        slideY = m_centerY;
    }
}

void Viewport::slideToScreen(double slideX, double slideY, double& sx, double& sy) const
{
    sx = (slideX - m_centerX) * m_scale + m_screenWidth / 2.0;
    sy = (slideY - m_centerY) * m_scale + m_screenHeight / 2.0;
}

} // namespace slideio::viewer::core
