#pragma once

#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/LevelUnreliableRegistry.h"
#include "slideio/viewer/core/Types.h"

#include <functional>
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

// Version of the SlideIO library this build is linked against. It lives in the
// infrastructure layer so the SlideIO headers stay confined here: the UI needs
// the string for the About dialog, not the library.
std::string slideioLibraryVersion();

// One adapter opens the slide once and serves every reader thread. Since SlideIO
// 2.10 a scene's block reads are safe to call concurrently, so readTile/readBlock
// need no external serialisation; all other state is set in the constructor or
// guarded by m_unreliableLevels, which does its own locking.
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
                              int targetWidth, int targetHeight,
                              int zIndex = 0, int tFrame = 0) override;

    void addOnLevelMarkedUnreliable(std::function<void(int)> listener) override;

private:
    bool isLevelUnreliable(int level) const;
    // Returns true the first time this level is marked. Subsequent calls return false.
    bool markLevelUnreliable(int level);

    std::string m_filePath;
    std::shared_ptr<::slideio::Slide> m_slide;
    std::shared_ptr<::slideio::Scene> m_scene;
    std::vector<core::LevelInfo> m_levels;
    core::SlideInfo m_slideInfo;

    core::LevelUnreliableRegistry m_unreliableLevels;
};

} // namespace slideio::viewer::infra
