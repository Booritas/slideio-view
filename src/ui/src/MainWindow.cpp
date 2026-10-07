#include "slideio/viewer/ui/MainWindow.h"
#include "slideio/viewer/core/ColorManagement.h"
#include "slideio/viewer/core/SlideId.h"
#include "slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"
#include "slideio/viewer/ui/AboutDialog.h"
#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/AssociatedImageWindow.h"
#include "slideio/viewer/ui/ChannelMixerPanel.h"
#include "slideio/viewer/ui/ColorProfilePolicy.h"
#include "slideio/viewer/ui/DriverFilters.h"
#include "slideio/viewer/ui/LoadingOverlay.h"
#include "slideio/viewer/ui/SceneThumbnailPanel.h"
#include "slideio/viewer/ui/SlidePropertiesPanel.h"
#include "slideio/viewer/ui/SlideProfilesDialog.h"
#include "slideio/viewer/ui/MetadataPanel.h"
#include "slideio/viewer/ui/MinimapWidget.h"
#include "slideio/viewer/ui/StatusBarManager.h"
#include "slideio/viewer/ui/ViewportController.h"
#include "slideio/viewer/ui/ViewportWidget.h"
#include "slideio/viewer/ui/ZTNavigationWidget.h"
#include "slideio/viewer/ui/ZoomIndicatorWidget.h"

#include <spdlog/spdlog.h>

#include <QAction>
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
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QRegion>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
constexpr int kMaxRecentFiles = 10;
const QString kRecentFilesKey = "recentFiles";
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
} // anonymous namespace

namespace slideio::viewer::ui
{

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
    QAction* colorManagementAction = nullptr;
    QAction* setDefaultProfileAction = nullptr;
    QAction* clearDefaultProfileAction = nullptr;
    QAction* setSlideProfileAction = nullptr;
    QAction* clearSlideProfileAction = nullptr;
    QAction* manageSlideProfilesAction = nullptr;
    QAction* aboutAction = nullptr;

    // Why the configured default ICC profile cannot be used, if it cannot.
    // Empty when none is configured or it is fine. Shown instead of the
    // slide-centric reason, which would otherwise blame the slide.
    std::string defaultProfileProblem;

    // The validated default profile bytes, cached so applyColorProfilePolicy
    // can rebuild the whole policy without re-reading the file.
    std::vector<uint8_t> defaultProfileBytes;

    std::unique_ptr<infra::QSettingsColorProfileOverrideStore> overrideStore;

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
        fileMenu->addAction(openLogAction);
        fileMenu->addSeparator();
        fileMenu->addAction(exitAction);

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
        viewMenu->addSeparator();
        viewMenu->addAction(colorManagementAction);
        viewMenu->addSeparator();
        viewMenu->addAction(setDefaultProfileAction);
        viewMenu->addAction(clearDefaultProfileAction);
        viewMenu->addSeparator();
        viewMenu->addAction(setSlideProfileAction);
        viewMenu->addAction(clearSlideProfileAction);
        viewMenu->addAction(manageSlideProfilesAction);

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
            viewportWidget->closeSlide();
            closeAction->setEnabled(false);
            sceneThumbnailPanel->clear();
            associatedImagesPanel->clear();
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
                QMessageBox::critical(owner, "Error", QString::fromStdString(message));
            });

        QObject::connect(viewportWidget, &ViewportWidget::slideClosed, owner, [this]() {
            closeAction->setEnabled(false);
            minimapWidget->clearThumbnail();
            statusBarManager->updateCursorPosition(0.0, 0.0);
            statusBarManager->updateMagnification(1.0, 0.0);
            zoomIndicatorWidget->setResolution(0.0);
            channelMixerPanel->clearChannels();
            propertiesPanel->clear();
            metadataPanel->clear();

            colorManagementAction->setEnabled(false);
            setSlideProfileAction->setEnabled(false);
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
                loadingOverlay->start(displayName);
            });

        QObject::connect(viewportWidget, &ViewportWidget::loadingStatusChanged, owner,
            [this](const QString& text) {
                loadingOverlay->setStatus(text);
            });

        QObject::connect(viewportWidget, &ViewportWidget::loadingFinished, owner, [this]() {
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
        QStringList files = settings.value(kRecentFilesKey).toStringList();
        QString path = QString::fromStdString(filePath);

        files.removeAll(path);
        files.prepend(path);

        while (files.size() > kMaxRecentFiles) {
            files.removeLast();
        }

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
            QString label = QString("&%1  %2").arg(i + 1).arg(QFileInfo(filePath).fileName());
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;
    m_impl->overrideStore = std::make_unique<infra::QSettingsColorProfileOverrideStore>();

    setWindowTitle("SlideIO Viewer");
    setAcceptDrops(true);

    // Apply dark theme — do NOT style QMainWindow background directly,
    // as it can paint over the QOpenGLWidget (central widget) content on Qt 6.
    setStyleSheet(
        "QMenuBar { background: #333333; color: #CCCCCC; }"
        "QMenuBar::item:selected { background: #505050; }"
        "QMenu { background: #333333; color: #CCCCCC; border: 1px solid #555555; }"
        "QMenu::item:selected { background: #4A90D9; }");

    // Create viewport (central widget)
    m_impl->viewportWidget = new ViewportWidget(this);
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
    m_impl->createMenus();
    m_impl->connectSignals();

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
    // Save window geometry and dock layout
    QSettings settings;
    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState());
}

void MainWindow::openSlide(const std::string& path, const std::string& driverId)
{
    // Don't clear the scene/aux panels here — the slideOpened handler will
    // rebuild them when (and only when) the file actually changes. Clearing
    // eagerly would flash empty panels for the duration of the load.
    setWindowTitle(QString("SlideIO Viewer - %1").arg(QString::fromStdString(path)));
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
    warnDefaultProfileAppliesToNewSlides();
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
    // Copied out rather than read back off `info` further down: the store write
    // and the policy rebuild below sit between here and the use, and a value
    // read once cannot be invalidated by what they touch.
    const core::ColorManagementAvailability availability = info.colorManagement;
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
    // nothing would read as a bug. Turning it on is what the user asked for in
    // substance.
    //
    // Guarded on the override being able to apply at all, because setChecked()
    // emits toggled() even on a disabled action and onColorManagementToggled
    // writes the global preference to QSettings. Without the guard, nominating
    // a profile on a slide that refuses ICC conversion would silently flip the
    // user's setting for every slide they opened afterwards. The enablement in
    // the slide-opened handler should already make this unreachable; it is
    // repeated here because the two are edited in different places.
    //
    // Deliberately not the action's own enabled state: on a slide that embeds
    // no profile, colour management is disabled until this very override
    // supplies one, and that is the case where turning it on matters most.
    if (core::slideProfileOverrideCanApply(availability)
        && !m_impl->colorManagementAction->isChecked()) {
        m_impl->colorManagementAction->setChecked(true);
    }

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

} // namespace slideio::viewer::ui
