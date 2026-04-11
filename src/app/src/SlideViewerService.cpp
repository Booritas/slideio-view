#include "slideio/viewer/app/SlideViewerService.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/Types.h"

namespace slideio::viewer::app {

struct SlideViewerService::Impl
{
    std::shared_ptr<core::ISlideSource> slideSource;
    std::shared_ptr<core::ITileCache> tileCache;
    core::SlideInfo slideInfo;
    bool isOpen = false;
};

SlideViewerService::SlideViewerService()
    : m_impl(std::make_unique<Impl>())
{
}

SlideViewerService::~SlideViewerService() = default;

bool SlideViewerService::openSlide(const std::string& /*filePath*/)
{
    // Slide opening is handled by the UI layer which creates
    // the adapter pool and cache directly. This service provides
    // a coordination point for future use (e.g., case management).
    return false;
}

void SlideViewerService::closeSlide()
{
    m_impl->slideSource.reset();
    m_impl->tileCache.reset();
    m_impl->slideInfo = {};
    m_impl->isOpen = false;
}

bool SlideViewerService::isSlideOpen() const
{
    return m_impl->isOpen;
}

const core::SlideInfo& SlideViewerService::slideInfo() const
{
    return m_impl->slideInfo;
}

std::shared_ptr<core::ISlideSource> SlideViewerService::slideSource() const
{
    return m_impl->slideSource;
}

std::shared_ptr<core::ITileCache> SlideViewerService::tileCache() const
{
    return m_impl->tileCache;
}

} // namespace slideio::viewer::app
