#pragma once

#include <QDockWidget>

#include <memory>
#include <string>

namespace slideio::viewer::app
{
class AnnotationModel;
}

namespace slideio::viewer::ui
{

/// The Layers tab (FR-ANN-23/24, UI design 4.6): one row per layer, top
/// first, with visibility, lock, name, annotation count and opacity; the
/// active layer in bold. New, Delete, Merge into..., Up and Down.
///
/// Structure edits -- create, rename, delete an empty layer, reorder,
/// visibility, lock, opacity -- go straight to AnnotationModel::setLayers and
/// are not undoable: they change no annotation. Merge DOES move annotations,
/// so it is a request MainWindow turns into one undo step.
///
/// The rules (which buttons, which targets, row order) are LayersPanelPolicy's;
/// this class only shows them. No test constructs it.
class LayersPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(QWidget* parent = nullptr);
    ~LayersPanel() override;

    LayersPanel(const LayersPanel&) = delete;
    LayersPanel& operator=(const LayersPanel&) = delete;

    void setAnnotationModel(app::AnnotationModel* model);

    /// False on a read-only scene: everything but the visibility boxes is
    /// disabled -- hiding a layer is a view, and the one thing a reader needs.
    void setEditingAllowed(bool allowed);

signals:
    /// Merge into... was chosen: move every annotation of `sourceId` to
    /// `targetId`, as one undo step.
    void mergeRequested(const std::string& sourceId, const std::string& targetId);

private:
    void rebuild();
    /// The only way a rebuild is asked for: queued and coalesced, because
    /// most requests arrive from inside the tree's own item signals.
    void scheduleRebuild();
    void updateButtons();
    [[nodiscard]] std::string selectedLayerId() const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
