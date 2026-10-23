#include "slideio/viewer/ui/MainWindow.h"
#include "slideio/viewer/app/AnnotationEditing.h"
#include "slideio/viewer/app/AnnotationImport.h"
#include "slideio/viewer/app/AnnotationImportCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"
#include "slideio/viewer/app/AnnotationRetagCommand.h"
#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/app/WorkspaceSwitch.h"
#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeoJson.h"
#include "slideio/viewer/core/ColorManagement.h"
#include "slideio/viewer/core/SlideId.h"
#include "slideio/viewer/infra/JsonAnnotationRepository.h"
#include "slideio/viewer/infra/QSettingsAnnotationCategoryStore.h"
#include "slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"
#include "slideio/viewer/ui/AboutDialog.h"
#include "slideio/viewer/ui/AnnotationCountExport.h"
#include "slideio/viewer/ui/AnnotationGeoJsonImport.h"
#include "slideio/viewer/ui/AnnotationInteraction.h"
#include "slideio/viewer/ui/AnnotationListPanel.h"
#include "slideio/viewer/ui/AnnotationListPolicy.h"
#include "slideio/viewer/ui/AnnotationMeasurementExport.h"
#include "slideio/viewer/ui/AnnotationPropertiesPanel.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"
#include "slideio/viewer/ui/AnnotationSettings.h"
#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/AssociatedImageWindow.h"
#include "slideio/viewer/ui/ChannelMixerPanel.h"
#include "slideio/viewer/ui/ColorProfilePolicy.h"
#include "slideio/viewer/ui/DriverFilters.h"
#include "slideio/viewer/ui/LoadingOverlay.h"
#include "slideio/viewer/ui/PathDisplay.h"
#include "slideio/viewer/ui/RecentFiles.h"
#include "slideio/viewer/ui/SceneThumbnailPanel.h"
#include "slideio/viewer/ui/ShortcutPolicy.h"
#include "slideio/viewer/ui/SlidePropertiesPanel.h"
#include "slideio/viewer/ui/SlideProfilesDialog.h"
#include "slideio/viewer/ui/WorkspaceChangeDialog.h"
#include "slideio/viewer/ui/MetadataPanel.h"
#include "slideio/viewer/ui/MinimapWidget.h"
#include "slideio/viewer/ui/PreferencesDialog.h"
#include "slideio/viewer/ui/StatusBarManager.h"
#include "slideio/viewer/ui/ViewportController.h"
#include "slideio/viewer/ui/ViewportWidget.h"
#include "slideio/viewer/ui/ZTNavigationWidget.h"
#include "slideio/viewer/ui/ZoomIndicatorWidget.h"

#include <spdlog/spdlog.h>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegion>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr int kMaxRecentFiles = 10;
const QString kRecentFilesKey = "recentFiles";

// The window title names the slide that is open; with none open it names the
// application alone. Held in one place because three sites say it.
const QString kApplicationTitle = QStringLiteral("SlideIO Viewer");

constexpr auto kDefaultProfileKey = "color/defaultSourceProfile";
constexpr auto kColorManagementKey = "view/colorManagement";

// What these two actions do, held in one place because the slide-opened handler
// restores them after a problem has temporarily displaced them. std::string
// rather than a literal so they can stand in a conditional beside the
// problem text they alternate with.
const std::string kColorManagementDescription =
    "Convert slide colours to sRGB via its ICC profile";
const std::string kSetSlideProfileDescription =
    "Choose an RGB ICC profile for the slide on screen";

/// Gives Undo/Redo to the focused text field, if the focus is in one.
///
/// The Edit menu's actions carry Qt::WindowShortcut, and Qt matches a shortcut
/// before the key event reaches the focus widget. Without this, Ctrl+Z typed in
/// the metadata filter or a channel's min/max box would undo the last
/// annotation edit instead of the typing -- an invisible change to the slide's
/// saved marks, made by a keystroke aimed at a text box, which autosave then
/// writes to disk.
///
/// True whenever the focus was in an editor, including when that editor had
/// nothing left to undo: the point is that the keystroke belongs to the text
/// field, not that it accomplished something there.
bool routeEditShortcutToFocusWidget(const QWidget* window, bool redo)
{
    QWidget* focus = window == nullptr ? nullptr : window->focusWidget();
    if (focus == nullptr) {
        return false;
    }

    // A spin box's editable area is a QLineEdit child, and focusWidget() names
    // that child, so spin boxes are covered by the first case.
    if (auto* lineEdit = qobject_cast<QLineEdit*>(focus)) {
        redo ? lineEdit->redo() : lineEdit->undo();
        return true;
    }
    if (auto* plainEdit = qobject_cast<QPlainTextEdit*>(focus)) {
        redo ? plainEdit->redo() : plainEdit->undo();
        return true;
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(focus)) {
        redo ? textEdit->redo() : textEdit->undo();
        return true;
    }
    return false;
}

/// Keeps the window answering while `work` runs on another thread.
///
/// An event loop polls for the result, so the window repaints and the OS never
/// marks it as not responding. A busy dialog with Cancel appears only after a
/// moment, so a file that reads quickly never flashes one; it is window-modal,
/// so nothing the user does can change what the worker was handed. Cancel only
/// raises `cancel` -- this still waits for the worker, which polls the flag
/// and returns promptly, because a worker abandoned while it reads a reference
/// to this frame would outlive what it reads.
template<typename T>
void waitWithCancel(QWidget* owner, std::future<T>& work, const QString& label, const QString& title,
                    std::atomic<bool>& cancel)
{
    const auto done = [&work]() { return work.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    if (done()) {
        return;
    }
    QGuiApplication::setOverrideCursor(Qt::BusyCursor);
    QProgressDialog progress(label, QObject::tr("Cancel"), 0, 0, owner);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoReset(false);
    progress.setAutoClose(false);
    // Shown by the poll below, not by QProgressDialog's own timer: with no
    // value ever set, that timer keeps its four-second default whatever
    // setMinimumDuration says, and a read of six seconds showed nothing for
    // four of them.
    progress.setMinimumDuration(std::numeric_limits<int>::max());
    constexpr qint64 kShowAfterMilliseconds = 400;
    QElapsedTimer elapsed;
    elapsed.start();

    QEventLoop loop;
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&cancel, &progress]() {
        cancel.store(true, std::memory_order_relaxed);
        progress.setLabelText(QObject::tr("Cancelling..."));
    });
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done()) {
            loop.quit();
        } else if (!progress.isVisible() && !cancel.load(std::memory_order_relaxed)
                   && elapsed.elapsed() >= kShowAfterMilliseconds) {
            progress.show();
        }
    });
    poll.start();
    loop.exec();
    QGuiApplication::restoreOverrideCursor();
}
} // anonymous namespace

namespace slideio::viewer::ui
{

namespace app = slideio::viewer::app;
namespace core = slideio::viewer::core;
namespace infra = slideio::viewer::infra;

struct MainWindow::Impl
{
    MainWindow* owner = nullptr;
    ViewportWidget* viewportWidget = nullptr;
    MinimapWidget* minimapWidget = nullptr;
    ZoomIndicatorWidget* zoomIndicatorWidget = nullptr;
    StatusBarManager* statusBarManager = nullptr;
    ZTNavigationWidget* ztNavigationWidget = nullptr;
    ChannelMixerPanel* channelMixerPanel = nullptr;
    SceneThumbnailPanel* sceneThumbnailPanel = nullptr;
    SceneThumbnailPanel* associatedImagesPanel = nullptr;
    SlidePropertiesPanel* propertiesPanel = nullptr;
    MetadataPanel* metadataPanel = nullptr;
    AnnotationListPanel* annotationListPanel = nullptr;
    AnnotationPropertiesPanel* annotationPropertiesPanel = nullptr;
    LoadingOverlay* loadingOverlay = nullptr;

    // File path of the most recently opened slide. Used to detect intra-slide
    // scene/aux switches so the scene/associated-image thumbnail panels are
    // not rebuilt (and their thumbnails not re-fetched) on every click.
    std::string lastOpenedFilePath;

    // Recent files
    QMenu* recentFilesMenu = nullptr;
    QList<QAction*> recentFileActions;

    // Actions
    QAction* openAction = nullptr;
    QAction* openFolderAction = nullptr;  // null when SlideIO has no DICOM driver
    QAction* closeAction = nullptr;
    QAction* openLogAction = nullptr;
    QAction* exitAction = nullptr;
    QAction* zoomInAction = nullptr;
    QAction* zoomOutAction = nullptr;
    QAction* fitAction = nullptr;
    QAction* actualPixelsAction = nullptr;
    QAction* fullScreenAction = nullptr;
    QAction* minimapToggleAction = nullptr;
    QAction* channelMixerToggleAction = nullptr;
    QAction* sceneThumbnailToggleAction = nullptr;
    QAction* associatedImagesToggleAction = nullptr;
    QAction* propertiesToggleAction = nullptr;
    QAction* metadataToggleAction = nullptr;
    QAction* annotationListToggleAction = nullptr;
    QAction* annotationPropertiesToggleAction = nullptr;
    QAction* colorManagementAction = nullptr;
    QAction* setDefaultProfileAction = nullptr;
    QAction* clearDefaultProfileAction = nullptr;
    QAction* setSlideProfileAction = nullptr;
    QAction* clearSlideProfileAction = nullptr;
    QAction* manageSlideProfilesAction = nullptr;
    QAction* panToolAction = nullptr;
    QAction* rectangleToolAction = nullptr;
    QAction* ellipseToolAction = nullptr;
    QAction* polygonToolAction = nullptr;
    QAction* freehandToolAction = nullptr;
    QAction* pointToolAction = nullptr;
    QAction* arrowToolAction = nullptr;
    QAction* preferencesAction = nullptr;
    QAction* openAnnotationsFolderAction = nullptr;
    QAction* importAnnotationsAction = nullptr;
    QAction* exportCountsAction = nullptr;
    QAction* exportMeasurementsAction = nullptr;
    QAction* exportAnnotationsAction = nullptr;
    QAction* aboutAction = nullptr;

    // Why the configured default ICC profile cannot be used, if it cannot.
    // Empty when none is configured or it is fine. Shown instead of the
    // slide-centric reason, which would otherwise blame the slide.
    std::string defaultProfileProblem;

    // The validated default profile bytes, cached so applyColorProfilePolicy
    // can rebuild the whole policy without re-reading the file.
    std::vector<uint8_t> defaultProfileBytes;

    // Slide the user has just nominated an ICC override for, awaiting the
    // reopen that will say whether the nominated profile actually binds.
    // Empty when nothing is pending. Carries the slide id rather than a bare
    // flag so an open that beats the reopen -- the user picking a profile and
    // then opening a different slide -- cannot turn colour management on for
    // the wrong slide. Consumed by the slide-opened handler, which clears it
    // on the first open it sees, matched or not.
    std::string pendingColorManagementEnableSlideId;

    std::unique_ptr<infra::QSettingsColorProfileOverrideStore> overrideStore;
    std::unique_ptr<infra::QSettingsAnnotationCategoryStore> categoryStore;

    /// Owned here, not by the viewport. The viewport renders it, the panels
    /// list and edit it, and persistence saves it -- four consumers across two
    /// layers, so the application layer's state belongs to the window that
    /// coordinates them rather than to one of the things that draws it.
    ///
    /// Declared ahead of the persistence stack below so it is destroyed after
    /// it: the service holds an AnnotationModel& and the undo stack's commands
    /// hold one each, and members die in reverse declaration order.
    std::unique_ptr<app::AnnotationModel> annotations;

    // The annotation persistence stack. Built once, in
    // buildAnnotationPersistence(), and never rebuilt: the service holds a
    // core::IAnnotationRepository& bound at construction, so replacing the
    // repository when the user picks another workspace would leave that
    // reference dangling. A workspace change re-roots the repository instead.
    std::unique_ptr<AnnotationSettings> annotationSettings;
    std::unique_ptr<infra::JsonAnnotationRepository> annotationRepository;
    std::unique_ptr<app::AnnotationPersistenceService> annotationPersistence;
    /// Owns what happens to the open slide's annotations when the workspace
    /// folder changes. Built beside the service, because it drives it.
    std::unique_ptr<app::WorkspaceSwitch> workspaceSwitch;
    std::unique_ptr<app::UndoRedoStack> undoStack;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;

    // At most one save-failure dialog per run of bad luck. A failed save stays
    // dirty and is retried on the autosave debounce, so without this latch an
    // unwritable workspace puts a modal on screen every two seconds.
    /// True between loadingStarted and loadingFinished. A slide open is async and
    /// its completion is delivered on a queued connection, which runs inside any
    /// modal dialog's nested event loop -- so an open can finish while the
    /// workspace-change dialog is up and swap the model out from under a switch
    /// that already captured the old slide's key.
    bool slideLoadInFlight = false;
    bool saveFailureReported = false;

    void buildAnnotationPersistence();
    void updateUndoActions();
    bool openPreferences();
    /// Enables or disables everything that writes annotations, and says why not.
    /// Driven from two signals: ViewportWidget::annotationsAvailableChanged,
    /// which carries the better wording on an open or close, and
    /// AnnotationPersistenceService::activeChanged, which is the only one that
    /// fires on a workspace switch.
    void applyAnnotationAvailability(bool available, const QString& reason);
    /// Says what the workspace change did to the annotations. A merge that
    /// silently changes a set of clinical findings is not acceptable; the count
    /// is the receipt.
    void reportWorkspaceSwitch(const app::WorkspaceSwitchResult& result);

    /// Writes the counting tally to a CSV file the user picks. Builds its rows
    /// with the same categoryTally() call the strip uses, on the same two
    /// inputs, so the file cannot disagree with what was on screen.
    void exportCounts();
    /// One row per annotation from core::measure(), the panel's own function;
    /// see the measurements export design spec.
    void exportMeasurements();
    void exportAnnotations();
    /// False, after warning, when the model is empty for a reason other than
    /// an empty scene; see AnnotationPersistenceService::annotationsLoaded().
    bool annotationsReadyToExport(const QString& title);
    /// The one save-dialog-and-write every export uses: offers `suggestedName`
    /// in the annotation workspace, writes `contents` whole, and reports a
    /// failure with `failureText`, whose %1 is the path and %2 the reason.
    /// Returns the chosen path, or empty when the user cancelled or the write
    /// failed -- a failure has already been reported, a cancel says nothing.
    QString saveExportFile(const QString& title, const QString& suggestedName, const QString& filter,
                           const std::string& contents, const QString& failureText);
    /// Reads a GeoJSON file, shows what it would add, and on confirmation adds
    /// it as one undoable step. See the GeoJSON import design spec.
    void importAnnotations();

