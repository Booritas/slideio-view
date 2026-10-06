#pragma once

#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/LevelUnreliableRegistry.h"
#include "slideio/viewer/core/Types.h"

#include <atomic>
#include <cstdint>
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
    // defaultProfileBytes stands in for slides that embed no profile; it is
    // ignored for slides that do. Empty means no default is configured.
    explicit SlideIOAdapter(const std::string& filePath, int sceneIndex = 0,
                             const std::string& driverId = "",
                             std::vector<uint8_t> defaultProfileBytes = {});
    SlideIOAdapter(const std::string& filePath, const std::string& auxImageName,
                   const std::string& driverId = "",
                   std::vector<uint8_t> defaultProfileBytes = {});
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

    void setColorMode(core::ColorMode mode) override;
    core::ColorMode colorMode() const override;
    core::ColorProfileInfo activeColorProfileInfo() const override;

    // Raw bytes of the profile the file embeds, empty when it embeds none.
    std::vector<uint8_t> embeddedProfileBytes() const;

private:
    // Pyramid reliability is tracked per read path, not per slide: see the
    // comment on m_unreliableLevels. Both take the flag readTile computes for
    // the scene it actually selected.
    bool isLevelUnreliable(int level, bool throughManagedScene) const;
    // Returns true the first time this level is marked on this path. Subsequent
    // calls for the same level and path return false.
    bool markLevelUnreliable(int level, bool throughManagedScene);

    core::LevelUnreliableRegistry& unreliableLevelsFor(bool throughManagedScene);
    const core::LevelUnreliableRegistry& unreliableLevelsFor(bool throughManagedScene) const;

    // The scene that serves a given colour mode: the colour-managed scene when
    // Managed is asked for and one was built at open, otherwise the raw scene.
    const std::shared_ptr<::slideio::Scene>& sceneForMode(core::ColorMode mode) const;

    // The scene that serves reads carrying no colour mode of their own
    // (readBlock): whichever mode the adapter is currently set to.
    const std::shared_ptr<::slideio::Scene>& activeScene() const;

    std::string m_filePath;
    std::shared_ptr<::slideio::Slide> m_slide;
    std::shared_ptr<::slideio::Scene> m_scene;
    // Built at open when the slide qualifies; null otherwise. Shares the origin's
    // underlying CVScene and its read serialisation mutex, so this is a second
    // Scene object, not a second reader or file handle.
    std::shared_ptr<::slideio::Scene> m_managedScene;
    std::atomic<core::ColorMode> m_colorMode{core::ColorMode::Raw};
    std::vector<core::LevelInfo> m_levels;
    core::SlideInfo m_slideInfo;

    // One ledger per read path. The two scenes read the same file, but a
    // managed read goes through the transform layer on top of it, so a failure
    // on one path says nothing about the other. A single shared ledger let
    // either path condemn a level for both -- and since the registry is never
    // cleared, a condemned level 0 has no finer level to fall back to and would
    // stay blank for the rest of the session, in both modes.
    core::LevelUnreliableRegistry m_unreliableLevels;
    core::LevelUnreliableRegistry m_managedUnreliableLevels;
};

} // namespace slideio::viewer::infra
