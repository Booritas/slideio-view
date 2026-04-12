#pragma once

#include "slideio/viewer/core/CoordinateSystem.h"
#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/core/Viewport.h"
#include "slideio/viewer/infra/Prefetcher.h"
#include "slideio/viewer/infra/TileLoadScheduler.h"

#include <memory>
#include <vector>

namespace slideio::viewer::ui
{

class ViewportController
{
public:
    ViewportController(std::shared_ptr<core::TilePyramid> pyramid,
                       std::shared_ptr<core::ITileCache> cache,
                       std::shared_ptr<infra::TileLoadScheduler> scheduler);
    ~ViewportController();

    ViewportController(const ViewportController&) = delete;
    ViewportController& operator=(const ViewportController&) = delete;

    void setSlide(const core::SlideInfo& info, const core::TilePyramid& pyramid);
    void setScheduler(std::shared_ptr<infra::TileLoadScheduler> scheduler);

    void resize(int w, int h);

    void pan(double dx, double dy);

    void zoomToPoint(double sx, double sy, double factor);

    void fitToSlide();

    void setActualPixels();

    const core::Viewport& viewport() const;

    std::vector<core::TileKey> visibleTileKeys() const;

    void requestVisibleTiles();

    void screenToSlide(double sx, double sy, double& slideX, double& slideY) const;

    int slideWidth() const;
    int slideHeight() const;
    double baseMagnification() const;

    void setZT(int zIndex, int tFrame);
    int currentZIndex() const;
    int currentTFrame() const;

private:
    std::shared_ptr<core::TilePyramid> m_pyramid;
    std::shared_ptr<core::ITileCache> m_cache;
    std::shared_ptr<infra::TileLoadScheduler> m_scheduler;
    core::Viewport m_viewport;
    std::unique_ptr<core::CoordinateSystem> m_coordSystem;
    int m_slideWidth;
    int m_slideHeight;
    double m_baseMagnification;
    int m_zIndex = 0;
    int m_tFrame = 0;
};

} // namespace slideio::viewer::ui