    void createActions()
    {
        openAction = new QAction("&Open Slide...", owner);
        openAction->setShortcut(QKeySequence("Ctrl+O"));
        openAction->setStatusTip("Open a whole-slide image file");

        // A DICOM study is a directory of per-frame files, not a single file,
        // so it needs a folder picker. DCM is the only SlideIO driver that
        // takes a directory; without it the action would have nothing to open.
        if (isDicomDriverAvailable()) {
            openFolderAction = new QAction("Open DICOM &Folder...", owner);
            openFolderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
            openFolderAction->setStatusTip("Open a folder containing a DICOM study");
        }

        closeAction = new QAction("&Close", owner);
        closeAction->setShortcut(QKeySequence("Ctrl+W"));
        closeAction->setStatusTip("Close the current slide");
        closeAction->setEnabled(false);

        openLogAction = new QAction("Open &Log File", owner);
        openLogAction->setStatusTip("Open the application log file in the system default text editor");

        exitAction = new QAction("E&xit", owner);
        exitAction->setShortcut(QKeySequence("Alt+F4"));
        exitAction->setStatusTip("Exit the application");

        zoomInAction = new QAction("Zoom &In", owner);
        zoomInAction->setShortcut(QKeySequence("Ctrl++"));
        zoomInAction->setStatusTip("Zoom in");

        zoomOutAction = new QAction("Zoom &Out", owner);
        zoomOutAction->setShortcut(QKeySequence("Ctrl+-"));
        zoomOutAction->setStatusTip("Zoom out");

        fitAction = new QAction("&Fit to Window", owner);
        fitAction->setShortcut(QKeySequence("Ctrl+0"));
        fitAction->setStatusTip("Fit the slide to the window");

        actualPixelsAction = new QAction("&Actual Pixels", owner);
        actualPixelsAction->setShortcut(QKeySequence("Ctrl+1"));
        actualPixelsAction->setStatusTip("View at 1:1 pixel mapping");

        fullScreenAction = new QAction("Full &Screen", owner);
        fullScreenAction->setShortcut(QKeySequence("F11"));
        fullScreenAction->setCheckable(true);
        fullScreenAction->setStatusTip("Toggle full screen mode");

        minimapToggleAction = new QAction("&Minimap", owner);
        minimapToggleAction->setShortcut(QKeySequence("Ctrl+M"));
        minimapToggleAction->setCheckable(true);
        minimapToggleAction->setChecked(true);
        minimapToggleAction->setStatusTip("Toggle the minimap overlay");

        colorManagementAction = new QAction("Color Management", owner);
        colorManagementAction->setCheckable(true);
        colorManagementAction->setShortcut(QKeySequence("Ctrl+Shift+I"));
        colorManagementAction->setEnabled(false);

        setDefaultProfileAction = new QAction("Set Default ICC Profile…", owner);
        setDefaultProfileAction->setStatusTip(
            "Choose an RGB ICC profile to assume for slides that embed none");

        clearDefaultProfileAction = new QAction("Clear Default ICC Profile", owner);
        clearDefaultProfileAction->setStatusTip("Stop assuming a default ICC profile");
        clearDefaultProfileAction->setEnabled(false);

        setSlideProfileAction = new QAction("Set ICC Profile for This Slide…", owner);
        setSlideProfileAction->setStatusTip(QString::fromStdString(kSetSlideProfileDescription));
        setSlideProfileAction->setEnabled(false);

        clearSlideProfileAction = new QAction("Clear ICC Profile for This Slide", owner);
        clearSlideProfileAction->setStatusTip(
            "Stop overriding this slide's color profile");
        clearSlideProfileAction->setEnabled(false);

        manageSlideProfilesAction = new QAction("Manage Slide ICC Profiles…", owner);
        manageSlideProfilesAction->setStatusTip(
            "See and remove stored per-slide color profile overrides");

        panToolAction = new QAction("&Navigate", owner);
        panToolAction->setCheckable(true);
        panToolAction->setChecked(true);
        panToolAction->setShortcut(QKeySequence("V"));
        // Names the vertex-editing gestures, which is the mitigation the
        // design spec records for Ctrl+click being undiscoverable: nothing on
        // screen would otherwise tell a user the mode exists or how to remove
        // a point.
        panToolAction->setStatusTip(
            "Pan and select; double-click a polygon or freehand line to edit its points "
            "(drag a handle to move, click the outline to add, Ctrl+click a handle to remove)");

        rectangleToolAction = new QAction("&Rectangle", owner);
        rectangleToolAction->setCheckable(true);
        rectangleToolAction->setShortcut(QKeySequence("R"));
        rectangleToolAction->setStatusTip(
            "Draw a rectangular annotation by dragging; hold Shift for a square, "
            "Alt to draw from the center");

        ellipseToolAction = new QAction("&Ellipse", owner);
        ellipseToolAction->setCheckable(true);
        ellipseToolAction->setShortcut(QKeySequence("E"));
        ellipseToolAction->setStatusTip(
            "Draw an elliptical annotation by dragging; hold Shift for a circle, "
            "Alt to draw from the center");

        polygonToolAction = new QAction("P&olygon", owner);
        polygonToolAction->setCheckable(true);
        polygonToolAction->setShortcut(QKeySequence("P"));
        polygonToolAction->setStatusTip("Draw a polygon by clicking points, or by dragging to trace");

        freehandToolAction = new QAction("&Freehand Line", owner);
        freehandToolAction->setCheckable(true);
        freehandToolAction->setShortcut(QKeySequence("F"));
        freehandToolAction->setStatusTip("Draw an open freehand line");

        // "M" for marker: "P" belongs to Polygon, whose own mnemonic is the
        // "o". These carry the same WidgetWithChildrenShortcut scope as the
        // five above, for the same reason -- at window scope "m" and "a" would
        // be stolen from every text field in the window.
        pointToolAction = new QAction("Point &Marker", owner);
        pointToolAction->setCheckable(true);
        pointToolAction->setShortcut(QKeySequence("M"));
        pointToolAction->setStatusTip(
            "Place a point marker with a single click; the tool stays active for the next one");

        arrowToolAction = new QAction("&Arrow", owner);
        arrowToolAction->setCheckable(true);
        arrowToolAction->setShortcut(QKeySequence("A"));
        arrowToolAction->setStatusTip("Draw an arrow by dragging from its tail to its head");

        // These are the only bare-letter shortcuts in the application; every
        // other one here is modifier-qualified. Qt matches shortcuts before the
        // key reaches the focus widget, so at the default window scope "v" and
        // "r" would be stolen from every text field in the window -- typing
        // "regression" in the Metadata filter would switch the viewport to the
        // Rectangle tool. Scoping them to the viewport (which they are added to
        // in connectSignals) keeps them working where they mean something and
        // out of the way everywhere else.
        panToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        rectangleToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        ellipseToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        polygonToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        freehandToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        pointToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        arrowToolAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);

        auto* toolGroup = new QActionGroup(owner);
        toolGroup->setExclusive(true);
        toolGroup->addAction(panToolAction);
        toolGroup->addAction(rectangleToolAction);
        toolGroup->addAction(ellipseToolAction);
        toolGroup->addAction(polygonToolAction);
        toolGroup->addAction(freehandToolAction);
        toolGroup->addAction(pointToolAction);
        toolGroup->addAction(arrowToolAction);
        // Spec §6: no drawing tools without a slide. Enabled and disabled from
        // ViewportWidget::annotationsAvailableChanged, which says more than
        // "a slide is open": an associated image and a slide whose annotation
        // file would not parse both leave it off.
        rectangleToolAction->setEnabled(false);
        ellipseToolAction->setEnabled(false);
        polygonToolAction->setEnabled(false);
        freehandToolAction->setEnabled(false);
        pointToolAction->setEnabled(false);
        arrowToolAction->setEnabled(false);

        importAnnotationsAction = new QAction("&Import Annotations from GeoJSON...", owner);
        importAnnotationsAction->setStatusTip(
            "Add the annotations in a QuPath or SlideIO GeoJSON file to this scene");
        // The one menu action that creates annotations, so it is gated where
        // creation is -- in applyAnnotationAvailability beside the drawing
        // tools, and nowhere else. The exports below are the opposite.
        importAnnotationsAction->setEnabled(false);

        exportCountsAction = new QAction("&Export Counts...", owner);
        exportCountsAction->setStatusTip(
            "Write this scene's category counts to a CSV file");
        // Enabled by a slide being open, NOT by annotations being editable: a
        // read-only slide's counts are exactly what a reader opening one wants,
        // which is the same reasoning that keeps the tally visible there while
        // the counting combo is disabled. It is therefore absent from
        // applyAnnotationAvailability and paired with closeAction instead.
        exportCountsAction->setEnabled(false);

        exportMeasurementsAction = new QAction("Export &Measurements...", owner);
        exportMeasurementsAction->setStatusTip(
            "Write each annotation's area, perimeter or length in this scene to a CSV file");
        // Gated exactly as Export Counts is, for the same reason, and every
        // exportCountsAction->setEnabled has a twin for this one.
        exportMeasurementsAction->setEnabled(false);

        // G, not A: Open Annotations Folder already answers to Alt+A here.
        exportAnnotationsAction = new QAction("Export Annotations as &GeoJSON...", owner);
        exportAnnotationsAction->setStatusTip(
            "Write this scene's annotations to a GeoJSON file QuPath can import");
        // Gated exactly as Export Counts is, for the same reason: a read-only
        // slide's annotations are exactly what a reader wants to take elsewhere.
        // Every exportCountsAction->setEnabled has a twin for this one.
        exportAnnotationsAction->setEnabled(false);

        openAnnotationsFolderAction = new QAction("Open &Annotations Folder", owner);
        openAnnotationsFolderAction->setStatusTip(
            "Show the folder where annotation files are saved");

        preferencesAction = new QAction("&Preferences...", owner);
        // On macOS this moves the entry into the application menu, as the
        // platform expects, instead of leaving it under Tools.
        preferencesAction->setMenuRole(QAction::PreferencesRole);
        preferencesAction->setStatusTip("Choose where annotations are saved and who they name");

        aboutAction = new QAction("&About SlideIO Viewer...", owner);
        // On macOS this moves the entry into the application menu, where the
        // platform expects it, instead of leaving it under Help.
        aboutAction->setMenuRole(QAction::AboutRole);
        aboutAction->setStatusTip("Show version, system and licence information");
    }

    void createMenus()
    {
        QMenu* fileMenu = owner->menuBar()->addMenu("&File");
        fileMenu->addAction(openAction);
        if (openFolderAction) {
            fileMenu->addAction(openFolderAction);
        }

        recentFilesMenu = fileMenu->addMenu("Recent &Files");
        updateRecentFilesMenu();

        fileMenu->addAction(closeAction);
        fileMenu->addSeparator();
        fileMenu->addAction(importAnnotationsAction);
        fileMenu->addAction(exportCountsAction);
        fileMenu->addAction(exportMeasurementsAction);
        fileMenu->addAction(exportAnnotationsAction);
        fileMenu->addAction(openAnnotationsFolderAction);
        fileMenu->addAction(openLogAction);
        fileMenu->addSeparator();
        fileMenu->addAction(exitAction);

        // Between File and View, where every application that has one puts it.
        QMenu* editMenu = owner->menuBar()->addMenu("&Edit");
        undoAction = new QAction("&Undo", owner);
        // The standard key for the platform rather than a hard-coded Ctrl+Z.
        undoAction->setShortcut(QKeySequence::Undo);
        undoAction->setEnabled(false);
        editMenu->addAction(undoAction);

        redoAction = new QAction("&Redo", owner);
        // Ctrl+Shift+Z alongside the platform standard: users arrive with both.
        // Built rather than listed, because on macOS and X11 Ctrl+Shift+Z *is*
        // the platform standard, and registering one sequence twice makes Qt
        // call the match ambiguous and refuse to trigger it at all.
        redoAction->setShortcuts(redoShortcuts(QKeySequence::keyBindings(QKeySequence::Redo)));
        redoAction->setEnabled(false);
        editMenu->addAction(redoAction);

        QMenu* viewMenu = owner->menuBar()->addMenu("&View");
        // Qt does not render QAction tooltips inside a menu unless asked. The
        // Color Management item is disabled whenever colour management is
        // unavailable, and its tooltip is the only place the reason is stated;
        // without this the user sees a greyed item and no explanation at all.
        viewMenu->setToolTipsVisible(true);
        viewMenu->addAction(zoomInAction);
        viewMenu->addAction(zoomOutAction);
        viewMenu->addSeparator();
        viewMenu->addAction(fitAction);
        viewMenu->addAction(actualPixelsAction);
        viewMenu->addSeparator();
        viewMenu->addAction(fullScreenAction);
        viewMenu->addAction(minimapToggleAction);
        viewMenu->addAction(channelMixerToggleAction);
        viewMenu->addAction(sceneThumbnailToggleAction);
        viewMenu->addAction(associatedImagesToggleAction);
        viewMenu->addAction(propertiesToggleAction);
        viewMenu->addAction(metadataToggleAction);
        viewMenu->addAction(annotationListToggleAction);
        viewMenu->addAction(annotationPropertiesToggleAction);
        viewMenu->addSeparator();
        viewMenu->addAction(colorManagementAction);
        viewMenu->addSeparator();
        viewMenu->addAction(setDefaultProfileAction);
        viewMenu->addAction(clearDefaultProfileAction);
        viewMenu->addSeparator();
        viewMenu->addAction(setSlideProfileAction);
        viewMenu->addAction(clearSlideProfileAction);
        viewMenu->addAction(manageSlideProfilesAction);

        QMenu* toolsMenu = owner->menuBar()->addMenu("&Tools");
        toolsMenu->addAction(panToolAction);
        toolsMenu->addAction(rectangleToolAction);
        toolsMenu->addAction(ellipseToolAction);
        toolsMenu->addAction(polygonToolAction);
        toolsMenu->addAction(freehandToolAction);
        toolsMenu->addAction(pointToolAction);
        toolsMenu->addAction(arrowToolAction);
        // Preferences lives here rather than under Edit, which holds only Undo
        // and Redo. setMenuRole() moves it to the application menu on macOS
        // regardless. (The comment here used to say there was no Edit menu at
        // all, which stopped being true when Undo and Redo got one.)
        toolsMenu->addSeparator();
        toolsMenu->addAction(preferencesAction);

        QMenu* helpMenu = owner->menuBar()->addMenu("&Help");
        helpMenu->addAction(aboutAction);
    }

    void connectSignals()
    {
        // File actions
        QObject::connect(openAction, &QAction::triggered, owner, [this]() {
            QList<DriverFilter> filters = availableDriverFilters();
            QString filterString = buildOpenFilterString(filters);
            QString selectedFilter;
            QString filePath = QFileDialog::getOpenFileName(
                owner, "Open Slide", QString(),
                filterString, &selectedFilter);
            if (!filePath.isEmpty()) {
                QString driverId = driverIdForFilter(selectedFilter, filters);
                owner->openSlide(filePath.toStdString(), driverId.toStdString());
            }
        });

        if (openFolderAction) {
            QObject::connect(openFolderAction, &QAction::triggered, owner, [this]() {
                QString dirPath = QFileDialog::getExistingDirectory(
                    owner, "Open DICOM Folder", QString());
                if (dirPath.isEmpty()) {
                    return;
                }
                // Forced rather than auto-detected: the user said DICOM, so a
                // folder holding no DICOM files should fail with the driver's
                // "no valid DICOM files found" message instead of SlideIO's
                // generic "cannot find a driver for this path".
                owner->openSlide(QDir::toNativeSeparators(dirPath).toStdString(), "DCM");
            });
        }

        QObject::connect(closeAction, &QAction::triggered, owner, [this]() {
            // A pending properties edit is committed by the aboutToCloseSlide
            // listener above, which closeSlide() emits before it flushes.
            viewportWidget->closeSlide();
            closeAction->setEnabled(false);
            // Paired with closeAction at all four of its sites -- construction,
            // here, a successful open and a failed one: the three exports belong
            // to the open scene, so they live and die with it.
            exportCountsAction->setEnabled(false);
            exportMeasurementsAction->setEnabled(false);
            exportAnnotationsAction->setEnabled(false);
            // Nothing is open now, so the title stops naming a slide. Done here
            // and not from the slideClosed signal, which also fires at the start
            // of every open: openSlide() sets the title before the viewport
            // closes the previous slide, so clearing there would blank it for
            // every open instead of only for a close. A stale title cost a whole
            // session of group AC -- a correctly closed viewer read as a failed
            // reopen.
            owner->setWindowTitle(kApplicationTitle);
            // Reset the tool here rather than on the slideClosed signal, which
            // also fires at the start of every open. Putting the viewer away is
            // the one case where returning to Navigate is what the user meant.
            viewportWidget->setActiveTool(AnnotationTool::Pan);
            sceneThumbnailPanel->clear();
            associatedImagesPanel->clear();
            // Both panels are empty now, so the memo of which file they hold
            // has to go with them. The slideOpened handler skips the rebuild
            // when the file has not changed -- without this, reopening the slide
            // just closed left the Scenes panel blank for the rest of the
            // session.
            lastOpenedFilePath.clear();
            // Dock panel visibility intentionally preserved — user-controlled.
        });

        QObject::connect(openLogAction, &QAction::triggered, owner, [this]() {
            QString path = logFilePath();
            if (!QFile::exists(path)) {
                QMessageBox::information(owner, "Open Log File",
                    QString("Log file does not exist yet:\n%1").arg(path));
                return;
            }
            // QDesktopServices::openUrl with a local file URL launches the
            // platform's default handler (notepad on Windows, TextEdit on macOS,
            // xdg-open on Linux).
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
                QMessageBox::warning(owner, "Open Log File",
                    QString("Failed to open log file:\n%1").arg(path));
            }
        });

