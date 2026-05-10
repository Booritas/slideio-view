#include "slideio/viewer/ui/MainWindow.h"
#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/AssociatedImageWindow.h"
#include "slideio/viewer/ui/ChannelMixerPanel.h"
#include "slideio/viewer/ui/DriverFilters.h"
#include "slideio/viewer/ui/LoadingOverlay.h"
#include "slideio/viewer/ui/SceneThumbnailPanel.h"
#include "slideio/viewer/ui/SlidePropertiesPanel.h"
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
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int kMaxRecentFiles = 10;
const QString kRecentFilesKey = "recentFiles";
} // anonymous namespace

namespace slideio::viewer::ui
{

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

    void createActions()
    {
        openAction = new QAction("&Open Slide...", owner);
        openAction->setShortcut(QKeySequence("Ctrl+O"));
        openAction->setStatusTip("Open a whole-slide image file");

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
    }

    void createMenus()
    {
        QMenu* fileMenu = owner->menuBar()->addMenu("&File");
        fileMenu->addAction(openAction);

        recentFilesMenu = fileMenu->addMenu("Recent &Files");
        updateRecentFilesMenu();

        fileMenu->addAction(closeAction);
        fileMenu->addSeparator();
        fileMenu->addAction(openLogAction);
        fileMenu->addSeparator();
        fileMenu->addAction(exitAction);

        QMenu* viewMenu = owner->menuBar()->addMenu("&View");
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
                if (info.numChannels > 1) {
                    channelMixerPanel->setChannels(info.channels);
                } else {
                    channelMixerPanel->clearChannels();
                }

                spdlog::info("MainWindow: setting Z/T: numZ={}, numT={}", info.numZSlices, info.numTFrames);
                ztNavigationWidget->setSliceFrameCounts(info.numZSlices, info.numTFrames);
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
                metadataPanel->setSlideInfo(info);
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
            statusBarManager->updateScaleBar(0.0);
            channelMixerPanel->clearChannels();
            propertiesPanel->clear();
            metadataPanel->clear();
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
                owner->openSlide(filePath.toStdString());
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

    // Position overlays
    m_impl->repositionOverlays();

    // Reposition overlays when viewport resizes
    m_impl->viewportWidget->installEventFilter(this);
    connect(m_impl->viewportWidget, &ViewportWidget::viewportChanged, this, [this]() {
        m_impl->repositionOverlays();
    });

    // Restore saved window geometry and dock layout
    QSettings settings;
    restoreGeometry(settings.value("mainWindow/geometry").toByteArray());
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_impl->viewportWidget && event->type() == QEvent::Resize) {
        m_impl->repositionOverlays();
    }
    return QMainWindow::eventFilter(watched, event);
}

namespace
{

// Returns the first URL in the mime data that points to a supported slide
// file, or an empty QUrl if none qualify. "Supported" means the file has a
// local path AND its extension matches one of the per-driver patterns from
// availableDriverFilters() (i.e., the same list shown in the Open dialog).
QUrl firstSupportedSlideUrl(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) return {};

    // Build a lower-case set of accepted extensions once. Skip the catch-all
    // "*" entry from "All Files" — drag/drop should only accept real image
    // formats so the cursor switches to "forbidden" for everything else.
    static const QSet<QString> kExtensions = []() {
        QSet<QString> exts;
        for (const auto& f : availableDriverFilters()) {
            for (const QString& e : f.extensions) {
                if (e != QStringLiteral("*")) {
                    exts.insert(e.toLower());
                }
            }
        }
        return exts;
    }();

    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QString name = QFileInfo(url.toLocalFile()).fileName().toLower();
        // Match against both single-suffix (".tif") and multi-suffix
        // (".ome.tif") patterns by checking every known extension.
        for (const QString& ext : kExtensions) {
            if (name.endsWith(QChar('.') + ext)) {
                return url;
            }
        }
    }
    return {};
}

} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (firstSupportedSlideUrl(event->mimeData()).isValid()) {
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
    if (firstSupportedSlideUrl(event->mimeData()).isValid()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QUrl url = firstSupportedSlideUrl(event->mimeData());
    if (url.isValid()) {
        openSlide(url.toLocalFile().toStdString());
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dropEvent(event);
}

} // namespace slideio::viewer::ui
