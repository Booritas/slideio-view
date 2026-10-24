#pragma once

#include "slideio/viewer/core/Calibration.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/ui/AnnotationInteraction.h"
#include "slideio/viewer/ui/ColorProfilePolicy.h"
#include "slideio/viewer/ui/GpuInfo.h"

#include <QImage>
#include <QOpenGLWidget>
#include <QPointF>
#include <QRectF>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QPainter;

namespace slideio::viewer::core
{
class Annotation;
}

namespace slideio::viewer::app
{
class AnnotationModel;
class AnnotationPersistenceService;
class UndoRedoStack;
}

namespace slideio::viewer::ui
{

class ViewportController;
struct SceneOpenResult;

/// Whether an open discards the undo history or keeps it.
///
/// Opening a slide empties the annotation model, and the undo stack refers to
/// annotations in it, so the default is to discard. Reopening the same scene --
/// which is what a colour-profile change does -- empties and refills it with
/// the same annotations under the same ids, and a stack thrown away there is
/// thrown away for nothing: the user toggles colour management and silently
/// loses every step they could have undone.
enum class UndoContext
{
    Reset,
    Preserve,
};

class ViewportWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    /// `model` must outlive this widget. MainWindow owns it and deletes this
    /// widget explicitly before releasing it; see ~MainWindow.
    ///
    /// A reference rather than a setter, unlike setPersistenceService and
    /// setUndoStack: this widget works perfectly well without those two and
    /// null-guards both, but it cannot paint, hit-test or draw without a model.
    /// A null-tolerant model pointer would be a dozen early returns describing a
    /// state that must never occur.
    explicit ViewportWidget(app::AnnotationModel& model, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    ViewportWidget(const ViewportWidget&) = delete;
    ViewportWidget& operator=(const ViewportWidget&) = delete;

    // driverId selects a specific SlideIO driver (e.g., "SVS", "CZI"); pass ""
    // to let SlideIO auto-detect from file content.
    void openSlide(const std::string& filePath, const std::string& driverId = "");
    void openScene(const std::string& filePath, int sceneIndex,
                   const std::string& driverId = "",
                   UndoContext undoContext = UndoContext::Reset);
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

    /// True when an associated image (a label or macro thumbnail) is on screen
    /// rather than a real scene. It has no annotations of its own, and its slide
    /// id is the file's, so treating it as a scene would load scene 0's
    /// annotations over it.
    [[nodiscard]] bool isAuxImageShown() const;

    /// The scene this viewport is showing. Half of the annotation key: a
    /// multi-scene file's scenes have one slide id between them.
    [[nodiscard]] int currentSceneIndex() const;

    ViewportController* controller() const;

    // OpenGL strings captured when the context came up. Every field is empty
    // until initializeGL() has run, and stays empty if it could not get a 3.3
    // core profile.
    GpuInfo glInfo() const;

    void fitToSlide();

    /// Brings one annotation into view, centred, with a margin around it. A
    /// no-op when `id` names nothing the model holds -- a list row can outlive
    /// its annotation by one event-loop turn when an undo removes it while the
    /// double-click is already queued.
    void showAnnotation(const std::string& id);

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

    /// Non-owning. MainWindow owns the service; the widget only calls it at the
    /// two points in the slide lifecycle where the ordering matters.
    void setPersistenceService(app::AnnotationPersistenceService* service);

    /// Non-owning. MainWindow owns the stack and clears it on slide change.
    /// Null until set, and the push sites check for that.
    void setUndoStack(app::UndoRedoStack* stack);

    /// Removes the last placed vertex of a live trace and returns true. Returns
    /// false when no trace is in progress, so the caller can fall through to the
    /// undo stack. Ctrl+Z during path drawing steps back one point in every
    /// vector editor, which is the instinct users arrive with; a pending shape is
    /// not an annotation and never reaches the model, so the stack is not
    /// involved.
    bool undoLastPendingVertex();

    /// Commits the text being typed in the inline editor, if it is open -- as
    /// any focus loss does. Undo and redo call it first, so an editor is never
    /// left open over an annotation they removed. Safe to call when closed.
    void commitTextEditing();

    /// The open scene's calibration, which the ruler and angle labels
    /// measure with. Repaints, so a label changes the moment it does.
    void setCalibration(const core::Calibration& calibration);

    /// True while a tool drag, a move drag or a pending trace with the button
    /// held is in progress. Undo and redo are refused during one: the widget
    /// caches movingId and pending-shape state, and an undo that removed the
    /// annotation being dragged would leave those pointing at nothing.
    [[nodiscard]] bool isDragInFlight() const;

    /// True while a multi-click shape is being traced. The Undo action must be
    /// *enabled* for its Ctrl+Z shortcut to fire at all -- a disabled QAction is
    /// removed from Qt's shortcut map -- so enabling it cannot depend on the
    /// undo stack alone, or Ctrl+Z would do nothing mid-trace whenever the stack
    /// happens to be empty.
    [[nodiscard]] bool hasLiveTrace() const;

    /// True while an open that keeps the undo history is in flight -- from the
    /// start of the reopen until its annotations are back in the model. The
    /// model empties and refills during that window, and the listener that
    /// clears the undo stack on those signals has to tell this apart from a
    /// genuine slide change.
    [[nodiscard]] bool isPreservingUndoContext() const;

    // The colour profile of the scene currently being read. Default-constructed
    // when no slide is open.
    core::ColorProfileInfo activeColorProfileInfo() const;

    /// Removes `id` and records the removal -- the one delete, for the Delete
    /// key and the annotation list alike, so the layer lock is refused on
    /// both. It performs the mutation as well, which is why it must not be
    /// gated on the undo stack: with no stack the annotation still goes, just
    /// unrecorded. Gating it would make the Delete key a silent no-op.
    void deleteAnnotation(const std::string& id);

signals:
    void viewportChanged();
    void cursorMoved(double slideX, double slideY);
    void slideOpened(const std::string& filePath);
    /// The first statement of closeSlide(), ahead of the persistence flush and
    /// of everything it tears down. The model is still whole here, which is the
    /// point: this is the last moment an editor can push a half-typed field
    /// into it and have that edit reach the file. Every path that empties the
    /// model -- a new slide, a scene switch, a colour-profile reopen, the Close
    /// action, quitting -- goes through closeSlide(), so a listener placed here
    /// covers all of them, including ones written later.
    void aboutToCloseSlide();
    void slideClosed();
    void thumbnailReady(const QImage& thumbnail);
    void errorOccurred(const std::string& message);
    void sceneThumbnailReady(int sceneIndex, bool isAuxiliary, const std::string& name, const QImage& thumbnail);
    void loadingStarted(const QString& displayName);
    void loadingStatusChanged(const QString& text);
    void loadingFinished();
    void activeToolChanged(AnnotationTool tool);
    /// A multi-click trace started or ended. Drives the Undo action's enabled
    /// state, which is what makes Ctrl+Z reach the trace at all.
    void liveTraceChanged(bool active);

    /// Whether annotations can be created on what is currently displayed.
    /// `reason` is empty when they can, and names the obstacle when they cannot.
    void annotationsAvailableChanged(bool available, const QString& reason);

    /// A move or delete was refused because the annotations are open read-only.
    /// Transient feedback only; `reason` is ready to show.
    void annotationEditBlocked(const QString& reason);

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
    /// Escape in the text editor, which QLineEdit does not report.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateCursor();

    /// The annotation under a screen point: core's geometric hit test, and the
    /// plates of text annotations, whose extent is in screen pixels; the
    /// topmost of the two wins (ui::topmostOf). Every selection, move and
    /// double-click asks this rather than the model directly.
    [[nodiscard]] std::string annotationAt(QPointF screenPos) const;

    /// A text annotation's plate on screen -- the one rectangle the painter
    /// fills and annotationAt tests. Empty for anything that is not a text.
    [[nodiscard]] QRectF textPlateFor(const core::Annotation& annotation) const;

    /// Why the annotation `id` may not change: "Layer <name> is locked", or
    /// empty. Every edit entry point asks it beside annotationsEditable().
    [[nodiscard]] QString lockRefusal(const std::string& id) const;
    /// Why nothing may be created right now: the active layer is hidden or
    /// locked. Empty when it is open.
    [[nodiscard]] QString activeLayerRefusal() const;

    /// Opens the inline editor at `anchor`: for a new text when `id` is empty,
    /// over that text otherwise. Commits any editor already open first.
    void openTextEditor(core::PointF anchor, const std::string& id, const QString& words);

    /// Closes the inline editor, creating or changing the text when `commit`
    /// and the trimmed words are not empty. One undo step either way.
    void finishTextEditor(bool commit);

    /// Moves the editor to follow its anchor after a pan or zoom.
    void positionTextEditor();
    void paintAnnotations(QPainter& painter);
    void resetAnnotationState();
    void commitPendingShape();
    void discardPendingShape();
    void endToolDrag();
    /// Shared by Backspace and by Ctrl+Z during a live trace.
    void removeLastPendingVertex();

    /// Turns the in-flight single-drag shape into an annotation, selects it and
    /// records the undo step. False when the drag was too small to be anything
    /// but a misclick, in which case nothing is created.
    ///
    /// Called from mouseReleaseEvent and from focusOutEvent, which is what
    /// makes a drag interrupted by focus loss survive rather than vanish.
    /// Reads drawAnchorSlide, drawCurrentSlide and drawModifiers, so the caller
    /// updates those first if it has fresher values.
    bool commitDrawDrag();

    /// Leaves vertex-editing mode and repaints. Safe to call when not editing.
    /// Records a live vertex drag first — see recordVertexDrag.
    void endVertexEditing();

    /// Pushes the undo step for a live vertex drag, if it changed anything,
    /// and clears the drag state. Safe to call with no drag live.
    ///
    /// Every route out of a vertex drag goes through this: the ordinary
    /// release, focus loss, and leaving the mode. Each one leaves the moved
    /// geometry already committed to the model by the per-frame update, so a
    /// route that forgets to record produces an edit that is saved with no
    /// undo step — and the user's next Ctrl+Z then undoes something unrelated.
    /// Reads the edited polygon through vertexEditId, so callers that clear
    /// that must call this BEFORE they do.
    void recordVertexDrag();

    /// Pushes the undo step for a live box resize, if it changed anything, and
    /// clears the drag state. Safe to call with no resize live.
    ///
    /// Every route out of a resize goes through this: the ordinary release,
    /// focus loss, Escape, an undo taken mid-drag, a tool change and a slide
    /// close. Each leaves the resized geometry already committed to the model
    /// by the per-frame update, so a route that forgets to record produces an
    /// edit that is saved with no undo step -- and the user's next Ctrl+Z then
    /// undoes something unrelated. Design spec 6.2.
    void recordBoxResize();

    /// Records one completed geometry edit: the annotation as it was, against
    /// what it is now. One per gesture, never one per mouse-move -- a single
    /// nudge would otherwise bury fifty steps of history and Ctrl+Z would
    /// appear not to work.
    ///
    /// Unlike pushMoveCommand this does NOT consult ui::moveEdit, which decides
    /// "did anything change" by comparing bounding boxes: exact for a
    /// translation, blind to an interior vertex moving inside an unchanged
    /// extent. The caller makes that decision with core::sameVertices.
    void pushGeometryCommand(const std::string& id, const core::Annotation& before,
                             const QString& description);

    /// Record a completed operation on the undo stack. No-ops when no stack is
    /// set or the annotation is not in the model. Called after the mutation has
    /// already gone through AnnotationModel's own API.
    void pushCreateCommand(const std::string& id);
    /// Records one completed move drag as a single step. Called on release, not
    /// from the per-frame setGeometry that drives the live preview.
    void pushMoveCommand(const std::string& id, const std::optional<core::Annotation>& original);
    void installSceneOpenResult(uint64_t opId, SceneOpenResult result);

    // Drops the GPU textures of every tile not in `keep`'s colour mode so the
    // other rendition's CPU-cached tiles can be re-uploaded on demand instead
    // of both modes' textures being held at once.
    void releaseTexturesOfOtherMode(core::ColorMode keep);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