        QObject::connect(exitAction, &QAction::triggered, owner, &QMainWindow::close);

        // View actions
        QObject::connect(zoomInAction, &QAction::triggered, viewportWidget, &ViewportWidget::zoomIn);
        QObject::connect(zoomOutAction, &QAction::triggered, viewportWidget, &ViewportWidget::zoomOut);
        QObject::connect(fitAction, &QAction::triggered, viewportWidget, &ViewportWidget::fitToSlide);
        QObject::connect(actualPixelsAction, &QAction::triggered, viewportWidget, &ViewportWidget::setActualPixels);

        QObject::connect(fullScreenAction, &QAction::toggled, owner, [this](bool checked) {
            if (checked) {
                owner->showFullScreen();
            } else {
                owner->showNormal();
            }
        });

        QObject::connect(minimapToggleAction, &QAction::toggled, minimapWidget, &QWidget::setVisible);

        QObject::connect(colorManagementAction, &QAction::toggled,
                          owner, &MainWindow::onColorManagementToggled);

        QObject::connect(setDefaultProfileAction, &QAction::triggered,
                          owner, &MainWindow::onSetDefaultColorProfile);
        QObject::connect(clearDefaultProfileAction, &QAction::triggered,
                          owner, &MainWindow::onClearDefaultColorProfile);

        QObject::connect(setSlideProfileAction, &QAction::triggered,
                         owner, &MainWindow::onSetSlideColorProfile);
        QObject::connect(clearSlideProfileAction, &QAction::triggered,
                         owner, &MainWindow::onClearSlideColorProfile);

        // Associating the actions with the viewport is what gives their
        // WidgetWithChildrenShortcut context something to match against; the
        // menu still shows "V" and "R" either way.
        viewportWidget->addAction(panToolAction);
        viewportWidget->addAction(rectangleToolAction);
        viewportWidget->addAction(ellipseToolAction);
        viewportWidget->addAction(polygonToolAction);
        viewportWidget->addAction(freehandToolAction);
        viewportWidget->addAction(pointToolAction);
        viewportWidget->addAction(arrowToolAction);

        QObject::connect(panToolAction, &QAction::triggered, owner, [this]() {
            viewportWidget->setActiveTool(AnnotationTool::Pan);
        });
        // Every drawing tool is gated on an identity, not just Rectangle.
        auto selectDrawingTool = [this](AnnotationTool tool) {
            if (!annotationSettings->hasUserName()) {
                // FR-USER-01: creation is blocked until an identity is
                // configured. Asked here, on the first attempt to annotate,
                // rather than at startup -- someone who opened the application
                // to look at a slide should not be met by a dialog.
                QMessageBox::information(
                    owner, tr("Your name is needed"),
                    tr("Annotations record who created them. Enter your name in "
                       "Preferences before drawing."));
                if (!openPreferences()) {
                    panToolAction->setChecked(true);
                    return;
                }
            }
            viewportWidget->setActiveTool(tool);
        };
        QObject::connect(rectangleToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Rectangle);
        });
        QObject::connect(ellipseToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Ellipse);
        });
        QObject::connect(polygonToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Polygon);
        });
        QObject::connect(freehandToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Freehand);
        });
        // Through selectDrawingTool, not setActiveTool: these create
        // annotations like any other tool, so FR-USER-01's identity gate
        // stands in front of them too.
        QObject::connect(pointToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Point);
        });
        QObject::connect(arrowToolAction, &QAction::triggered, owner, [selectDrawingTool]() {
            selectDrawingTool(AnnotationTool::Arrow);
        });

        QObject::connect(preferencesAction, &QAction::triggered, owner, [this]() {
            openPreferences();
        });

        QObject::connect(importAnnotationsAction, &QAction::triggered, owner, [this]() {
            importAnnotations();
        });

        QObject::connect(exportCountsAction, &QAction::triggered, owner, [this]() {
            exportCounts();
        });

        QObject::connect(exportMeasurementsAction, &QAction::triggered, owner, [this]() {
            exportMeasurements();
        });

        QObject::connect(exportAnnotationsAction, &QAction::triggered, owner, [this]() {
            exportAnnotations();
        });

        QObject::connect(openAnnotationsFolderAction, &QAction::triggered, owner, [this]() {
            const QString workspace = annotationSettings->workspaceDirectory();
            const QString annotations = workspace + QStringLiteral("/annotations");
            const QString target = QFileInfo::exists(annotations) ? annotations : workspace;
            if (!QFileInfo::exists(target)) {
                QMessageBox::information(
                    owner, tr("No annotations yet"),
                    tr("The annotation folder is created the first time you save an "
                       "annotation. It will be:\n\n%1")
                        .arg(QDir::toNativeSeparators(annotations)));
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        });

        QObject::connect(viewportWidget, &ViewportWidget::activeToolChanged, owner,
                         [this](AnnotationTool tool) {
                             panToolAction->setChecked(tool == AnnotationTool::Pan);
                             rectangleToolAction->setChecked(tool == AnnotationTool::Rectangle);
                             ellipseToolAction->setChecked(tool == AnnotationTool::Ellipse);
                             polygonToolAction->setChecked(tool == AnnotationTool::Polygon);
                             freehandToolAction->setChecked(tool == AnnotationTool::Freehand);
                             pointToolAction->setChecked(tool == AnnotationTool::Point);
                             arrowToolAction->setChecked(tool == AnnotationTool::Arrow);
                             if (isNPointTool(tool)) {
                                 // A user who clicks once and does not know how
                                 // to finish has no affordance at all otherwise.
                                 statusBarManager->showTransientMessage(
                                     tr("Click to add points, double-click or Enter to "
                                        "finish, Esc to cancel"));
                             }
                         });

        QObject::connect(viewportWidget, &ViewportWidget::annotationsAvailableChanged, owner,
                         [this](bool available, const QString& reason) {
                             applyAnnotationAvailability(available, reason);
                         });

        QObject::connect(viewportWidget, &ViewportWidget::annotationEditBlocked, owner,
                         [this](const QString& reason) { statusBarManager->showTransientMessage(reason); });

        // A field typed into but not yet committed would otherwise be wiped
        // when the model empties. This used to be a call placed by hand ahead
        // of the three closeSlide() sites MainWindow owns, which left the five
        // that go through openScene() and reopenCurrentScene() -- a scene
        // switch, a workspace-folder change, and three colour-profile paths --
        // relying on the focus moving first. That is luck, not design. The
        // signal fires from closeSlide() itself, so every path is covered.
        QObject::connect(viewportWidget, &ViewportWidget::aboutToCloseSlide, owner, [this]() {
            if (annotationPropertiesPanel != nullptr) {
                annotationPropertiesPanel->commitPendingEdit();
            }
        });

        QObject::connect(manageSlideProfilesAction, &QAction::triggered, owner, [this]() {
            const std::string openSlideId = owner->currentSlideId();
            const bool hadOverride =
                !openSlideId.empty() && overrideStore->find(openSlideId).has_value();

            SlideProfilesDialog dialog(*overrideStore, owner);
            QObject::connect(&dialog, &SlideProfilesDialog::overridesChanged, owner,
                [this, openSlideId, hadOverride]() {
                    owner->applyColorProfilePolicy();
                    // Reopening unconditionally would reload the user's slide for
                    // no reason whenever an unrelated entry was removed; only do
                    // it when the slide on screen just lost its own entry.
                    if (hadOverride && !overrideStore->find(openSlideId).has_value()) {
                        viewportWidget->reopenCurrentScene();
                    }
                });
            dialog.exec();
        });

        // Help actions. The dialog is built per invocation so that it picks up
        // the OpenGL strings once the viewport's context has come up, rather
        // than caching an empty set from before the first show.
        QObject::connect(aboutAction, &QAction::triggered, owner, [this]() {
            AboutDialog dialog(viewportWidget->glInfo(), owner);
            dialog.exec();
        });

        // Viewport signals
        QObject::connect(viewportWidget, &ViewportWidget::viewportChanged, owner, [this]() {
            auto* ctrl = viewportWidget->controller();
            if (!ctrl) {
                return;
            }
            const auto& vp = ctrl->viewport();
            double baseMag = ctrl->baseMagnification();

            statusBarManager->updateMagnification(vp.scale(), baseMag);
            minimapWidget->setViewport(vp, ctrl->slideWidth(), ctrl->slideHeight());
            zoomIndicatorWidget->setZoomLevel(vp.scale(), baseMag);
        });

        QObject::connect(viewportWidget, &ViewportWidget::cursorMoved, owner,
            [this](double slideX, double slideY) {
                statusBarManager->updateCursorPosition(slideX, slideY);
            });

        QObject::connect(viewportWidget, &ViewportWidget::slideOpened, owner,
            [this](const std::string& filePath) {
                const bool sameFile = (filePath == lastOpenedFilePath);
                lastOpenedFilePath = filePath;
                closeAction->setEnabled(true);
                exportCountsAction->setEnabled(true);
                exportMeasurementsAction->setEnabled(true);
                exportAnnotationsAction->setEnabled(true);
                // The Rectangle tool is NOT enabled here: an associated image
                // opens through this path too, and so does a slide whose
                // annotations could not be read. Its enabled state comes from
                // annotationsAvailableChanged, which ViewportWidget emits just
                // after this signal.
                // The V and R shortcuts are scoped to the viewport, so give it
                // focus once a slide is up; otherwise they would do nothing
                // until the user happened to click the image first.
                viewportWidget->setFocus(Qt::OtherFocusReason);
                auto* ctrl = viewportWidget->controller();
                if (ctrl) {
                    const auto& vp = ctrl->viewport();
                    statusBarManager->updateMagnification(vp.scale(), ctrl->baseMagnification());
                    minimapWidget->setViewport(vp, ctrl->slideWidth(), ctrl->slideHeight());
                    zoomIndicatorWidget->setZoomLevel(vp.scale(), ctrl->baseMagnification());
                }

                // Populate panel content. Visibility/position is intentionally
                // NOT changed here — those are user-controlled via the View
                // menu and persisted across runs by save/restoreState().
                const auto& info = viewportWidget->slideInfo();
                zoomIndicatorWidget->setResolution(info.resolutionX);
                if (annotationPropertiesPanel != nullptr) {
                    // Measurements need both axes: pixels are not necessarily
                    // square, and a perimeter does not scale by a single factor.
                    annotationPropertiesPanel->setPixelSize(
                        core::PixelSize{info.resolutionX, info.resolutionY});
                }
                if (info.numChannels > 0) {
                    channelMixerPanel->setChannels(info.channels);
                } else {
                    channelMixerPanel->clearChannels();
                }

                spdlog::info("MainWindow: setting Z/T: numZ={}, numT={}", info.numZSlices, info.numTFrames);
                ztNavigationWidget->setSliceFrameCounts(info.numZSlices, info.numTFrames);
                ztNavigationWidget->setCurrentValues(viewportWidget->currentZSlice(),
                                                     viewportWidget->currentTFrame());
                ztNavigationWidget->raise();

                // On intra-slide opens (clicking a scene/aux thumbnail in the
                // currently-loaded file) the panels already hold the correct
                // items and pixmaps, so leave them alone. Rebuilding would
                // clear the thumbnails for the duration of the regeneration.
                if (!sameFile) {
                    // Always populate the Scenes panel when the slide has any
                    // scenes (single-scene slides should still show their one
                    // thumbnail). Only fully empty enumerations clear the panel.
                    if (!info.scenes.empty()) {
                        sceneThumbnailPanel->setScenes(info.scenes, {});
                        sceneThumbnailPanel->setActiveScene(0, false);
                        viewportWidget->generateSceneThumbnails();
                    } else {
                        sceneThumbnailPanel->clear();
                    }

                    if (!info.auxImages.empty()) {
                        associatedImagesPanel->setScenes({}, info.auxImages);
                        viewportWidget->generateAuxImageThumbnails();
                    } else {
                        associatedImagesPanel->clear();
                    }
                }

                propertiesPanel->setSlideInfo(info);

                const bool available =
                    info.colorManagement == core::ColorManagementAvailability::Available;
                colorManagementAction->setEnabled(available);
                // Gated on whether an override could ever apply to this slide,
                // not merely on a slide being open. On a fluorescence slide ICC
                // conversion is refused outright, so storing one would record an
                // entry that reads as configured and can never take effect.
                //
                // Not gated on `available`: a brightfield slide that embeds no
                // profile reports NoProfile, and that is the case the feature
                // exists for -- supplying the profile is what makes colour
                // management available at the next open.
                const bool overrideCanApply =
                    core::slideProfileOverrideCanApply(info.colorManagement);
                setSlideProfileAction->setEnabled(viewportWidget->isSlideOpen() && overrideCanApply);
                // Clear stays enabled on the existence of an entry alone. A user
                // may hold an override stored before that gate existed, and
                // removing a setting must never be blocked by the very condition
                // that made the setting useless.
                clearSlideProfileAction->setEnabled(
                    viewportWidget->isSlideOpen()
                    && overrideStore->find(owner->currentSlideId()).has_value());
                // A configured-but-unusable default profile is a fault in the
                // setting, not in the slide. applyDefaultColorProfile dropped
                // its bytes, so infra correctly sees no default and reports
                // NoProfile; say what actually went wrong rather than letting
                // that stand as "no default profile is set".
                const std::string availabilityReason =
                    core::colorManagementUnavailableReason(info.colorManagement,
                                                           info.colorManagementDetail);
                std::string unavailableReason = availabilityReason;
                if (!available && !defaultProfileProblem.empty()) {
                    unavailableReason = defaultProfileProblem;
                }
                // Most specific problem first. An override the user set for
                // this very slide explains the colours better than a global
                // default does, and either explains them better than a message
                // about what the slide does or does not embed.
                const std::string overrideProblem =
                    viewportWidget->lastColorProfileProblem();
                const std::string reason =
                    overrideProblem.empty() ? unavailableReason : overrideProblem;
                // The two tips carry different things on purpose. The tooltip
                // renders in the menu (viewMenu has setToolTipsVisible) and is
                // where a problem belongs; the status tip is the action's
                // description, which a broken override on an otherwise fine
                // slide must not cost the user. Only when colour management is
                // unavailable -- when there is no behaviour left to describe --
                // does the status tip give up the description for the reason.
                colorManagementAction->setToolTip(QString::fromStdString(reason));
                colorManagementAction->setStatusTip(QString::fromStdString(
                    (available || reason.empty()) ? kColorManagementDescription : reason));
                // Disabled above for exactly the reason the slide is not
                // colorimetric, so say so in the same words rather than
                // inventing a second explanation of the same fact. The raw
                // availability reason, not unavailableReason: a broken default
                // profile is not why this action is disabled.
                setSlideProfileAction->setToolTip(QString::fromStdString(
                    overrideCanApply ? std::string() : availabilityReason));
                setSlideProfileAction->setStatusTip(QString::fromStdString(
                    (overrideCanApply || availabilityReason.empty())
                        ? kSetSlideProfileDescription
                        : availabilityReason));

                QSettings settings;
                const bool wanted = available
                    && settings.value(kColorManagementKey, false).toBool();
                // Syncing the action's checked state to the newly opened slide is
                // not a user action and must not be recorded as one: without this
                // blocker, an unmanageable slide (wanted == false) changing the
                // action's state away from a remembered "on" would fire toggled()
                // and overwrite the stored preference with false. The lines below
                // already perform every update onColorManagementToggled would —
                // the slot is only suppressed, not skipped.
                {
                    QSignalBlocker blocker(colorManagementAction);
                    colorManagementAction->setChecked(wanted);
                }
                viewportWidget->setColorMode(
                    wanted ? core::ColorMode::Managed : core::ColorMode::Raw);
                statusBarManager->setColorManaged(wanted);
                propertiesPanel->setActiveColorProfile(
                    viewportWidget->activeColorProfileInfo(),
                    info.colorProfileOrigin, info.displacedEmbeddedProfile,
                    info.colorProfileInfo.description,
                    ctrl ? ctrl->colorMode() : core::ColorMode::Raw);

                // An override the user set just before this open asked, in
                // substance, for colour management to be on -- but only if the
                // profile they nominated works, which nothing could know until
                // this reopen reported it. Taken and cleared here whether or
                // not it matches and whether or not it is applied, so a record
                // left behind by a reopen that never arrived cannot reach a
                // later slide.
                const std::string pendingEnableSlideId = pendingColorManagementEnableSlideId;
                pendingColorManagementEnableSlideId.clear();
                // Deliberately NOT under the blocker above: this one is the
                // user's own action arriving late, so onColorManagementToggled
                // must run and write the global preference exactly as a click
                // on the menu item would. Every condition is a fail-closed
                // guard -- a profile that did not bind leaves `available`
                // false, and a different slide having opened in the meantime
                // leaves the ids unequal; either way nothing is written and
                // the user can still turn it on themselves.
                if (!pendingEnableSlideId.empty() && available
                    && pendingEnableSlideId == owner->currentSlideId()
                    && !colorManagementAction->isChecked()) {
                    colorManagementAction->setChecked(true);
                }

                metadataPanel->setSlideInfo(info);
                // No thumbnail will arrive for a slide with no downsampled
                // level, so tell the minimap to explain the empty panel
                // rather than leaving a blank grey rectangle.
                minimapWidget->setOverviewAvailable(info.overviewAvailable);
            });

        QObject::connect(viewportWidget, &ViewportWidget::thumbnailReady, owner,
            [this](const QImage& thumbnail) {
                minimapWidget->setThumbnail(thumbnail);
            });

        QObject::connect(viewportWidget, &ViewportWidget::errorOccurred, owner,
            [this](const std::string& message) {
                // openSlide() sets the title before the open is attempted, and
                // the only thing that emits this is an open that failed, so the
                // title is naming a slide that is not there.
                owner->setWindowTitle(kApplicationTitle);
                QMessageBox::critical(owner, "Error", QString::fromStdString(message));
            });

        QObject::connect(viewportWidget, &ViewportWidget::slideClosed, owner, [this]() {
            // Disabling the action is what satisfies "no drawing tool without a
            // slide" (spec §6); the active tool is deliberately left alone.
            // openSlide() calls closeSlide() first, so resetting it here would
            // drop the user back to Navigate every time they opened the next
            // slide -- re-picking the tool on every slide of a series.
            rectangleToolAction->setEnabled(false);
            ellipseToolAction->setEnabled(false);
            polygonToolAction->setEnabled(false);
            freehandToolAction->setEnabled(false);
            pointToolAction->setEnabled(false);
            arrowToolAction->setEnabled(false);
            closeAction->setEnabled(false);
            exportCountsAction->setEnabled(false);
            exportMeasurementsAction->setEnabled(false);
            exportAnnotationsAction->setEnabled(false);
            minimapWidget->clearThumbnail();
            statusBarManager->updateCursorPosition(0.0, 0.0);
            statusBarManager->updateMagnification(1.0, 0.0);
            zoomIndicatorWidget->setResolution(0.0);
            channelMixerPanel->clearChannels();
            propertiesPanel->clear();
            metadataPanel->clear();

            colorManagementAction->setEnabled(false);
            setSlideProfileAction->setEnabled(false);
            // The action is disabled because no slide is open now, not because
            // of whatever slide the tooltip/status tip last described -- clear
            // that reason rather than let it outlive the slide it was about.
            setSlideProfileAction->setToolTip(QString());
            setSlideProfileAction->setStatusTip(QString::fromStdString(kSetSlideProfileDescription));
            clearSlideProfileAction->setEnabled(false);
            {
                // As in the slide-opened handler: resetting the action to reflect
                // "no slide open" is not a user action, so it must not fire
                // toggled() and overwrite the remembered view/colorManagement
                // preference.
                QSignalBlocker blocker(colorManagementAction);
                colorManagementAction->setChecked(false);
            }
            statusBarManager->setColorManaged(false);
            // Note: scene panel is NOT cleared here because slideClosed also fires
            // during scene switching (openScene calls closeSlide internally).
            // The scene panel is cleared explicitly in openSlide() and the close action.
            // Dock panel visibility is intentionally not changed here — the user
            // controls it via the View menu and the layout persists across runs.
        });

        // Loading overlay lifecycle
        QObject::connect(viewportWidget, &ViewportWidget::loadingStarted, owner,
            [this](const QString& displayName) {
                loadingOverlay->resize(viewportWidget->size());
                loadingOverlay->move(0, 0);
                loadingOverlay->raise();
                slideLoadInFlight = true;
                loadingOverlay->start(displayName);
            });

        QObject::connect(viewportWidget, &ViewportWidget::loadingStatusChanged, owner,
            [this](const QString& text) {
                loadingOverlay->setStatus(text);
            });

        QObject::connect(viewportWidget, &ViewportWidget::loadingFinished, owner, [this]() {
            slideLoadInFlight = false;
            loadingOverlay->stop();
        });

        // Scene thumbnail panel: wire scene switching
        QObject::connect(viewportWidget, &ViewportWidget::sceneThumbnailReady, owner,
            [this](int sceneIndex, bool isAuxiliary, const std::string& name, const QImage& thumbnail) {
                if (isAuxiliary) {
                    associatedImagesPanel->setThumbnail(sceneIndex, isAuxiliary, name, thumbnail);
                } else {
                    sceneThumbnailPanel->setThumbnail(sceneIndex, isAuxiliary, name, thumbnail);
                }
            });

        QObject::connect(sceneThumbnailPanel, &SceneThumbnailPanel::sceneSelected, owner,
            [this](int sceneIndex) {
                auto filePath = viewportWidget->currentFilePath();
                if (filePath.empty()) return;

                viewportWidget->openScene(filePath, sceneIndex);
                sceneThumbnailPanel->setActiveScene(sceneIndex, false);
            });

        QObject::connect(associatedImagesPanel, &SceneThumbnailPanel::auxImageSelected, owner,
            [this](const std::string& auxImageName) {
                auto filePath = viewportWidget->currentFilePath();
                if (filePath.empty()) return;

                // Load the aux image at native resolution and pop it into a
                // dedicated resizable window. The main slide stays loaded in
                // the viewport — the user is just previewing the label/macro/
                // preview alongside, not switching to it.
                QImage img = viewportWidget->loadAuxImage(auxImageName);
                if (img.isNull()) {
                    QMessageBox::warning(owner, "Open Associated Image",
                        QString("Failed to load associated image '%1'.")
                            .arg(QString::fromStdString(auxImageName)));
                    return;
                }
                auto* w = new AssociatedImageWindow(
                    QString::fromStdString(auxImageName), img, owner);
                w->show();
                w->raise();
                w->activateWindow();
            });

        // Channel mixer panel: push settings changes to viewport
        QObject::connect(channelMixerPanel, &ChannelMixerPanel::channelSettingsChanged, owner,
            [this](const std::vector<core::ChannelInfo>& channels) {
                viewportWidget->setChannelSettings(channels);
            });

        // Minimap navigation
        QObject::connect(minimapWidget, &MinimapWidget::navigationRequested, owner,
            [this](double slideCenterX, double slideCenterY) {
                auto* ctrl = viewportWidget->controller();
                if (!ctrl) {
                    return;
                }
                const auto& vp = ctrl->viewport();
                double currentSlideX = 0.0;
                double currentSlideY = 0.0;
                vp.screenToSlide(vp.screenWidth() / 2.0, vp.screenHeight() / 2.0,
                                 currentSlideX, currentSlideY);

                double deltaSlideX = currentSlideX - slideCenterX;
                double deltaSlideY = currentSlideY - slideCenterY;
                double screenDx = deltaSlideX * vp.scale();
                double screenDy = deltaSlideY * vp.scale();

                viewportWidget->panByPixels(screenDx, screenDy);
            });

        // Zoom indicator
        QObject::connect(zoomIndicatorWidget, &ZoomIndicatorWidget::zoomChanged, owner,
            [this](double newScale) {
                auto* ctrl = viewportWidget->controller();
                if (!ctrl) {
                    return;
                }
                double currentScale = ctrl->viewport().scale();
                if (currentScale > 0.0) {
                    double factor = newScale / currentScale;
                    double cx = viewportWidget->width() / 2.0;
                    double cy = viewportWidget->height() / 2.0;
                    ctrl->zoomToPoint(cx, cy, factor);
                    viewportWidget->update();
                    emit viewportWidget->viewportChanged();
                }
            });

        // Z/T navigation
        QObject::connect(ztNavigationWidget, &ZTNavigationWidget::zSliceChanged, owner,
            [this](int zIndex) {
                viewportWidget->setZSlice(zIndex);
            });

        QObject::connect(ztNavigationWidget, &ZTNavigationWidget::tFrameChanged, owner,
            [this](int tFrame) {
                viewportWidget->setTFrame(tFrame);
            });
    }

    void addToRecentFiles(const std::string& filePath)
    {
        QSettings settings;
        // The whole rule -- normalize, promote, de-duplicate, truncate -- is in
        // recentFilesWithPathPromoted, where a test can reach it. An exact
        // string match was not enough: the shell gives backslashes where the
        // file dialog gives forward slashes, and the menu labels an entry with
        // its last component alone, so one slide was listed twice under one
        // name.
        const QStringList files = recentFilesWithPathPromoted(
            settings.value(kRecentFilesKey).toStringList(),
            QString::fromStdString(filePath), kMaxRecentFiles);

        settings.setValue(kRecentFilesKey, files);
        updateRecentFilesMenu();
    }

    void updateRecentFilesMenu()
    {
        if (!recentFilesMenu) {
            return;
        }

        recentFilesMenu->clear();

        QSettings settings;
        QStringList files = settings.value(kRecentFilesKey).toStringList();

        if (files.isEmpty()) {
            QAction* emptyAction = recentFilesMenu->addAction("(No recent files)");
            emptyAction->setEnabled(false);
            return;
        }

        for (int i = 0; i < files.size(); ++i) {
            const QString& filePath = files[i];
            // slideDisplayName, not QFileInfo::fileName: a DICOM study is a
            // folder, and fileName() on one labels the entry with nothing at all.
            QString label = QString("&%1  %2").arg(i + 1).arg(slideDisplayName(filePath));
            QAction* action = recentFilesMenu->addAction(label);
            action->setData(filePath);
            action->setStatusTip(filePath);
            QObject::connect(action, &QAction::triggered, owner, [this, filePath]() {
                owner->openSlide(filePath.toStdString(),
                                 driverIdForPath(filePath).toStdString());
            });
        }

        recentFilesMenu->addSeparator();
        QAction* clearAction = recentFilesMenu->addAction("Clear Recent Files");
        QObject::connect(clearAction, &QAction::triggered, owner, [this]() {
            QSettings settings;
            settings.remove(kRecentFilesKey);
            updateRecentFilesMenu();
        });
    }

    void repositionOverlays()
    {
        if (minimapWidget && viewportWidget) {
            int margin = 10;
            int x = viewportWidget->width() - minimapWidget->width() - margin;
            int y = margin;
            minimapWidget->move(x, y);
        }

        if (zoomIndicatorWidget && viewportWidget) {
            int margin = 10;
            int x = margin;
            int y = viewportWidget->height() - zoomIndicatorWidget->height() - margin;
            zoomIndicatorWidget->move(x, y);
        }

        if (ztNavigationWidget && viewportWidget) {
            int margin = 10;
            ztNavigationWidget->move(margin, margin);
        }

        if (loadingOverlay && viewportWidget) {
            loadingOverlay->resize(viewportWidget->size());
            loadingOverlay->move(0, 0);
        }
    }
};

