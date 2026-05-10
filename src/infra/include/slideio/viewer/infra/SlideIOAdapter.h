#pragma once

#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/Types.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
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
    // driverId selects a specific SlideIO driver (e.g., "SVS", "CZI"); pass "" to
    // let SlideIO auto-detect from the file content.
    explicit SlideIOAdapter(const std::string& filePath, int sceneIndex = 0,
                             const std::string& driverId = "");
    SlideIOAdapter(const std::string& filePath, const std::string& auxImageName,
                   const std::string& driverId = "");
    ~SlideIOAdapter() override;

    static std::pair<std::vector<core::SceneInfo>, std::vector<core::SceneInfo>> enumerateScenes(
        const std::string& filePath, const std::string& driverId = "");

    // Returns the list of SlideIO driver IDs available at runtime (e.g.,
    // {"AFI", "CZI", "DCM", "GDAL", "NDPI", "SCN", "SVS", ...}).
    static std::vector<std::string> availableDriverIds();

    core::SlideInfo slideInfo() const override;
    std::vector<core::LevelInfo> levels() const override;
    core::TileData readTile(const core::TileKey& key) override;

    // Read an arbitrary slide region resampled to (targetWidth, targetHeight).
    // SlideIO picks the best pyramid level internally and interpolates as needed.
    // Useful for sharp thumbnails and overview images. Throws on read errors.
    core::TileData readBlock(int slideX, int slideY, int slideWidth, int slideHeight,
                              int targetWidth, int targetHeight) override;

    void setOnLevelMarkedUnreliable(std::function<void(int)> callback) override;

private:
    bool isLevelUnreliable(int level) const;
    // Returns true the first time this level is marked. Subsequent calls return false.
    bool markLevelUnreliable(int level);

    std::string m_filePath;
    int m_sceneIndex = 0;
    std::shared_ptr<::slideio::Slide> m_slide;
    std::shared_ptr<::slideio::Scene> m_scene;
    std::vector<core::LevelInfo> m_levels;
    core::SlideInfo m_slideInfo;

    mutable std::mutex m_unreliableLevelsMutex;
    std::unordered_set<int> m_unreliableLevels;
    std::function<void(int)> m_onLevelMarkedUnreliable;
};

} // namespace slideio::viewer::infra
