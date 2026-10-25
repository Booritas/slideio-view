#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QStringList>
#include <QSurfaceFormat>

#include <utility>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/DriverFilters.h"
#include "slideio/viewer/ui/MainWindow.h"

namespace
{

void openInWindow(slideio::viewer::ui::MainWindow& window, const QString& path)
{
    // A DICOM study folder is passed through here too (shell "open with",
    // a path on the command line, a folder dropped on the Dock icon), so name
    // its driver explicitly rather than leaning on auto-detection, which only
    // recognises a directory by its lack of an extension.
    window.openSlide(path.toStdString(), slideio::viewer::ui::driverIdForPath(path).toStdString());
}

// macOS hands a file opened from Finder -- Open With, a double-click, a drop on
// the Dock icon -- to the application as a QFileOpenEvent, never in argv, and
// whether or not the application was already running. Qt sends it to the
// application object, so it is handled here rather than by an event filter,
// which would see every event in the application. Other platforms never send it.
class ViewerApplication : public QApplication
{
public:
    using QApplication::QApplication;

    // Opens anything that arrived before the window existed.
    void setMainWindow(slideio::viewer::ui::MainWindow* window)
    {
        m_window = window;
        const QStringList pending = std::exchange(m_pending, {});
        for (const QString& path : pending) {
            openInWindow(*m_window, path);
        }
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            // file() is empty for a URL that is not a local file.
            const QString path = static_cast<QFileOpenEvent*>(event)->file();
            if (!path.isEmpty()) {
                spdlog::info("File open request: {}", path.toStdString());
                if (m_window != nullptr) {
                    openInWindow(*m_window, path);
                } else {
                    m_pending << path;
                }
            }
            return true;
        }
        return QApplication::event(event);
    }

private:
    slideio::viewer::ui::MainWindow* m_window = nullptr;
    QStringList m_pending;
};

} // namespace

int main(int argc, char* argv[])
{
    // Set application identity BEFORE any QStandardPaths call so the log
    // location resolves to a per-user platform-standard directory.
    QCoreApplication::setOrganizationName("SlideIO");
    QCoreApplication::setOrganizationDomain("slideio.com");
    QCoreApplication::setApplicationName("SlideIO Viewer");

    // Resolve the log file path, ensure its directory exists, and set up
    // logging: rotating file sink + stderr.
    QString logPath = slideio::viewer::ui::logFilePath();
    QDir().mkpath(QFileInfo(logPath).absolutePath());

    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logPath.toStdString(), 10 * 1024 * 1024, 3);
    auto consoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("viewer",
        spdlog::sinks_init_list{fileSink, consoleSink});
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);

    spdlog::info("SlideIO Viewer starting, log file: {}", logPath.toStdString());

    // Request OpenGL 3.3 Core Profile
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    ViewerApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    // Without this a disabled menu item is pixel-identical to an enabled one.
    // The Windows 11 style draws menu text through the native theme and ignores
    // QPalette's Disabled roles -- setting them was measured to change nothing --
    // so a stylesheet is the only lever that works. It matters because disabling
    // an action is how this application says "not here": the annotation tools on
    // a read-only slide or an associated image, File > Close with no slide open.
    // Said in a colour nobody can see, that reads as the application being broken.
    //
    // Deliberately this narrow. The selector touches the one thing it names:
    // menu geometry, checkmarks, separators and every enabled item render
    // byte-identically with and without it.
    app.setStyleSheet(QStringLiteral("QMenu::item:disabled { color: #707070; }"));

    slideio::viewer::ui::MainWindow mainWindow;
    mainWindow.show();

    if (argc > 1) {
        openInWindow(mainWindow, QString::fromLocal8Bit(argv[1]));
    }
    app.setMainWindow(&mainWindow);

    spdlog::info("Entering event loop");
    int result = app.exec();
    spdlog::info("SlideIO Viewer exiting with code {}", result);
    return result;
}