void MainWindow::Impl::updateUndoActions()
{
    if (undoAction == nullptr || redoAction == nullptr || undoStack == nullptr) {
        return;
    }

    // A disabled QAction is removed from Qt's shortcut map, so its Ctrl+Z never
    // fires. The mid-trace vertex removal lives in this action's handler, so
    // gating enablement on the stack alone would make Ctrl+Z inert during a
    // trace whenever the stack is empty -- which is the common case, since
    // annotations loaded from disk push nothing.
    const bool traceLive = viewportWidget != nullptr && viewportWidget->hasLiveTrace();
    const bool canUndo = undoStack->canUndo();
    undoAction->setEnabled(canUndo || traceLive);

    // Naming the operation is most of the value: "Undo" alone does not tell the
    // user whether Ctrl+Z is about to remove the shape they just drew or undo a
    // move they have forgotten about. While a trace is live the key removes a
    // vertex, so the label has to say that rather than name a stack entry it
    // will not touch.
    if (traceLive) {
        undoAction->setText(tr("&Undo Vertex"));
    } else if (canUndo) {
        undoAction->setText(
            tr("&Undo %1").arg(QString::fromStdString(undoStack->undoDescription())));
    } else {
        undoAction->setText(tr("&Undo"));
    }

    const bool canRedo = undoStack->canRedo();
    redoAction->setEnabled(canRedo);
    redoAction->setText(canRedo
                            ? tr("&Redo %1").arg(QString::fromStdString(undoStack->redoDescription()))
                            : tr("&Redo"));
}

