#pragma once

#include <memory>
#include <string>
#include <functional>

namespace slideio::viewer::core {
class ISlideSource;
class ITileCache;
struct SlideInfo;
struct LevelInfo;
}

namespace slideio::viewer::app {

/// Application-level service coordinating slide viewing operations.
/// Owns the slide source and cache, and provides a unified API
/// for the presentation layer.
class SlideViewerService
{
public:
    SlideViewerService();
    ~SlideViewerService();

    /// Open a slide from a file path. Returns true on success.
    bool openSlide(const std::string& filePath);

    /// Close the currently open slide.
    void closeSlide();

    /// Whether a slide is currently open.
    bool isSlideOpen() const;

    /// Get info about the currently open slide.
    const core::SlideInfo& slideInfo() const;

    /// Get the slide source for the current slide.
    std::shared_ptr<core::ISlideSource> slideSource() const;

    /// Get the tile cache.
    std::shared_ptr<core::ITileCache> tileCache() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::app
