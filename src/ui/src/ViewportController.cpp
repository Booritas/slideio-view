#include "slideio/viewer/ui/ViewportController.h"

#include "slideio/viewer/infra/PerfLog.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <limits>

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
    // Remap keys to include current Z/T indices
    if (m_zIndex != 0 || m_tFrame != 0) {
        for (auto& key : keys) {
            key = core::TileKey(key.level(), key.column(), key.row(), m_zIndex, m_tFrame);
        }
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

    if (infra::perfLogEnabled() && !visibleKeys.empty()) {
        // Slide-coordinate bounding box of the current viewport (clamped to the
        // slide) = the image region that must be loaded to paint the view.
        double cx[4], cy[4];
        m_viewport.screenToSlide(0.0, 0.0, cx[0], cy[0]);
        m_viewport.screenToSlide(static_cast<double>(m_viewport.screenWidth()), 0.0, cx[1], cy[1]);
        m_viewport.screenToSlide(0.0, static_cast<double>(m_viewport.screenHeight()), cx[2], cy[2]);
        m_viewport.screenToSlide(static_cast<double>(m_viewport.screenWidth()),
                                 static_cast<double>(m_viewport.screenHeight()), cx[3], cy[3]);
        double minX = cx[0], maxX = cx[0], minY = cy[0], maxY = cy[0];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, cx[i]); maxX = std::max(maxX, cx[i]);
            minY = std::min(minY, cy[i]); maxY = std::max(maxY, cy[i]);
        }
        minX = std::max(0.0, minX);
        minY = std::max(0.0, minY);
        maxX = std::min(static_cast<double>(m_slideWidth), maxX);
        maxY = std::min(static_cast<double>(m_slideHeight), maxY);

        int level = visibleKeys.front().level();
        double scale = m_pyramid ? m_pyramid->levelInfo(level).scale : 0.0;

        int minCol = std::numeric_limits<int>::max(), maxCol = std::numeric_limits<int>::min();
        int minRow = std::numeric_limits<int>::max(), maxRow = std::numeric_limits<int>::min();
        for (const auto& key : visibleKeys) {
            minCol = std::min(minCol, key.column()); maxCol = std::max(maxCol, key.column());
            minRow = std::min(minRow, key.row()); maxRow = std::max(maxRow, key.row());
        }

        const size_t toLoad = visibleRequests.size();
        const size_t cached = visibleKeys.size() - toLoad;
        infra::perfLog().trace(
            "requestVisibleTiles: slideRect=({:.0f},{:.0f} {:.0f}x{:.0f}) level={} scale={:.4f} "
            "tiles=cols[{}..{}]xrows[{}..{}] visible={} cached={} toLoad={}",
            minX, minY, maxX - minX, maxY - minY, level, scale,
            minCol, maxCol, minRow, maxRow, visibleKeys.size(), cached, toLoad);
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

} // namespace slideio::viewer::ui