void MainWindow::Impl::buildAnnotationPersistence()
{
    annotationRepository = std::make_unique<infra::JsonAnnotationRepository>(
        annotationSettings->workspaceDirectory().toStdString());
    annotationPersistence = std::make_unique<app::AnnotationPersistenceService>(
        *annotationRepository, *annotations);
    viewportWidget->setPersistenceService(annotationPersistence.get());

    undoStack = std::make_unique<app::UndoRedoStack>();
    viewportWidget->setUndoStack(undoStack.get());
    undoStack->onStackChanged = [this]() { updateUndoActions(); };

    // Hung off the model rather than called at each slide-switch site: a future
    // way to change slides cannot forget it. Neither signal fires on an edit.
    //
    // They do fire on a reopen of the same scene, which is what a colour-profile
    // change is: the model empties and refills with the same annotations under
    // the same ids, leaving every step on the stack still valid. The viewport
    // flags that window so the history is not thrown away for nothing.
    const auto clearUndoUnlessReopening = [this]() {
        if (viewportWidget->isPreservingUndoContext()) {
            return;
        }
        undoStack->clear();
    };
    QObject::connect(annotations.get(), &app::AnnotationModel::cleared, owner,
                     clearUndoUnlessReopening);
    QObject::connect(annotations.get(), &app::AnnotationModel::modelReset, owner,
                     clearUndoUnlessReopening);

    // Starting or ending a trace changes what Ctrl+Z means, and therefore
    // whether the Undo action may be disabled at all.
    QObject::connect(viewportWidget, &ViewportWidget::liveTraceChanged, owner,
                     [this](bool) { updateUndoActions(); });

    QObject::connect(undoAction, &QAction::triggered, owner, [this]() {
        // Before every other test: the keystroke may not be for us at all.
        // Ctrl+Z in a dock's text field belongs to that field.
        if (routeEditShortcutToFocusWidget(owner, /*redo=*/false)) {
            return;
        }
        // One entry point for the key, with the context test in one place rather
        // than split between a shortcut and a key handler.
        if (viewportWidget->isDragInFlight()) {
            return;
        }
        if (viewportWidget->undoLastPendingVertex()) {
            return;
        }
        // The description has to be read before the undo: afterwards the top of
        // the stack is a different command.
        const QString what = QString::fromStdString(undoStack->undoDescription());
        undoStack->undo();
        statusBarManager->showTransientMessage(tr("Undo: %1").arg(what));
    });
    QObject::connect(redoAction, &QAction::triggered, owner, [this]() {
        if (routeEditShortcutToFocusWidget(owner, /*redo=*/true)) {
            return;
        }
        if (viewportWidget->isDragInFlight()) {
            return;
        }
        const QString what = QString::fromStdString(undoStack->redoDescription());
        undoStack->redo();
        statusBarManager->showTransientMessage(tr("Redo: %1").arg(what));
    });

    updateUndoActions();

    annotationPersistence->setMismatchResolver(
        [this](const core::AnnotationDocument& stored, const core::SlideProvenance& current) {
            QMessageBox box(owner);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(tr("Annotations may belong to another slide"));
            box.setText(tr("The stored annotations were saved for a different slide."));
            // Both names, so the user has something to judge by. A matching id
            // is not proof of provenance, and a mismatch is not proof of error:
            // a re-exported slide has a new content hash and the same tissue.
            box.setInformativeText(tr("Saved for: %1\nCurrently open: %2")
                                       .arg(QString::fromStdString(stored.slide.fileName),
                                            QString::fromStdString(current.fileName)));
            QPushButton* readOnly = box.addButton(tr("Open Read-Only"), QMessageBox::AcceptRole);
            QPushButton* reassociate =
                box.addButton(tr("Use for This Slide"), QMessageBox::DestructiveRole);
            box.addButton(tr("Ignore Them"), QMessageBox::RejectRole);
            box.setDefaultButton(readOnly);
            // Esc and the title-bar X must take the safe option the dialog
            // highlights, not the lone RejectRole button that Qt would pick.
            box.setEscapeButton(readOnly);
            box.exec();

            using Choice = app::AnnotationPersistenceService::MismatchChoice;
            // A decision rather than a failure, but the only one here that can
            // re-point stored annotations at a different slide, so it belongs in
            // the log with the pair of names the user judged by and the number of
            // annotations that pair decided the fate of.
            if (box.clickedButton() == reassociate) {
                // warn, not info: the stored file adopts this slide's id on the
                // next save, and its original association is then gone.
                spdlog::warn("Slide-id mismatch resolved as Use-for-This-Slide: "
                             "{} annotation(s) saved for '{}' (id {}) re-associated to '{}'",
                             stored.annotations.size(), stored.slide.fileName, stored.slideId,
                             current.fileName);
                return Choice::Reassociate;
            }
            if (box.clickedButton() == readOnly) {
                // Also the Esc and title-bar-X route, so this is the line that
                // appears when the dialog was dismissed rather than answered.
                spdlog::info("Slide-id mismatch resolved as Open-Read-Only: "
                             "{} annotation(s) saved for '{}' (id {}) shown over '{}', "
                             "not writable",
                             stored.annotations.size(), stored.slide.fileName, stored.slideId,
                             current.fileName);
                return Choice::ReadOnly;
            }
            spdlog::info("Slide-id mismatch resolved as Ignore: "
                         "{} annotation(s) saved for '{}' (id {}) not shown over '{}'; "
                         "file left unchanged",
                         stored.annotations.size(), stored.slide.fileName, stored.slideId,
                         current.fileName);
            return Choice::Ignore;
        });

    // Fires on a slide *switch* as well as an explicit close, because
    // openScene() calls closeSlide() internally. That is the point: it is the
    // one place where a failed write would otherwise cost the user everything
    // they had drawn, with no indication it had happened.
    annotationPersistence->setSaveFailureResolver(
        [this](const core::SaveResult& failure, int annotationCount) {
            using Choice = app::AnnotationPersistenceService::SaveFailureChoice;

            QMessageBox box(owner);
            box.setIcon(QMessageBox::Critical);
            box.setWindowTitle(tr("Annotations could not be saved"));
            box.setText(tr("%1 annotation(s) could not be saved and will be lost if you continue.")
                            .arg(annotationCount));
            box.setInformativeText(tr("%1\n\n%2\n\nRetry tries the same folder again, up to five times, "
                                      "and then the annotations are discarded. To keep them, choose "
                                      "Choose Another Folder.")
                                       .arg(QString::fromStdString(failure.message),
                                            QString::fromStdString(failure.path)));
            QPushButton* retry = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
            QPushButton* chooseFolder =
                box.addButton(tr("Choose Another Folder..."), QMessageBox::ActionRole);
            box.addButton(tr("Discard and Close"), QMessageBox::DestructiveRole);
            box.setDefaultButton(retry);
            // Esc must never mean Discard; Retry is the safe exit.
            box.setEscapeButton(retry);
            box.exec();

            if (box.clickedButton() == chooseFolder) {
                // A plain folder picker, NOT openPreferences() -- that path
                // calls endSlide(), which is what called this resolver, and the
                // two would call each other until the stack ran out.
                const QString chosen = QFileDialog::getExistingDirectory(
                    owner, tr("Choose a folder for annotations"),
                    annotationSettings->workspaceDirectory());
                if (chosen.isEmpty()) {
                    return Choice::Discard;
                }
                annotationSettings->setWorkspaceDirectory(chosen);
                // Re-root rather than rebuild: the service holds a reference to
                // this repository object.
                annotationRepository->setWorkspaceRoot(chosen.toStdString());
                return Choice::Retry;
            }
            if (box.clickedButton() == retry) {
                return Choice::Retry;
            }
            // Never Retry unless a button said so: this drives a bounded loop
            // in endSlide(), and a resolver that always retries spends all five
            // attempts wedging the UI thread.
            return Choice::Discard;
        });

    QObject::connect(annotationPersistence.get(), &app::AnnotationPersistenceService::loadFailed,
                     owner, [this](const QString& path, const QString& message) {
                         // The dialog is dismissed and then the refusal is gone.
                         // Logged here rather than in the service because the app
                         // layer does not link spdlog, and because what the user
                         // was actually told is the entry worth keeping.
                         spdlog::warn("Annotations refused: {} ({}); file left unchanged",
                                      path.toStdString(), message.toStdString());
                         QMessageBox::warning(
                             owner, tr("Annotations could not be opened"),
                             tr("%1\n\n%2\n\nThe file has not been changed. Annotations are "
                                "disabled for this slide so it cannot be overwritten.")
                                 .arg(message, QDir::toNativeSeparators(path)));
                     });

    QObject::connect(annotationPersistence.get(), &app::AnnotationPersistenceService::saveFailed,
                     owner, [this](const QString& path, const QString& message) {
                         // Latched: the document stays dirty after a failure, so
                         // the autosave retries it every couple of seconds. One
                         // dialog is a warning; one every two seconds is a trap
                         // the user cannot click their way out of.
                         if (saveFailureReported) {
                             return;
                         }
                         saveFailureReported = true;
                         // Inside the latch deliberately: the autosave retries
                         // every couple of seconds, so logging each attempt would
                         // bury the log on a dead share. One line opens the run of
                         // failures; the `saved` handler below closes it.
                         spdlog::warn("Annotation save failed: {} ({}); retrying",
                                      path.toStdString(), message.toStdString());
                         QMessageBox::warning(
                             owner, tr("Annotations could not be saved"),
                             tr("%1\n\n%2\n\nYour annotations are still open and will be "
                                "retried. Choose another folder in Preferences if this "
                                "keeps happening.")
                                 .arg(message, QDir::toNativeSeparators(path)));
                     });

    // A write that reached disk ends the run of bad luck, so the next distinct
    // failure is reported again even if the slide was never reopened.
    QObject::connect(annotationPersistence.get(), &app::AnnotationPersistenceService::saved,
                     owner, [this](const QString& path) {
                         // Closes the run opened above. Without it the log ends on
                         // "save failed" and never says the writes came back, which
                         // reads as unsaved work long after it reached disk.
                         if (saveFailureReported) {
                             spdlog::info("Annotation save recovered: {}", path.toStdString());
                         }
                         saveFailureReported = false;
                     });

    QObject::connect(annotationPersistence.get(), &app::AnnotationPersistenceService::saveAbandoned,
                     owner, [this](const QString& path, const QString& message) {
                         // The one event here that destroys work: retries are spent
                         // and the annotations are dropped. error, not warn -- if
                         // anything in this subsystem is worth finding in a log
                         // afterwards, it is this.
                         spdlog::error("Annotations discarded after repeated save failures: "
                                       "{} ({})",
                                       path.toStdString(), message.toStdString());
                         QMessageBox::critical(
                             owner, tr("Annotations were discarded"),
                             tr("Saving failed repeatedly, so the annotations for this slide "
                                "were discarded.\n\n%1\n\n%2")
                                 .arg(message, QDir::toNativeSeparators(path)));
                     });

    QObject::connect(annotationPersistence.get(),
                     &app::AnnotationPersistenceService::lockedByAnother, owner,
                     [](const QString& path, const QString& holder) {
                         // Logged, not shown: the user is already told by the
                         // read-only reason that ViewportWidget composes from
                         // isLockedByAnother(), and a dialog on top of it would
                         // interrupt a reader who is allowed to read. The log
                         // line is for the support question that follows --
                         // "why can I not annotate this slide" -- which the
                         // transient message cannot answer an hour later.
                         spdlog::warn("Annotations locked by another instance: {} held by {}",
                                      path.toStdString(),
                                      holder.isEmpty() ? std::string("an unidentified process")
                                                       : holder.toStdString());
                     });

    QObject::connect(annotationPersistence.get(), &app::AnnotationPersistenceService::activeChanged,
                     owner, [this](bool active) {
                         // The service announces no successful save, so this is
                         // the nearest thing to one: a slide that has just
                         // become writable is a fresh start for the latch
                         // above, and whatever was wrong with the last slide
                         // has already been reported about that slide.
                         if (active) {
                             saveFailureReported = false;
                         }
                         // This signal's own documentation says the ui enables
                         // and disables the drawing tools from it, and until now
                         // the ui did not: enablement hung solely off
                         // ViewportWidget::annotationsAvailableChanged, which is
                         // emitted only when a slide opens or closes. A
                         // workspace switch opens no slide, so a switch that
                         // landed on an unwritable file left every tool enabled
                         // and the user drew into a model nobody was saving.
                         //
                         // Second in line deliberately. On an open or a close
                         // the viewport emits afterwards with a reason naming
                         // the actual obstacle -- an associated image, an
                         // unidentified slide -- and that better message wins.
                         applyAnnotationAvailability(
                             active, active ? QString()
                                            : tr("Annotations are read-only for this slide: "
                                                 "the stored file could not be used."));
                     });

    // Built last, because it drives the service above and re-points the
    // repository through these two callbacks rather than reaching for
    // infra::JsonAnnotationRepository from the app layer.
    workspaceSwitch = std::make_unique<app::WorkspaceSwitch>(
        *annotationRepository, *annotations, *annotationPersistence,
        [this]() { return annotationRepository->workspaceRoot(); },
        [this](std::string root) { annotationRepository->setWorkspaceRoot(std::move(root)); });
    workspaceSwitch->setResolver(
        [this](const core::WorkspaceSwitchFacts& facts, const core::WorkspaceSwitchOptions& options) {
            WorkspaceChangeDialog dialog(facts, options, owner);
            dialog.exec();
            return dialog.choice();
        });
}

bool MainWindow::Impl::openPreferences()
{
    QStringList currentCategories;
    for (const std::string& category : categoryStore->categories()) {
        currentCategories.append(QString::fromStdString(category));
    }
    PreferencesDialog dialog(annotationSettings->workspaceDirectory(),
                             annotationSettings->userName(), currentCategories, owner);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    // The store normalises on write and on read, so what the panel is fed is
    // what a restart would load.
    std::vector<std::string> edited;
    for (const QString& entry : dialog.categories()) {
        edited.push_back(entry.toStdString());
    }
    categoryStore->setCategories(edited);
    const std::vector<std::string> stored = categoryStore->categories();
    annotationPropertiesPanel->setCategories(stored);
    annotationListPanel->setCategories(stored);

    annotationSettings->setUserName(dialog.userName());
    annotations->setDefaultAuthor(annotationSettings->userName().toStdString());

    const QString entered = dialog.workspaceDirectory();
    // Resolve the entry before comparing: a cleared field means "use the
    // default", which is very often the root already in use, and comparing the
    // raw entry would reload the slide only to land on the folder it was
    // already rooted at. Through AnnotationSettings' own rule rather than a
    // copy of it -- a second copy would drift, and this rule decides where
    // annotations are written.
    const QString requested = AnnotationSettings::resolveWorkspaceDirectory(entered);

    if (requested != QString::fromStdString(annotationRepository->workspaceRoot())) {
        if (slideLoadInFlight) {
            // A switch captures the open slide's key and set, then shows a
            // modal dialog. An async open completing during that dialog --
            // queued connections do run in a modal nested loop -- would call
            // beginSlide() for the NEW slide underneath it, after which the
            // switch would write the previous slide's annotations and then load
            // them back over a slide they do not belong to. Refusing is enough:
            // a modal dialog is also what stops the user starting an open once
            // the switch is already under way.
            // A modal box, not a transient line, which check X17 caught: the
            // user has just pressed OK on Preferences, so the change looks
            // accepted, and the one thing that says otherwise cannot be a
            // status line four seconds long while the loading overlay holds
            // their attention in the middle of the viewport. An action that
            // silently does not happen is worse than one that is refused out
            // loud. Same reasoning as the both-folders-unwritable report below.
            // The way out matters as much as the refusal. An open runs on a
            // detached thread with no timeout, so a load that hangs never
            // emits loadingFinished and this flag never clears -- without a
            // stated escape the folder could not be changed again for the rest
            // of the session. closeSlide() emits loadingFinished, and the menu
            // bar stays live under the loading overlay, so File > Close Slide
            // is a real route out and is named here.
            QMessageBox::information(
                owner, tr("The annotation folder was not changed"),
                tr("A slide is still opening. The annotation folder cannot be changed "
                   "until it has finished loading.\n\nTry again once it has opened, or "
                   "close the slide first if it is not going to."));
            return annotationSettings->hasUserName();
        }

        // Before everything: a field typed into but not yet committed would
        // otherwise be lost, and this is the one close path that does not go
        // through closeSlide()'s aboutToCloseSlide listener.
        if (annotationPropertiesPanel != nullptr) {
            annotationPropertiesPanel->commitPendingEdit();
        }

        // Captured before the setting is written, so Cancel can put it back --
        // and captured RAW, not resolved. An empty setting means "follow the
        // default"; restoring the resolved path instead would turn a Cancel
        // into a decision to pin the workspace at today's Documents folder,
        // which a user who later moves it would never see happen.
        const QString previousWorkspace = annotationSettings->configuredWorkspaceDirectory();

        annotationSettings->setWorkspaceDirectory(entered);
        // Read the path back rather than reusing `entered`: an empty or
        // whitespace-only entry clears the setting, and the repository must
        // then follow the default workspace rather than root itself at "".
        const QString resolved = annotationSettings->workspaceDirectory();

        if (!viewportWidget->isSlideOpen() || viewportWidget->isAuxImageShown()) {
            // An associated image answers isSlideOpen() with true -- slideOpen
            // is set in the shared open path -- and carries the file's slide id
            // with scene index 0. Handing that key to the switch would make it
            // beginSlide() scene 0 and paint the tissue's annotations over a
            // label, which is exactly what the open path refuses to do.
            //
            // isAuxImageShown() is false today whatever the user does, and this
            // half of the condition is insurance, not a live guard: the
            // Associated Images panel previews into a separate window and
            // leaves the slide in the viewport (see the auxImageSelected
            // handler above), and ViewportWidget::openAuxImage -- the only
            // thing that sets currentAuxImageName -- is reached only from
            // reopenCurrentScene, which calls it only when that name is already
            // non-empty. Nothing can set it first. Check X13 cannot exercise
            // this branch and no test reaches it either; it is here for the
            // wiring coming back, and the reasoning above is what makes it
            // right when it does.
            //
            // Nothing to carry over and nothing to ask about. Re-rooted, never
            // rebuilt: the service holds a core::IAnnotationRepository& bound at
            // construction, so destroying the repository out from under it is a
            // dangling reference.
            annotationRepository->setWorkspaceRoot(resolved.toStdString());
            return annotationSettings->hasUserName();
        }

        // WorkspaceSwitch re-points the repository itself, because what it
        // writes and where depends on what the new folder turns out to hold. It
        // also flushes to the old root and re-enters the slide, which is why
        // there is no endSlide() and no reopenCurrentScene() here any more. The
        // flush in particular must not be done twice: run() reads isActive()
        // before its own flush to decide whether the annotations may be
        // written, so a flush performed here would make every switch look
        // read-only and silently withhold copy, keep-current and merge.
        core::SlideProvenance provenance;
        const core::SlideInfo& info = viewportWidget->slideInfo();
        provenance.path = viewportWidget->currentFilePath();
        provenance.fileName =
            QFileInfo(QString::fromStdString(provenance.path)).fileName().toStdString();
        provenance.width = info.width;
        provenance.height = info.height;

        const core::AnnotationKey key{viewportWidget->currentSlideId(),
                                      viewportWidget->currentSceneIndex()};
        const app::WorkspaceSwitchResult switched =
            workspaceSwitch->run(resolved.toStdString(), key, provenance);

        if (switched.flush == core::SaveStatus::Saved
            || switched.flush == core::SaveStatus::NothingToDo) {
            saveFailureReported = false;
        }

        if (switched.rescued) {
            // The flush failed and the save-failure resolver rescued it to a
            // folder the user picked mid-failure, which it has already made the
            // workspace and the repository root. Applying the dialog's value on
            // top would strand the file it just saved somewhere this
            // application never looks again, and say nothing about it. The more
            // recent choice wins; the user is told where their work went.
            // Gated on the flush actually succeeding: when the retries against
            // the new folder failed too, endSlide() abandoned the annotations
            // and claiming they were saved would be false. The folder in use is
            // named either way, so the typed value is not mistaken for it.
            const QString folder =
                QDir::toNativeSeparators(annotationSettings->workspaceDirectory());
            if (switched.flush == core::SaveStatus::Saved) {
                statusBarManager->showTransientMessage(
                    tr("Annotations were saved to %1, which is now the annotation folder "
                       "(your entry was not used).")
                        .arg(folder));
            } else {
                statusBarManager->showTransientMessage(
                    tr("The annotation folder is now %1 (your entry was not used). "
                       "The annotations were NOT saved.")
                        .arg(folder));
            }
            return annotationSettings->hasUserName();
        }

        if (switched.choice == app::WorkspaceChoice::Cancel) {
            // The folder in use did not change, so neither should the setting.
            annotationSettings->setWorkspaceDirectory(previousWorkspace);
        } else {
            // Re-asserted, because the save-failure resolver may have written
            // the setting during the flush without the root ending up anywhere
            // new -- a user who hits "Choose Another Folder" and picks the one
            // already in use. run() sees no root change, correctly carries on
            // with the switch, and the setting would otherwise be left naming
            // the old folder while the repository writes to the new one. The
            // next launch would then root at the old folder and everything
            // saved this session would be invisible.
            annotationSettings->setWorkspaceDirectory(entered);
        }
        reportWorkspaceSwitch(switched);
    }
    return annotationSettings->hasUserName();
}

