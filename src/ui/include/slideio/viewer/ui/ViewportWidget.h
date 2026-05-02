#pragma once

#include "slideio/viewer/core/Types.h"

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

    void openSlide(const std::string& filePath);
    void openScene(const std::string& filePath, int sceneIndex);
    void openAuxImage(const std::string& filePath, const std::string& auxImageName);
    void generateSceneThumbnails();
    void closeSlide();
    bool isSlideOpen() const;
    const std::string& currentFilePath() const;

    ViewportController* controller() const;

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

signals:
    void viewportChanged();
    void cursorMoved(double slideX, double slideY);
    void slideOpened(const std::string& filePath);
    void slideClosed();
    void thumbnailReady(const QImage& thumbnail);
    void errorOccurred(const std::string& message);
    void sceneThumbnailReady(int sceneIndex, bool isAuxiliary, const std::string& name, const QImage& thumbnail);
    void loadingStarted(const QString& displayName);
    void loadingFinished();

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

private:
    void installSceneOpenResult(uint64_t opId, SceneOpenResult result);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
