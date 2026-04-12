#pragma once

#include "slideio/viewer/core/Types.h"

#include <QDockWidget>
#include <QImage>
#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

class SceneThumbnailPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit SceneThumbnailPanel(QWidget* parent = nullptr);
    ~SceneThumbnailPanel() override;

    void setScenes(const std::vector<core::SceneInfo>& scenes,
                   const std::vector<core::SceneInfo>& auxImages);
    void setThumbnail(int sceneIndex, bool isAuxiliary, const std::string& name, const QImage& thumbnail);
    void setActiveScene(int sceneIndex, bool isAuxiliary);
    void clear();

signals:
    void sceneSelected(int sceneIndex);
    void auxImageSelected(const std::string& auxImageName);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