void MainWindow::Impl::applyAnnotationAvailability(bool available, const QString& reason)
{
    rectangleToolAction->setEnabled(available);
    ellipseToolAction->setEnabled(available);
    polygonToolAction->setEnabled(available);
    freehandToolAction->setEnabled(available);
    pointToolAction->setEnabled(available);
    arrowToolAction->setEnabled(available);
    // Import creates annotations, so it follows the tools, not the exports.
    importAnnotationsAction->setEnabled(available);
    // Same gate for the properties editor's fields. A read-only slide still
    // shows its annotations and still lets them be selected; what it refuses is
    // changing them, and a field that cannot be used should not look as though
    // it can. The reason is already going to the status bar just below.
    if (annotationPropertiesPanel != nullptr) {
        annotationPropertiesPanel->setEditingAllowed(available);
    }
    // The counting combo only; the tally rows stay visible and correct on a read-only slide.
    if (annotationListPanel != nullptr) {
        annotationListPanel->setCountingEnabled(available);
    }
    // Mirror that disabled state somewhere the user can actually see it. A
    // disabled menu item renders identically to an enabled one, and the
    // transient message below is gone in four seconds -- so without this, a user
    // who reaches for a tool a minute later gets silence and no way to learn
    // why. Set before the early return, so closing a slide clears it too.
    statusBarManager->setAnnotationsBlocked(!available && viewportWidget->isSlideOpen(), reason);
    if (available || !viewportWidget->isSlideOpen()) {
        // Either fine, or the transient "no slide" that every slide switch
        // emits -- leave the tool alone, as the slideClosed handler
        // deliberately does.
        return;
    }
    // A real gate: an associated image, an unidentifiable slide, or a file we
    // can read but must not write.
    viewportWidget->setActiveTool(AnnotationTool::Pan);
    if (!reason.isEmpty()) {
        statusBarManager->showTransientMessage(reason);
    }
}

void MainWindow::Impl::exportCounts()
{
    // The action is only enabled with a slide open, so this is defence in depth
    // rather than a live gate -- but the counts of nothing are a file full of
    // zeros that names no slide, and that is worse than no file.
    if (!viewportWidget->isSlideOpen()) {
        return;
    }
    if (!annotationsReadyToExport(tr("Export Counts"))) {
        return;
    }

    // The SAME call the strip makes, on the same two inputs. Not a second
    // counting pass that agrees by inspection: a report that can drift from the
    // screen it was read off is a clinical trap, and this one cannot.
    const std::vector<CategoryTallyRow> rows =
        categoryTally(annotations->annotations(), categoryStore->categories());

    const std::string slidePath = viewportWidget->currentFilePath();
    const int sceneIndex = viewportWidget->currentSceneIndex();
    const std::string stem = countExportFileStem(
        QFileInfo(QString::fromStdString(slidePath)).fileName().toStdString(), sceneIndex);

    CountExportContext context;
    context.slidePath = slidePath;
    // May be empty, for a slide that could not be identified. The column stays,
    // which is how the file says so rather than implying a count came from
    // somewhere it did not.
    context.slideId = viewportWidget->currentSlideId();
    context.sceneIndex = sceneIndex;
    const std::string csv = categoryCountCsv(rows, context, std::chrono::system_clock::now());

    const QString chosen = saveExportFile(tr("Export Counts"), QString::fromStdString(stem) + QStringLiteral(".csv"),
                                          tr("Comma-separated values (*.csv);;All files (*)"), csv,
                                          tr("The counts could not be written to:\n\n%1\n\n%2"));
    if (!chosen.isEmpty()) {
        statusBarManager->showTransientMessage(tr("Counts exported to %1").arg(QFileInfo(chosen).fileName()));
    }
}

QString MainWindow::Impl::saveExportFile(const QString& title, const QString& suggestedName, const QString& filter,
                                         const std::string& contents, const QString& failureText)
{
    // Offered in the annotation workspace -- this instance's, not the shared
    // setting -- where the files these exports describe already live. The
    // dialog asks about overwriting; this does not second-guess it.
    const QString workspace = annotationSettings->workspaceDirectory();
    const QString start = workspace.isEmpty() ? suggestedName : workspace + QStringLiteral("/") + suggestedName;
    const QString chosen = QFileDialog::getSaveFileName(owner, title, start, filter);
    if (chosen.isEmpty()) {
        return {};
    }

    const auto reportFailure = [this, &title, &chosen, &failureText](const QString& detail) {
        QMessageBox::warning(owner, title, failureText.arg(QDir::toNativeSeparators(chosen), detail));
    };

    QFile file(chosen);
    // Truncate, never Append: the dialog has already asked about overwriting,
    // and appending would bury a second byte-order mark and a second header row
    // in the middle of the file.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reportFailure(file.errorString());
        return {};
    }
    const qint64 written = file.write(contents.data(), static_cast<qint64>(contents.size()));
    // flush() and close() explicitly: a buffered write can still fail on the
    // way out, and ~QFile has nowhere to report that. A half-written export
    // that reported success is the one outcome worth this much care -- it
    // would be read as the whole scene.
    const bool flushed = file.flush();
    const QString error = file.errorString();
    file.close();
    if (written != static_cast<qint64>(contents.size()) || !flushed) {
        reportFailure(error);
        return {};
    }
    return chosen;
}

void MainWindow::Impl::exportMeasurements()
{
    // Defence in depth, as in exportCounts: the action is only enabled with a
    // slide open.
    if (!viewportWidget->isSlideOpen()) {
        return;
    }
    if (!annotationsReadyToExport(tr("Export Measurements"))) {
        return;
    }

    const std::string slidePath = viewportWidget->currentFilePath();
    const int sceneIndex = viewportWidget->currentSceneIndex();

    MeasurementExportContext context;
    context.slidePath = slidePath;
    context.slideId = viewportWidget->currentSlideId();
    context.sceneIndex = sceneIndex;
    // SlideInfo carries metres; zero means the slide did not say, and the
    // formatter then leaves every micron column empty rather than guess.
    const core::SlideInfo& info = viewportWidget->slideInfo();
    context.pixelWidthMicrons = info.resolutionX * 1e6;
    context.pixelHeightMicrons = info.resolutionY * 1e6;

    // Every annotation in the model, never the list's filtered view. Built
    // before the dialog, as the GeoJSON export is, so a scene that cannot be
    // written is reported without first asking where to put it.
    const MeasurementExportResult result =
        annotationMeasurementCsv(annotations->annotations(), context, std::chrono::system_clock::now());
    if (!result.error.empty()) {
        QMessageBox::warning(owner, tr("Export Measurements"),
                             tr("The measurements could not be exported.\n\n%1")
                                 .arg(QString::fromStdString(result.error)));
        return;
    }

    const std::string stem = measurementExportFileStem(
        QFileInfo(QString::fromStdString(slidePath)).fileName().toStdString(), sceneIndex);
    const QString chosen = saveExportFile(
        tr("Export Measurements"), QString::fromStdString(stem) + QStringLiteral(".csv"),
        tr("Comma-separated values (*.csv);;All files (*)"), result.text,
        tr("The measurements could not be written to:\n\n%1\n\n%2"));
    if (!chosen.isEmpty()) {
        statusBarManager->showTransientMessage(tr("Measurements exported to %1").arg(QFileInfo(chosen).fileName()));
    }
}

bool MainWindow::Impl::annotationsReadyToExport(const QString& title)
{
    // Both exports write a file that says what a scene holds, and an empty
    // model says "nothing" -- true only when the scene's file was read. After
    // a load failure, on an associated image or an unidentified slide, or with
    // a mismatch resolved by ignoring the stored annotations, the model is
    // empty without the scene being empty, and the export would file a
    // complete-looking false zero. A locked or read-only scene IS loaded and
    // stays exportable: that is what a reader wants to take away.
    if (annotationPersistence->annotationsLoaded()) {
        return true;
    }
    QMessageBox::warning(owner, title,
                         tr("This scene's annotations were not loaded, so there is nothing "
                            "reliable to export: a file written now would say the scene has "
                            "none.\n\nThe status bar says why they are unavailable."));
    return false;
}

void MainWindow::Impl::exportAnnotations()
{
    // Defence in depth, as in exportCounts: the action is only enabled with a
    // slide open.
    if (!viewportWidget->isSlideOpen()) {
        return;
    }
    if (!annotationsReadyToExport(tr("Export Annotations"))) {
        return;
    }

    const std::string slidePath = viewportWidget->currentFilePath();
    const int sceneIndex = viewportWidget->currentSceneIndex();

    core::GeoJsonExportContext context;
    context.slidePath = slidePath;
    context.slideId = viewportWidget->currentSlideId();
    context.sceneIndex = sceneIndex;
    // SlideInfo carries metres; zero means the slide did not say, and the
    // writer then leaves the pixel size out rather than claiming zero.
    const core::SlideInfo& info = viewportWidget->slideInfo();
    context.pixelSizeMicronsX = info.resolutionX * 1e6;
    context.pixelSizeMicronsY = info.resolutionY * 1e6;

    // Every annotation in the scene -- the model's, never the list's filtered
    // view. Built before the dialog, so a scene that cannot be written is
    // reported without first asking where to put it.
    const core::GeoJsonExportResult result =
        core::annotationsToGeoJson(annotations->annotations(), context, std::chrono::system_clock::now());
    if (!result.error.empty()) {
        QMessageBox::warning(owner, tr("Export Annotations"),
                             tr("The annotations could not be exported.\n\n%1")
                                 .arg(QString::fromStdString(result.error)));
        return;
    }

    const std::string stem = sceneExportFileStem(
        QFileInfo(QString::fromStdString(slidePath)).fileName().toStdString(), sceneIndex);
    const QString chosen = saveExportFile(
        tr("Export Annotations"), QString::fromStdString(stem) + QStringLiteral(".geojson"),
        tr("GeoJSON (*.geojson);;JSON (*.json);;All files (*)"), result.text,
        tr("The annotations could not be written to:\n\n%1\n\n%2"));
    if (!chosen.isEmpty()) {
        statusBarManager->showTransientMessage(tr("Annotations exported to %1").arg(QFileInfo(chosen).fileName()));
    }
}

void MainWindow::Impl::importAnnotations()
{
    // Defence in depth, as commitPendingShape does: the action is only enabled
    // where annotations may be created, and an inactive service never writes,
    // so an import past it would be on screen and silently never saved.
    const auto editable = [this]() {
        return viewportWidget->isSlideOpen() && annotationPersistence != nullptr && annotationPersistence->isActive();
    };
    if (!editable() || undoStack == nullptr) {
        return;
    }

    const QString title = tr("Import Annotations");
    const QString chosen = QFileDialog::getOpenFileName(owner, title, annotationSettings->workspaceDirectory(),
                                                        tr("GeoJSON (*.geojson *.json);;All files (*)"));
    if (chosen.isEmpty()) {
        return;
    }
    const auto reportFailure = [this, &title, &chosen](const QString& detail) {
        QMessageBox::warning(owner, title,
                             tr("The annotations could not be read from:\n\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(chosen), detail));
    };

    // Everything is read and converted before anything is shown or changed: a
    // file that cannot be read changes nothing. On a worker thread, because a
    // QuPath detection export is 150 MB and takes seconds -- the window would
    // otherwise stop answering for all of them (TD-2). The reader touches
    // nothing but its arguments, and the dialog below is window-modal, so no
    // slide can close and no annotation change while it runs.
    struct ReadOutcome
    {
        QString fileError;
        core::GeoJsonImportResult result;
    };
    std::atomic<bool> cancel{false};
    const auto importedAt = std::chrono::system_clock::now();
    std::future<ReadOutcome> reading = std::async(std::launch::async, [&chosen, &cancel, importedAt]() {
        ReadOutcome outcome;
        QFile file(chosen);
        if (!file.open(QIODevice::ReadOnly)) {
            outcome.fileError = file.errorString();
            return outcome;
        }
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            outcome.fileError = file.errorString();
            return outcome;
        }
        file.close();
        outcome.result = core::geoJsonToAnnotations(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), importedAt, &cancel);
        return outcome;
    });
    waitWithCancel(owner, reading, tr("Reading %1...").arg(QFileInfo(chosen).fileName()), title, cancel);
    const ReadOutcome outcome = reading.get();
    if (!outcome.fileError.isEmpty()) {
        reportFailure(outcome.fileError);
        return;
    }
    const core::GeoJsonImportResult& result = outcome.result;
    if (result.cancelled) {
        // The user asked for it; nothing was changed and nothing needs saying.
        return;
    }
    if (!result.error.empty()) {
        reportFailure(QString::fromStdString(result.error));
        return;
    }

    app::ImportPlan plan = app::planImport(result.annotations, result.idSeeds, *annotations);
    const core::SlideInfo& info = viewportWidget->slideInfo();
    GeoJsonImportSummaryInput summary;
    summary.fileName = QFileInfo(chosen).fileName();
    summary.result = &result;
    summary.toAdd = static_cast<int>(plan.toAdd.size());
    summary.alreadyPresent = plan.alreadyPresent;
    summary.outsideScene = countOutsideScene(plan.toAdd, info.width, info.height);
    summary.openSlideId = viewportWidget->currentSlideId();
    summary.openScene = viewportWidget->currentSceneIndex();
    const QString text = geoJsonImportSummary(summary);

    if (plan.toAdd.empty()) {
        QMessageBox::information(owner, title, text);
        return;
    }
    QMessageBox confirm(QMessageBox::Question, title, text, QMessageBox::NoButton, owner);
    QPushButton* importButton = confirm.addButton(tr("&Import"), QMessageBox::AcceptRole);
    QPushButton* cancelButton = confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(cancelButton);
    confirm.exec();
    if (confirm.clickedButton() != importButton || !editable()) {
        return;
    }

    // The model changes first and the command is recorded after, as every
    // other edit here is: execute() runs only on redo. One insertMany, so one
    // annotationsBulkChanged -- one list rebuild, and the autosave the
    // persistence service already arms on that signal.
    const int count = static_cast<int>(plan.toAdd.size());
    annotations->clearSelection();
    annotations->insertMany(plan.toAdd);
    const QString what = count == 1 ? tr("Import 1 annotation") : tr("Import %1 annotations").arg(count);
    undoStack->push(app::AnnotationImportCommand::create(*annotations, std::move(plan.toAdd), what.toStdString()));
    const QString name = QFileInfo(chosen).fileName();
    statusBarManager->showTransientMessage(count == 1 ? tr("Imported 1 annotation from %1").arg(name)
                                                      : tr("Imported %1 annotations from %2").arg(count).arg(name));
}

