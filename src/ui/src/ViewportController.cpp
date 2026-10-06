#include "slideio/viewer/ui/ViewportController.h"

#include <algorithm>
#include <cmath>

namespace slideio::viewer::ui
{

ViewportController::ViewportController(std::shared_ptr<core::TilePyramid> pyramid,
                                       std::shared_ptr<core::ITileCache> cache,
                                       std::shared_ptr<infra::TileLoadScheduler> scheduler)
    : m_pyramid(std::move(pyramid))
    , m_cache(std::move(cache))
    , m_scheduler(std::move(scheduler))
    , m_viewport()
    , m_coordSystem(nullptr)
    , m_slideWidth(0)
    , m_slideHeight(0)
    , m_baseMagnification(0.0)
{
    if (m_pyramid) {
        m_coordSystem = std::make_unique<core::CoordinateSystem>(*m_pyramid);
        m_slideWidth = m_pyramid->slideWidth();
        m_slideHeight = m_pyramid->slideHeight();
    }
}

ViewportController::~ViewportController() = default;

void ViewportController::setScheduler(std::shared_ptr<infra::TileLoadScheduler> scheduler)
{
    m_scheduler = std::move(scheduler);
}

void ViewportController::setSlide(const core::SlideInfo& info, const core::TilePyramid& pyramid)
{
    m_slideWidth = info.width;
    m_slideHeight = info.height;
    m_baseMagnification = info.magnification;

    m_pyramid = std::make_shared<core::TilePyramid>(pyramid);
    m_coordSystem = std::make_unique<core::CoordinateSystem>(*m_pyramid);

    m_viewport.fitToSlide(m_slideWidth, m_slideHeight);

    requestVisibleTiles();
}

void ViewportController::resize(int w, int h)
{
    m_viewport.setScreenSize(w, h);

    if (m_slideWidth > 0 && m_slideHeight > 0) {
        requestVisibleTiles();
    }
}

void ViewportController::pan(double dx, double dy)
{
    m_viewport.pan(dx, dy);
    requestVisibleTiles();
}

void ViewportController::zoomToPoint(double sx, double sy, double factor)
{
    double currentScale = m_viewport.scale();
    double newScale = currentScale * factor;

    static constexpr double kMinScale = 0.001;
    static constexpr double kMaxScale = 100.0;
    newScale = std::max(kMinScale, std::min(kMaxScale, newScale));

    m_viewport.zoomToPoint(sx, sy, newScale);
    requestVisibleTiles();
}

void ViewportController::fitToSlide()
{
    m_viewport.fitToSlide(m_slideWidth, m_slideHeight);
    requestVisibleTiles();
}

void ViewportController::setActualPixels()
{
    m_viewport.setActualPixels(m_slideWidth, m_slideHeight);
    requestVisibleTiles();
}

const core::Viewport& ViewportController::viewport() const
{
    return m_viewport;
}

std::vector<core::TileKey> ViewportController::visibleTileKeys() const
{
    if (!m_coordSystem) {
        return {};
    }
    auto keys = m_coordSystem->visibleTiles(m_viewport);
    // TilePyramid yields geometry only -- level, column, row. The remaining
    // dimensions of a tile's identity are view state, so they are stamped here:
    // Z/T as before, and now the colour mode, so raw and managed tiles of the
    // same region stay distinct in the cache.
    for (auto& key : keys) {
        key = core::TileKey(key.level(), key.column(), key.row(), m_zIndex, m_tFrame, m_colorMode);
    }
    return keys;
}

void ViewportController::requestVisibleTiles()
{
    if (!m_coordSystem || !m_scheduler) {
        return;
    }

    m_scheduler->cancelAll();

    auto visibleKeys = visibleTileKeys();

    std::vector<infra::TileRequest> visibleRequests;
    visibleRequests.reserve(visibleKeys.size());
    for (const auto& key : visibleKeys) {
        if (!m_cache || !m_cache->lookup(key)) {
            visibleRequests.push_back({key, infra::TilePriority::Visible});
        }
    }
    if (!visibleRequests.empty()) {
        m_scheduler->requestTiles(visibleRequests);
    }
}

void ViewportController::screenToSlide(double sx, double sy, double& slideX, double& slideY) const
{
    m_viewport.screenToSlide(sx, sy, slideX, slideY);
}

int ViewportController::slideWidth() const
{
    return m_slideWidth;
}

int ViewportController::slideHeight() const
{
    return m_slideHeight;
}

double ViewportController::baseMagnification() const
{
    return m_baseMagnification;
}

void ViewportController::setZT(int zIndex, int tFrame)
{
    m_zIndex = zIndex;
    m_tFrame = tFrame;
}

int ViewportController::currentZIndex() const
{
    return m_zIndex;
}

int ViewportController::currentTFrame() const
{
    return m_tFrame;
}

void ViewportController::setColorMode(core::ColorMode mode)
{
    m_colorMode = mode;
}

core::ColorMode ViewportController::colorMode() const
{
    return m_colorMode;
}

} // namespace slideio::viewer::ui
