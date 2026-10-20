#pragma once

#include "slideio/viewer/core/Types.h"

namespace slideio::viewer::core
{

class Viewport
{
public:
    Viewport();
    Viewport(double centerX, double centerY, double scale, int screenWidth, int screenHeight);

    double centerX() const;
    double centerY() const;
    double scale() const;
    int screenWidth() const;
    int screenHeight() const;

    void setCenterX(double x);
    void setCenterY(double y);
    void setScale(double s);
    void setScreenSize(int width, int height);

    void pan(double screenDx, double screenDy);

    void zoomToPoint(double screenX, double screenY, double newScale);

    void fitToSlide(int slideWidth, int slideHeight);

    /// Centres on `target` and zooms so it fits the screen with
    /// `marginFraction` of the screen left free on each side (0.1 leaves 10%
    /// top, bottom, left and right). Used to bring one annotation into view.
    ///
    /// `marginFraction` is clamped to [0, 0.45]. At 0.5 the usable width is
    /// zero and the computed scale would be infinite, so a caller cannot ask
    /// for a margin that consumes the whole screen and leaves no room for the
    /// annotation it is trying to show.
    ///
    /// This function applies no zoom limit of its own. A sub-pixel target box
    /// yields an enormous scale, and clamping it is the caller's job --
    /// ui::ViewportController does it immediately afterwards, because the
    /// limits are a ui concern that core cannot see.
    ///
    /// A degenerate axis is skipped rather than fitted: a perfectly horizontal
    /// freehand line has zero height, and fitting that axis would divide by
    /// zero. When both axes are degenerate there is no zoom the box implies, so
    /// the current scale is kept and only the centre moves.
    void fitToRect(const Rect<double>& target, double marginFraction);

    void setActualPixels(int slideWidth, int slideHeight);

    Rect<double> visibleSlideRect() const;

    void screenToSlide(double sx, double sy, double& slideX, double& slideY) const;

    void slideToScreen(double slideX, double slideY, double& sx, double& sy) const;

private:
    double m_centerX;
    double m_centerY;
    double m_scale;
    int m_screenWidth;
    int m_screenHeight;
};

} // namespace slideio::viewer::core