void MainWindow::Impl::reportWorkspaceSwitch(const app::WorkspaceSwitchResult& result)
{
    if (result.write == core::SaveStatus::NotWritable || result.write == core::SaveStatus::Failed) {
        const QString path = QDir::toNativeSeparators(QString::fromStdString(result.writePath));
        if (result.flush == core::SaveStatus::NotWritable
            || result.flush == core::SaveStatus::Failed) {
            // Both folders refused the write, so the annotations are on screen
            // and nowhere else. Saying "they are still in the folder you
            // switched from" here would be false, and a transient line is the
            // wrong weight for it: this is the one case where closing the slide
            // loses clinical findings.
            QMessageBox::critical(
                owner, tr("Annotations are not saved anywhere"),
                tr("Neither the folder you switched from nor %1 could be written.\n\n"
                   "Your annotations are still open and are being retried, but they are "
                   "currently not saved in either folder. Choose a writable folder in "
                   "Preferences before closing the slide.")
                    .arg(path));
            return;
        }
        statusBarManager->showTransientMessage(
            tr("The annotations could not be written to %1. They are still in the folder you "
               "switched from, and the save is being retried.")
                .arg(path));
        return;
    }

    switch (result.choice) {
    case app::WorkspaceChoice::Merge:
        statusBarManager->showTransientMessage(tr("Merged: %1 added, %2 superseded, %3 kept.")
                                                   .arg(result.merge.adopted)
                                                   .arg(result.merge.superseded)
                                                   .arg(result.merge.kept));
        break;
    case app::WorkspaceChoice::Copy:
    case app::WorkspaceChoice::KeepCurrent:
        statusBarManager->showTransientMessage(tr("Annotations copied to the new folder."));
        break;
    case app::WorkspaceChoice::LoadNew:
        if (result.asked) {
            statusBarManager->showTransientMessage(
                tr("Now showing the new folder's annotations. The previous ones are unchanged where "
                   "they were."));
        }
        break;
    case app::WorkspaceChoice::Cancel:
        statusBarManager->showTransientMessage(tr("The annotation folder was not changed."));
        break;
    }
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;
    m_impl->overrideStore = std::make_unique<infra::QSettingsColorProfileOverrideStore>();
    m_impl->categoryStore = std::make_unique<infra::QSettingsAnnotationCategoryStore>();

    setWindowTitle(kApplicationTitle);
    setAcceptDrops(true);

    // Apply dark theme — do NOT style QMainWindow background directly,
    // as it can paint over the QOpenGLWidget (central widget) content on Qt 6.
    setStyleSheet(
        "QMenuBar { background: #333333; color: #CCCCCC; }"
        "QMenuBar::item:selected { background: #505050; }"
        "QMenu { background: #333333; color: #CCCCCC; border: 1px solid #555555; }"
        "QMenu::item:selected { background: #4A90D9; }");

    // Create viewport (central widget)
    m_impl->annotations = std::make_unique<app::AnnotationModel>();
    m_impl->viewportWidget = new ViewportWidget(*m_impl->annotations, this);
    setCentralWidget(m_impl->viewportWidget);

    // Create overlay widgets as children of the viewport
    m_impl->minimapWidget = new MinimapWidget(m_impl->viewportWidget);
    m_impl->minimapWidget->raise();

    m_impl->zoomIndicatorWidget = new ZoomIndicatorWidget(m_impl->viewportWidget);
    m_impl->zoomIndicatorWidget->raise();

    m_impl->ztNavigationWidget = new ZTNavigationWidget(m_impl->viewportWidget);
    m_impl->ztNavigationWidget->raise();

    m_impl->loadingOverlay = new LoadingOverlay(m_impl->viewportWidget);
    m_impl->loadingOverlay->raise();

    // Create status bar manager
    m_impl->statusBarManager = new StatusBarManager(this);
    m_impl->statusBarManager->setup(statusBar());

    // Create channel mixer dock widget. Set objectName so QMainWindow's
    // saveState/restoreState can place it correctly across sessions.
    m_impl->channelMixerPanel = new ChannelMixerPanel(this);
    m_impl->channelMixerPanel->setObjectName(QStringLiteral("ChannelMixerPanel"));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->channelMixerPanel);
    m_impl->channelMixerPanel->hide();

    // Create scene thumbnail dock widget (hidden by default)
    m_impl->sceneThumbnailPanel = new SceneThumbnailPanel(this);
    m_impl->sceneThumbnailPanel->setObjectName(QStringLiteral("SceneThumbnailPanel"));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->sceneThumbnailPanel);
    m_impl->sceneThumbnailPanel->hide();

    // Associated images panel: a second SceneThumbnailPanel instance dedicated
    // to the slide's auxiliary images (label, macro, …). Distinguished from
    // the scenes panel by its title and objectName so saveState/restoreState
    // can place each independently.
    m_impl->associatedImagesPanel = new SceneThumbnailPanel(this);
    m_impl->associatedImagesPanel->setObjectName(QStringLiteral("AssociatedImagesPanel"));
    m_impl->associatedImagesPanel->setWindowTitle(QStringLiteral("Associated Images"));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->associatedImagesPanel);
    m_impl->associatedImagesPanel->hide();

    // Create properties dock widget (hidden by default)
    m_impl->propertiesPanel = new SlidePropertiesPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_impl->propertiesPanel);
    m_impl->propertiesPanel->hide();

    // Create metadata dock widget (hidden by default)
    m_impl->metadataPanel = new MetadataPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_impl->metadataPanel);
    m_impl->metadataPanel->hide();

    // Annotation list dock (hidden by default)
    m_impl->annotationListPanel = new AnnotationListPanel(this);
    m_impl->annotationListPanel->setObjectName(QStringLiteral("AnnotationListPanel"));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->annotationListPanel);
    m_impl->annotationListPanel->hide();

    // Annotation properties dock (hidden by default)
    m_impl->annotationPropertiesPanel = new AnnotationPropertiesPanel(this);
    m_impl->annotationPropertiesPanel->setObjectName(QStringLiteral("AnnotationPropertiesPanel"));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->annotationPropertiesPanel);
    m_impl->annotationPropertiesPanel->hide();
    m_impl->annotationPropertiesPanel->setCategories(m_impl->categoryStore->categories());
    m_impl->annotationListPanel->setCategories(m_impl->categoryStore->categories());

    // Create actions and menus
    m_impl->createActions();
    m_impl->channelMixerToggleAction = m_impl->channelMixerPanel->toggleViewAction();
    m_impl->channelMixerToggleAction->setText("&Channels");
    m_impl->channelMixerToggleAction->setShortcut(QKeySequence("Ctrl+Shift+C"));
    m_impl->channelMixerToggleAction->setStatusTip("Toggle the channel mixer panel");
    m_impl->sceneThumbnailToggleAction = m_impl->sceneThumbnailPanel->toggleViewAction();
    m_impl->sceneThumbnailToggleAction->setText("&Scenes");
    m_impl->sceneThumbnailToggleAction->setShortcut(QKeySequence("Ctrl+Shift+T"));
    m_impl->sceneThumbnailToggleAction->setStatusTip("Toggle the scene thumbnail panel");
    m_impl->associatedImagesToggleAction = m_impl->associatedImagesPanel->toggleViewAction();
    m_impl->associatedImagesToggleAction->setText("&Associated Images");
    m_impl->associatedImagesToggleAction->setShortcut(QKeySequence("Ctrl+Shift+A"));
    m_impl->associatedImagesToggleAction->setStatusTip("Toggle the associated images panel");
    m_impl->propertiesToggleAction = m_impl->propertiesPanel->toggleViewAction();
    m_impl->propertiesToggleAction->setText("&Properties");
    m_impl->propertiesToggleAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    m_impl->propertiesToggleAction->setStatusTip("Toggle the slide properties panel");
    m_impl->metadataToggleAction = m_impl->metadataPanel->toggleViewAction();
    m_impl->metadataToggleAction->setText("Meta&data");
    m_impl->metadataToggleAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
    m_impl->metadataToggleAction->setStatusTip("Toggle the slide/scene metadata panel");

    m_impl->annotationListToggleAction = m_impl->annotationListPanel->toggleViewAction();
    m_impl->annotationListToggleAction->setText("Annotation &List");
    // QDockWidget::changeEvent copies every window-title change onto its
    // toggleViewAction. The list panel retitles itself ("Annotations (N)") on
    // each rebuild, which would replace this text and its Alt+L mnemonic, so
    // it is put back. Not redundant -- do not remove.
    connect(m_impl->annotationListPanel, &QDockWidget::windowTitleChanged, this,
            [this](const QString&) {
                m_impl->annotationListToggleAction->setText(tr("Annotation &List"));
            });
    m_impl->annotationListToggleAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    m_impl->annotationListToggleAction->setStatusTip("Toggle the annotation list panel");
    m_impl->annotationPropertiesToggleAction = m_impl->annotationPropertiesPanel->toggleViewAction();
    m_impl->annotationPropertiesToggleAction->setText("Annotation Prop&erties");
    m_impl->annotationPropertiesToggleAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    m_impl->annotationPropertiesToggleAction->setStatusTip("Toggle the annotation properties panel");
    m_impl->createMenus();
    m_impl->connectSignals();

    // Annotation persistence. The settings object is a member rather than a
    // static because QSettings() needs the organization and application names
    // that main() sets before this window is built.
    m_impl->annotationSettings = std::make_unique<AnnotationSettings>();
    m_impl->annotations->setDefaultAuthor(m_impl->annotationSettings->userName().toStdString());
    m_impl->buildAnnotationPersistence();

    m_impl->annotationListPanel->setAnnotationModel(m_impl->annotations.get());
    m_impl->annotationPropertiesPanel->setAnnotationModel(m_impl->annotations.get());

    connect(m_impl->annotationPropertiesPanel, &AnnotationPropertiesPanel::propertiesCommitted, this,
            [this](const std::string& id, const core::AnnotationProperties& properties,
                   const std::string& description) {
                // Same guard as the list panel's deleteRequested: a slide whose file could not be
                // parsed is never written to, so an edit here would show on screen and be lost.
                if (m_impl->annotationPersistence != nullptr && !m_impl->annotationPersistence->isActive()) {
                    m_impl->statusBarManager->showTransientMessage(
                        tr("These annotations are open read-only and cannot be changed."));
                    // The fields still show what was typed; snap them back to the stored values.
                    m_impl->annotationPropertiesPanel->revertToModel();
                    return;
                }
                if (m_impl->undoStack == nullptr) {
                    return;
                }
                app::setPropertiesWithUndo(*m_impl->annotations, *m_impl->undoStack, id, properties, description);
            });

    connect(m_impl->annotationListPanel, &AnnotationListPanel::annotationActivated, this,
            [this](const std::string& id) {
                if (m_impl->viewportWidget != nullptr) {
                    m_impl->viewportWidget->showAnnotation(id);
                }
            });

    connect(m_impl->annotationListPanel, &AnnotationListPanel::categoryUsed, this,
            [this](const std::string& category) {
                std::vector<std::string> list = m_impl->categoryStore->categories();
                // normalizeCategoryList is the one rule for "already in the list": appending and
                // re-normalising drops a case-insensitive duplicate, so there is no second
                // comparison to keep in step with it.
                std::vector<std::string> candidate = list;
                candidate.push_back(category);
                if (core::normalizeCategoryList(candidate).size() == list.size()) {
                    return;
                }
                list.push_back(category);
                m_impl->categoryStore->setCategories(list);
                const std::vector<std::string> stored = m_impl->categoryStore->categories();
                m_impl->annotationListPanel->setCategories(stored);
                m_impl->annotationPropertiesPanel->setCategories(stored);
            });

    connect(m_impl->annotationListPanel, &AnnotationListPanel::renameCategoryRequested, this,
            [this](const std::string& from) {
                // The scope, not a plain refusal: isActive() is false for a
                // slide whose annotation file could not be parsed AND for no
                // slide at all, so asking it alone told a user with an empty
                // viewer that "these annotations" were open read-only. The
                // category list is a global preference, so with nothing open
                // the rename is an edit of that preference and goes through,
                // retagging nothing -- which is what Remove from list, right
                // beside it in the same menu, has always done.
                const bool slideOpen =
                    m_impl->viewportWidget != nullptr && m_impl->viewportWidget->isSlideOpen();
                const bool editable = m_impl->annotationPersistence == nullptr
                    || m_impl->annotationPersistence->isActive();
                const CategoryRenameScope scope = categoryRenameScope(slideOpen, editable);
                if (scope == CategoryRenameScope::Refused) {
                    m_impl->statusBarManager->showTransientMessage(
                        tr("These annotations are open read-only and cannot be changed."));
                    return;
                }
                // Only the retag needs the stack; renaming the vocabulary does
                // not, and with no slide open there is nothing to record.
                if (scope == CategoryRenameScope::VocabularyAndAnnotations && m_impl->undoStack == nullptr) {
                    return;
                }
                bool accepted = false;
                const QString typed = QInputDialog::getText(
                    this, tr("Rename Category"), tr("New name for \"%1\":").arg(QString::fromStdString(from)),
                    QLineEdit::Normal, QString::fromStdString(from), &accepted);
                if (!accepted) {
                    return;
                }
                const std::string target = typed.trimmed().toStdString();
                // Checked here as well as in retagClassification, which also returns empty for it:
                // an empty result is indistinguishable from "nothing matched", and a rename that
                // silently does nothing reads as a bug.
                if (target.empty()) {
                    m_impl->statusBarManager->showTransientMessage(
                        tr("A category needs a name. Nothing was renamed."));
                    return;
                }
                // Core refuses this too, but an empty result reads as "nothing matched".
                if (core::isUnclassifiedCategory(target)) {
                    m_impl->statusBarManager->showTransientMessage(
                        tr("Unclassified is not a category. Nothing was renamed."));
                    return;
                }
                if (QString::compare(QString::fromStdString(target), QString::fromStdString(from),
                                     Qt::CaseInsensitive) == 0) {
                    m_impl->statusBarManager->showTransientMessage(
                        tr("The name is unchanged. Nothing was renamed."));
                    return;
                }

                // The command is recorded AFTER the model has changed, as everywhere else in this
                // project, so it is executed once here and then pushed. UndoRedoStack::push does
                // not execute.
                auto command = app::AnnotationRetagCommand::create(
                    *m_impl->annotations,
                    core::retagClassification(m_impl->annotations->annotations(), from, target,
                                              std::chrono::system_clock::now()),
                    tr("Rename Category").toStdString());
                const bool retagged = command != nullptr;
                if (retagged) {
                    command->execute();
                    m_impl->undoStack->push(std::move(command));
                }

                // Rename the entry in place so it keeps its position. normalizeCategoryList then
                // folds a case-insensitive duplicate, which is what makes renaming onto an existing
                // category a merge in the vocabulary as the retag has made it one in the data.
                // Deliberately NOT undoable: the list is a global application preference shared by
                // every slide, while the undo stack is per-slide and cleared on a slide change.
                // Putting the vocabulary on it would let an undo here silently revert a change
                // another slide is already using. Undo restores the annotations only.
                std::vector<std::string> list = m_impl->categoryStore->categories();
                for (std::string& entry : list) {
                    if (QString::compare(QString::fromStdString(entry), QString::fromStdString(from),
                                         Qt::CaseInsensitive) == 0) {
                        entry = target;
                    }
                }
                m_impl->categoryStore->setCategories(list);
                const std::vector<std::string> stored = m_impl->categoryStore->categories();
                m_impl->annotationListPanel->setCategories(stored);
                m_impl->annotationPropertiesPanel->setCategories(stored);

                // Counting continues under the new name without the user re-picking it.
                if (QString::compare(QString::fromStdString(m_impl->annotations->currentCategory()),
                                     QString::fromStdString(from), Qt::CaseInsensitive) == 0) {
                    m_impl->annotationListPanel->setCurrentCategory(target);
                }
                if (!retagged) {
                    // Two different facts, and the old wording stated the wrong
                    // one with nothing open: "no annotation on this slide"
                    // names a slide the user does not have.
                    m_impl->statusBarManager->showTransientMessage(
                        slideOpen
                            ? tr("No annotation on this slide carries \"%1\"; the list entry was renamed.")
                                  .arg(QString::fromStdString(from))
                            : tr("No slide is open; the list entry was renamed."));
                }
            });

    connect(m_impl->annotationListPanel, &AnnotationListPanel::removeCategoryRequested, this,
            [this](const std::string& category) {
                // Vocabulary only: no annotation is touched, and the current category is left alone
                // (a value may be current without being configured).
                std::vector<std::string> list = m_impl->categoryStore->categories();
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [&category](const std::string& entry) {
                                              return QString::compare(QString::fromStdString(entry),
                                                                      QString::fromStdString(category),
                                                                      Qt::CaseInsensitive) == 0;
                                          }),
                           list.end());
                m_impl->categoryStore->setCategories(list);
                const std::vector<std::string> stored = m_impl->categoryStore->categories();
                m_impl->annotationListPanel->setCategories(stored);
                m_impl->annotationPropertiesPanel->setCategories(stored);
            });

    connect(m_impl->annotationListPanel, &AnnotationListPanel::deleteRequested, this,
            [this](const std::string& id) {
                // Mirrors ViewportWidget's annotationsEditable(): a slide whose file could not be
                // parsed is never written to, so a delete here would be silently lost.
                if (m_impl->annotationPersistence != nullptr && !m_impl->annotationPersistence->isActive()) {
                    m_impl->statusBarManager->showTransientMessage(
                        tr("These annotations are open read-only and cannot be changed."));
                    return;
                }
                const core::Annotation* annotation = m_impl->annotations->find(id);
                if (annotation == nullptr || m_impl->undoStack == nullptr) {
                    return;
                }
                const QString what = tr("Delete %1").arg(shapeName(*annotation));
                app::deleteWithUndo(*m_impl->annotations, *m_impl->undoStack, id, what.toStdString());
            });

    // Load the configured default ICC profile (if any) now that the viewport
    // and the actions that reflect this setting both exist.
    applyDefaultColorProfile();

    // Position overlays
    m_impl->repositionOverlays();

    // Reposition overlays when viewport resizes
    m_impl->viewportWidget->installEventFilter(this);
    connect(m_impl->viewportWidget, &ViewportWidget::viewportChanged, this, [this]() {
        m_impl->repositionOverlays();
    });

    // Restore saved window geometry and dock layout. If no geometry was saved,
    // or the saved rectangle no longer fits any available display (monitor
    // disconnected, resolution changed, …), fall back to a default size
    // centred on the primary screen.
    QSettings settings;
    const QByteArray savedGeometry = settings.value("mainWindow/geometry").toByteArray();
    bool useDefault = savedGeometry.isEmpty() || !restoreGeometry(savedGeometry);
    if (!useDefault) {
        QRegion availableArea;
        const auto screens = QGuiApplication::screens();
        for (const QScreen* screen : screens) {
            availableArea += screen->availableGeometry();
        }
        if (!availableArea.contains(frameGeometry())) {
            useDefault = true;
        }
    }
    if (useDefault) {
        resize(1280, 800);
        if (QScreen* primary = QGuiApplication::primaryScreen()) {
            const QRect avail = primary->availableGeometry();
            move(avail.center() - QPoint(width() / 2, height() / 2));
        }
    }
    restoreState(settings.value("mainWindow/state").toByteArray());
}

