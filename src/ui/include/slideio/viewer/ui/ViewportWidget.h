#pragma once

#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/ui/AnnotationInteraction.h"
#include "slideio/viewer/ui/ColorProfilePolicy.h"
#include "slideio/viewer/ui/GpuInfo.h"

#include <QImage>
#include <QOpenGLWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

class ViewportController;
struct SceneOpenResult;

class ViewportWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    ViewportWidget(const ViewportWidget&) = delete;
    ViewportWidget& operator=(const ViewportWidget&) = delete;

    // driverId selects a specific SlideIO driver (e.g., "SVS", "CZI"); pass ""
    // to let SlideIO auto-detect from file content.
    void openSlide(const std::string& filePath, const std::string& driverId = "");
    void openScene(const std::string& filePath, int sceneIndex,
                   const std::string& driverId = "");
    void openAuxImage(const std::string& filePath, const std::string& auxImageName,
                      const std::string& driverId = "");
    void generateSceneThumbnails();
    void generateAuxImageThumbnails();

    // Load an associated image (label/macro/preview/…) at its native resolution
    // and return it as a QImage. Returns a null QImage on failure (no slide
    // open, name not found, read error). Synchronous — aux images are typically
    // small (a few thousand pixels per side) so this completes quickly.
    QImage loadAuxImage(const std::string& auxImageName);
    void closeSlide();
    bool isSlideOpen() const;
    const std::string& currentFilePath() const;

    // The open slide's content-derived identity (core::computeSlideId), cached
    // from the background open worker that already computed it -- callers must
    // not recompute it by re-walking the slide's files on the UI thread, which
    // for a DICOM study is a full recursive directory scan. Empty when no
    // slide is open or the open slide could not be identified.
    const std::string& currentSlideId() const;

    ViewportController* controller() const;

    // OpenGL strings captured when the context came up. Every field is empty
    // until initializeGL() has run, and stays empty if it could not get a 3.3
    // core profile.
    GpuInfo glInfo() const;

    void fitToSlide();
    void setActualPixels();
    void zoomIn();
    void zoomOut();
    void panByPixels(double dx, double dy);

    void setChannelSettings(const std::vector<core::ChannelInfo>& channels);
    const core::SlideInfo& slideInfo() const;

    void setZSlice(int zIndex);
    void setTFrame(int tFrame);
    int currentZSlice() const;
    int currentTFrame() const;

    // The profile policy to apply to slides opened from now on: the global
    // default, plus the per-slide overrides by slide id. Snapshotted by
    // MainWindow on the UI thread; the open threads capture it by value.
    void setColorProfilePolicy(ColorProfilePolicy policy);

    // Why a configured override could not be used on the slide now showing.
    // Empty when none was configured or it was fine.
    const std::string& lastColorProfileProblem() const;

    // Reopen whatever is currently displayed, so a profile change takes effect
    // without the user reopening the file by hand. A no-op when nothing is
    // open. Goes through the ordinary open path, so the openOpId guard still
    // decides which of several in-flight opens wins.
    void reopenCurrentScene();

    // Switches which rendition of the open slide is read and displayed.
    // A no-op when no slide is open.
    void setColorMode(core::ColorMode mode);

    /// The tool that owns left-drag. Pan is the default, so navigation behaves
    /// exactly as it did before annotations existed until the user picks a
    /// drawing tool.
    void setActiveTool(AnnotationTool tool);
    [[nodiscard]] AnnotationTool activeTool() const;

    // The colour profile of the scene currently being read. Default-constructed
    // when no slide is open.
    core::ColorProfileInfo activeColorProfileInfo() const;

signals:
    void viewportChanged();
    void cursorMoved(double slideX, double slideY);
    void slideOpened(const std::string& filePath);
    void slideClosed();
    void thumbnailReady(const QImage& thumbnail);
    void errorOccurred(const std::string& message);
    void sceneThumbnailReady(int sceneIndex, bool isAuxiliary, const std::string& name, const QImage& thumbnail);
    void loadingStarted(const QString& displayName);
    void loadingStatusChanged(const QString& text);
    void loadingFinished();
    void activeToolChanged(AnnotationTool tool);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void updateCursor();
    void installSceneOpenResult(uint64_t opId, SceneOpenResult result);

    // Drops the GPU textures of every tile not in `keep`'s colour mode so the
    // other rendition's CPU-cached tiles can be re-uploaded on demand instead
    // of both modes' textures being held at once.
    void releaseTexturesOfOtherMode(core::ColorMode keep);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
