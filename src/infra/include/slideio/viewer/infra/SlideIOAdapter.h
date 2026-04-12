#pragma once

#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/Types.h"

#include <memory>
#include <string>
#include <utility>
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
    explicit SlideIOAdapter(const std::string& filePath, int sceneIndex = 0);
    SlideIOAdapter(const std::string& filePath, const std::string& auxImageName);
    ~SlideIOAdapter() override;

    static std::pair<std::vector<core::SceneInfo>, std::vector<core::SceneInfo>> enumerateScenes(
        const std::string& filePath);

    core::SlideInfo slideInfo() const override;
    std::vector<core::LevelInfo> levels() const override;
    core::TileData readTile(const core::TileKey& key) override;

private:
    std::string m_filePath;
    int m_sceneIndex = 0;
    std::shared_ptr<::slideio::Slide> m_slide;
    std::shared_ptr<::slideio::Scene> m_scene;
    std::vector<core::LevelInfo> m_levels;
    core::SlideInfo m_slideInfo;
};

} // namespace slideio::viewer::infra
