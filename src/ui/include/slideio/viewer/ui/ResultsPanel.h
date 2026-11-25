#pragma once

#include "slideio/viewer/core/Types.h"

#include <QDockWidget>

#include <memory>
#include <string>

namespace slideio::viewer::app
{
class ResultsOverlayService;
}

namespace slideio::viewer::ui
{

/// The Results tab (processing-results overlay design §5.5): the attached
/// workspace and slide, then per results layer a visibility box, colour, fill
/// and opacity, object count and stage-state badge; under each, "colour by
/// class" and the legend rows; the layer's in-view or building line; Reload
/// and Close; the selected result's details.
///
/// Reads the service's in-memory state only -- never the store. View settings
/// are written straight to the service with setLayerView, then
/// layerViewChanged() tells MainWindow to save them and repaint. Its words are
/// ResultsPanelText's. No test constructs it.
class ResultsPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit ResultsPanel(QWidget* parent = nullptr);
    ~ResultsPanel() override;

    ResultsPanel(const ResultsPanel&) = delete;
    ResultsPanel& operator=(const ResultsPanel&) = delete;

    /// Non-owning. Null, or a detached service, shows the detached state.
    void setService(app::ResultsOverlayService* service);
    /// The header's workspace folder and the slide and scene it is attached to.
    void setAttachment(const QString& workspacePath, const QString& slidePath, int scene);

    /// Rebuilds every row from the service: after attach, reload, detach and a
    /// colour change. Queued and coalesced, so it is safe from a row's own slot.
    void scheduleRebuild();
    /// Updates the in-view / building lines and the selection details, at most
    /// once per 50 ms: onChanged fires up to a hundred times per scanned layer.
    void scheduleRefresh();

signals:
    void reloadRequested();
    void closeRequested();
    /// A layer view was changed through the service; save it and repaint.
    void layerViewChanged();
    /// The user picked `colour` for the class `className` in a legend.
    void classColourChosen(const std::string& className, slideio::viewer::core::Color colour);

private:
    void rebuild();
    void refresh();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