MainWindow::~MainWindow()
{
    // m_impl dies before ~QMainWindow deletes this window's children, so
    // anything Qt-owned that holds the model, the persistence service or the
    // undo stack would outlive all three. The viewport holds the model by
    // reference and cannot be cut loose from it, so it is deleted here instead,
    // ahead of m_impl -- rather than relying on ~ViewportWidget never touching
    // what it was given, which is a fact about another file that nobody
    // changing it will think to come back and check.
    //
    // The window state is saved first. saveState() serialises the dock layout
    // by objectName, so a dock deleted before it is already out of the layout
    // and its position and visibility would never be written. The viewport is
    // the central widget, which saveState does not record, so its order does
    // not matter -- but any dock deleted below must come after the save.
    QSettings settings;
    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState());

    delete m_impl->viewportWidget;
    m_impl->viewportWidget = nullptr;

    delete m_impl->annotationListPanel;
    m_impl->annotationListPanel = nullptr;

    delete m_impl->annotationPropertiesPanel;
    m_impl->annotationPropertiesPanel = nullptr;
}

void MainWindow::openSlide(const std::string& path, const std::string& driverId)
{
    // A pending properties edit is committed from aboutToCloseSlide, which the
    // viewport's own openSlide() reaches through closeSlide().
    // Don't clear the scene/aux panels here — the slideOpened handler will
    // rebuild them when (and only when) the file actually changes. Clearing
    // eagerly would flash empty panels for the duration of the load.
    setWindowTitle(QStringLiteral("%1 - %2").arg(kApplicationTitle, QString::fromStdString(path)));
    m_impl->addToRecentFiles(path);

    // openSlide is async: it kicks off background work and returns immediately.
    // The scene panel is populated, the loading overlay is hidden, and other
    // post-open UI updates run from the slideOpened/loadingFinished signal handlers.
    m_impl->viewportWidget->openSlide(path, driverId);
}

void MainWindow::onColorManagementToggled(bool enabled)
{
    QSettings settings;
    settings.setValue(kColorManagementKey, enabled);

    m_impl->viewportWidget->setColorMode(enabled ? core::ColorMode::Managed
                                                 : core::ColorMode::Raw);
    m_impl->statusBarManager->setColorManaged(enabled);
    // The two scenes report different provenance -- the wrapper reports the
    // profile it bound, which for a supplied default is not what the file
    // carries -- so the panel is refreshed from the active scene, not from the
    // SlideInfo captured at open.
    auto* ctrl = m_impl->viewportWidget->controller();
    m_impl->propertiesPanel->setActiveColorProfile(
        m_impl->viewportWidget->activeColorProfileInfo(),
        m_impl->viewportWidget->slideInfo().colorProfileOrigin,
        m_impl->viewportWidget->slideInfo().displacedEmbeddedProfile,
        m_impl->viewportWidget->slideInfo().colorProfileInfo.description,
        ctrl ? ctrl->colorMode() : core::ColorMode::Raw);
}

// Checked here rather than left to fail at the next slide open: SlideIO
// treats an unusable profile as absence, so without this a setting or an
// override would appear to take and then quietly do nothing.
std::optional<QString> MainWindow::pickAndValidateIccProfile(const QString& dialogCaption,
                                                              const QString& messageBoxTitle)
{
    const QString path = QFileDialog::getOpenFileName(
        this, dialogCaption, QString(),
        tr("ICC profiles (*.icc *.icm);;All files (*)"));
    if (path.isEmpty()) {
        return std::nullopt;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, messageBoxTitle,
                             tr("Could not read %1.").arg(QDir::toNativeSeparators(path)));
        return std::nullopt;
    }
    const QByteArray raw = file.readAll();
    const std::vector<uint8_t> bytes(raw.begin(), raw.end());

    const core::IccHeaderSummary summary = core::inspectIccHeader(bytes);
    if (!summary.plausible) {
        QMessageBox::warning(this, messageBoxTitle,
                             tr("%1 is not a valid ICC profile.")
                                 .arg(QDir::toNativeSeparators(path)));
        return std::nullopt;
    }
    if (summary.dataSpace != "RGB ") {
        QMessageBox::warning(
            this, messageBoxTitle,
            tr("%1 describes %2 data. Color management needs an RGB profile.")
                .arg(QDir::toNativeSeparators(path),
                     QString::fromStdString(summary.dataSpace).trimmed()));
        return std::nullopt;
    }

    return path;
}

void MainWindow::onSetDefaultColorProfile()
{
    const std::optional<QString> path = pickAndValidateIccProfile(
        tr("Select Default ICC Profile"), tr("Default ICC Profile"));
    if (!path) {
        return;
    }

    QSettings settings;
    settings.setValue(kDefaultProfileKey, *path);
    applyDefaultColorProfile();
    warnDefaultProfileAppliesToNewSlides();
}

void MainWindow::onClearDefaultColorProfile()
{
    QSettings settings;
    settings.remove(kDefaultProfileKey);
    applyDefaultColorProfile();
    m_impl->statusBarManager->showTransientMessage(QString::fromStdString(
        clearedDefaultProfileMessage(!m_impl->viewportWidget->currentFilePath().empty())));
}

// The default profile is only consumed where an adapter is constructed, so a
// slide that is already open keeps the binding it was opened with -- including
// the "no profile at all" binding whose menu tooltip says no default is set.
// Rebuilding the open slide's scenes in place is a larger change; saying so
// plainly is the honest alternative to leaving the user with a stale view and
// a tooltip that has become untrue.
void MainWindow::warnDefaultProfileAppliesToNewSlides()
{
    if (m_impl->viewportWidget->currentFilePath().empty()) {
        return;
    }
    QMessageBox::information(
        this, tr("Default ICC Profile"),
        tr("The default color profile applies to slides opened from now on. "
           "Reopen the current slide for the change to take effect on it."));
}

// Reads the configured profile from disk and hands its bytes to the viewport,
// which caches them. QSettings stores the path, but the bytes are read here
// only -- at construction and whenever the setting changes -- so replacing the
// file on disk has no effect until the app restarts or the user re-picks it.
void MainWindow::applyDefaultColorProfile()
{
    QSettings settings;
    const QString path = settings.value(kDefaultProfileKey).toString();
    std::vector<uint8_t> bytes;
    m_impl->defaultProfileProblem.clear();

    if (!path.isEmpty()) {
        QFile file(path);
        const bool readable = file.open(QIODevice::ReadOnly);
        if (readable) {
            const QByteArray raw = file.readAll();
            bytes.assign(raw.begin(), raw.end());
        }

        // Revalidated on every load, not only when the user picked the file.
        // The setting stores a path, so the file can be truncated, replaced or
        // deleted afterwards. Handing unusable bytes to SlideIO makes it report
        // that the *slide* embeds no profile -- true, and exactly why the
        // default was being consulted -- so the user is told their slide is at
        // fault for a problem in their own setting. Drop the bytes and keep the
        // real reason to show instead.
        const core::DefaultProfileStatus status =
            core::classifyDefaultProfile(readable, core::inspectIccHeader(bytes));
        if (status != core::DefaultProfileStatus::Ok) {
            m_impl->defaultProfileProblem =
                core::defaultProfileProblemText(status, QDir::toNativeSeparators(path).toStdString());
            bytes.clear();
            spdlog::warn("MainWindow: {}", m_impl->defaultProfileProblem);
        }
    }

    m_impl->clearDefaultProfileAction->setEnabled(!path.isEmpty());

    m_impl->defaultProfileBytes = std::move(bytes);
    applyColorProfilePolicy();
}

void MainWindow::applyColorProfilePolicy()
{
    ColorProfilePolicy policy;
    policy.defaultBytes = m_impl->defaultProfileBytes;
    for (const auto& entry : m_impl->overrideStore->all()) {
        policy.overridePathsBySlideId[entry.slideId] = entry.profilePath;
    }
    m_impl->viewportWidget->setColorProfilePolicy(std::move(policy));
}

std::string MainWindow::currentSlideId() const
{
    // The background open worker already computed this (and from the exact
    // same inputs this function used to redo the work with); re-deriving it
    // here meant a second slideContentSize() walk on the UI thread on every
    // open, which for a DICOM study is a full recursive directory scan. See
    // ViewportWidget::currentSlideId() for the empty-string contract: no slide
    // open, or the open slide could not be identified -- both unchanged here.
    return m_impl->viewportWidget->currentSlideId();
}

void MainWindow::onSetSlideColorProfile()
{
    const std::string slideId = currentSlideId();
    if (slideId.empty()) {
        QMessageBox::warning(this, tr("Slide ICC Profile"),
                             tr("This slide could not be identified, so a profile "
                                "cannot be remembered for it."));
        return;
    }

    const std::optional<QString> pickedPath = pickAndValidateIccProfile(
        tr("Select ICC Profile for This Slide"), tr("Slide ICC Profile"));
    if (!pickedPath) {
        return;
    }
    const QString& path = *pickedPath;

    // An override displaces whatever the slide carries. When the slide carries
    // its own characterisation, say so before replacing it: the result is a
    // slide displayed through a profile its scanner did not produce.
    const core::SlideInfo& info = m_impl->viewportWidget->slideInfo();
    const bool displaces = info.colorProfileInfo.present;
    if (displaces) {
        const QString embedded = QString::fromStdString(info.colorProfileInfo.description);
        const auto answer = QMessageBox::question(
            this, tr("Slide ICC Profile"),
            tr("This slide embeds its own color profile (%1). Overriding it displays "
               "the slide through a profile the scanner did not produce.\n\nContinue?")
                .arg(embedded.isEmpty() ? tr("unnamed") : embedded),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    core::ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = path.toStdString();
    entry.slideDisplayName =
        QFileInfo(QString::fromStdString(m_impl->viewportWidget->currentFilePath()))
            .fileName().toStdString();
    entry.displacedEmbedded = displaces;
    m_impl->overrideStore->set(entry);

    applyColorProfilePolicy();

    // An override has no effect in Raw mode, so a menu item that visibly did
    // nothing would read as a bug. Turning colour management on is what the
    // user asked for in substance -- but not here, and not yet.
    //
    // setChecked() emits toggled() even on a disabled action, and
    // onColorManagementToggled writes the global preference to QSettings. At
    // this point nobody knows whether the nominated profile works: validation
    // at pick time inspects the ICC header only, and a structurally sound
    // header can still be refused by lcms2 at bind time. Enabling now would
    // flip the user's setting for every slide they open afterwards on the
    // strength of a profile that is about to fail -- and the slide-opened
    // handler, which syncs the action under a signal blocker, would then
    // uncheck the action without ever undoing the write.
    //
    // So record the intent against this slide and let the reopen below decide.
    m_impl->pendingColorManagementEnableSlideId = slideId;

    m_impl->viewportWidget->reopenCurrentScene();
}

void MainWindow::onClearSlideColorProfile()
{
    const std::string slideId = currentSlideId();
    if (slideId.empty()) {
        return;
    }

    m_impl->overrideStore->remove(slideId);
    applyColorProfilePolicy();
    // Colour management stays as the user left it; only the profile changes.
    m_impl->viewportWidget->reopenCurrentScene();
    m_impl->statusBarManager->showTransientMessage(
        QString::fromStdString(clearedSlideProfileMessage()));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_impl->viewportWidget && event->type() == QEvent::Resize) {
        m_impl->repositionOverlays();
    }
    return QMainWindow::eventFilter(watched, event);
}

namespace
{

// Returns the local path of the first dragged item the viewer can open, or an
// empty string if none qualify. Remote URLs are dropped here; which local paths
// qualify is firstOpenableSlidePath's decision.
QString firstSupportedSlidePath(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) return {};

    QStringList localPaths;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) {
            localPaths << url.toLocalFile();
        }
    }
    return firstOpenableSlidePath(localPaths);
}

} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!firstSupportedSlidePath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    // Calling ignore() makes the OS show the "no-drop" cursor while the
    // unsupported file is being dragged over the window.
    event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    // dragMoveEvent fires repeatedly while the cursor moves over the window;
    // re-confirm acceptance so the cursor stays correct on every platform.
    if (!firstSupportedSlidePath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QString localPath = firstSupportedSlidePath(event->mimeData());
    if (!localPath.isEmpty()) {
        openSlide(localPath.toStdString(), driverIdForPath(localPath).toStdString());
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dropEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // A pending properties edit is committed from aboutToCloseSlide, which
    // closeSlide() emits before it flushes.
    // Quitting with a slide open would otherwise lose up to a full autosave
    // debounce of work: ~ViewportWidget does not close the slide, so nothing
    // flushes. closeSlide() flushes before it empties the model, by design.
    m_impl->viewportWidget->closeSlide();
    QMainWindow::closeEvent(event);
}

} // namespace slideio::viewer::ui
