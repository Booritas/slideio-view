#pragma once

#include "slideio/viewer/core/ISlideSource.h"

#include <memory>
#include <string>
#include <vector>

namespace slideio
{
class Slide;
class Scene;
} // namespace slideio

namespace slideio::viewer::infra
{

class SlideIOAdapter : public core::ISlideSource
{
public:
    explicit SlideIOAdapter(const std::string& filePath);
    ~SlideIOAdapter() override;

    core::SlideInfo slideInfo() const override;
    std::vector<core::LevelInfo> levels() const override;
    core::TileData readTile(const core::TileKey& key) override;

private:
    std::string m_filePath;
    std::shared_ptr<::slideio::Slide> m_slide;
    std::shared_ptr<::slideio::Scene> m_scene;
    std::vector<core::LevelInfo> m_levels;
    core::SlideInfo m_slideInfo;
};

} // namespace slideio::viewer::infra
