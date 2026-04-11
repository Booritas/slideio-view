#include "slideio/viewer/ui/MainWindow.h"
#include "slideio/viewer/ui/MinimapWidget.h"
#include "slideio/viewer/ui/StatusBarManager.h"
#include "slideio/viewer/ui/ViewportController.h"
#include "slideio/viewer/ui/ViewportWidget.h"
#include "slideio/viewer/ui/ZoomIndicatorWidget.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMimeData>
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

    // Recent files
    QMenu* recentFilesMenu = nullptr;
    QList<QAction*> recentFileActions;

    // Actions
    QAction* openAction = nullptr;
    QAction* closeAction = nullptr;
    QAction* exitAction = nullptr;
    QAction* zoomInAction = nullptr;
    QAction* zoomOutAction = nullptr;
    QAction* fitAction = nullptr;
    QAction* actualPixelsAction = nullptr;
    QAction* fullScreenAction = nullptr;
    QAction* minimapToggleAction = nullptr;

    void createActions()
    {
        openAction = new QAction("&Open Slide...", owner);
        openAction->setShortcut(QKeySequence("Ctrl+O"));
        openAction->setStatusTip("Open a whole-slide image file");

        closeAction = new QAction("&Close", owner);
        closeAction->setShortcut(QKeySequence("Ctrl+W"));
        closeAction->setStatusTip("Close the current slide");
        closeAction->setEnabled(false);

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
    }

    void connectSignals()
    {
        // File actions
        QObject::connect(openAction, &QAction::triggered, owner, [this]() {
            QString filePath = QFileDialog::getOpenFileName(
                owner, "Open Slide", QString(),
                "Slide Images (*.svs *.tiff *.tif *.ndpi *.scn *.mrxs *.bif *.vsi *.czi *.lif "
                "*.afi *.svslide *.dcm);;All Files (*)");
            if (!filePath.isEmpty()) {
                owner->openSlide(filePath.toStdString());
            }
        });

        QObject::connect(closeAction, &QAction::triggered, owner, [this]() {
            viewportWidget->closeSlide();
            closeAction->setEnabled(false);
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
            [this](const std::string& /*filePath*/) {
                closeAction->setEnabled(true);
                auto* ctrl = viewportWidget->controller();
                if (ctrl) {
                    const auto& vp = ctrl->viewport();
                    statusBarManager->updateMagnification(vp.scale(), ctrl->baseMagnification());
                    minimapWidget->setViewport(vp, ctrl->slideWidth(), ctrl->slideHeight());
                    zoomIndicatorWidget->setZoomLevel(vp.scale(), ctrl->baseMagnification());
                }
            });

        QObject::connect(viewportWidget, &ViewportWidget::thumbnailReady, owner,
            [this](const QImage& thumbnail) {
                minimapWidget->setThumbnail(thumbnail);
            });

        QObject::connect(viewportWidget, &ViewportWidget::slideClosed, owner, [this]() {
            closeAction->setEnabled(false);
            minimapWidget->clearThumbnail();
            statusBarManager->updateCursorPosition(0.0, 0.0);
            statusBarManager->updateMagnification(1.0, 0.0);
            statusBarManager->updateScaleBar(0.0);
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

    // Create status bar manager
    m_impl->statusBarManager = new StatusBarManager(this);
    m_impl->statusBarManager->setup(statusBar());

    // Create actions and menus
    m_impl->createActions();
    m_impl->createMenus();
    m_impl->connectSignals();

    // Position overlays
    m_impl->repositionOverlays();

    // Reposition overlays when viewport resizes
    m_impl->viewportWidget->installEventFilter(this);
    connect(m_impl->viewportWidget, &ViewportWidget::viewportChanged, this, [this]() {
        m_impl->repositionOverlays();
    });
}

MainWindow::~MainWindow()
{
    // Impl destroyed automatically; Qt children destroyed by Qt parent chain
}

void MainWindow::openSlide(const std::string& path)
{
    m_impl->viewportWidget->openSlide(path);
    setWindowTitle(QString("SlideIO Viewer - %1").arg(QString::fromStdString(path)));
    m_impl->addToRecentFiles(path);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_impl->viewportWidget && event->type() == QEvent::Resize) {
        m_impl->repositionOverlays();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            QString filePath = urls.first().toLocalFile();
            openSlide(filePath.toStdString());
            event->acceptProposedAction();
            return;
        }
    }
    QMainWindow::dropEvent(event);
}

} // namespace slideio::viewer::ui
