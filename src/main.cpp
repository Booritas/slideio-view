#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSurfaceFormat>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <string>

#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/MainWindow.h"

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

    // Dedicated perf logger for tile-loading / rendering timing. Shares the
    // same sinks as the main logger but is OFF by default — only emits when
    // SLIDEIO_PERF_LOG is set to a non-empty, non-"0" value. Keeps normal runs
    // free of the verbose per-tile/per-frame timing output.
    auto perfLogger = std::make_shared<spdlog::logger>("perf",
        spdlog::sinks_init_list{fileSink, consoleSink});
    const char* perfEnv = std::getenv("SLIDEIO_PERF_LOG");
    const bool perfOn = perfEnv && perfEnv[0] != '\0' && std::string(perfEnv) != "0";
    perfLogger->set_level(perfOn ? spdlog::level::trace : spdlog::level::off);
    perfLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(perfLogger);
    if (perfOn)
    {
        spdlog::info("Performance logging ENABLED (SLIDEIO_PERF_LOG set)");
    }

    spdlog::info("SlideIO Viewer starting, log file: {}", logPath.toStdString());

    // Request OpenGL 3.3 Core Profile
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    slideio::viewer::ui::MainWindow mainWindow;
    mainWindow.show();

    if (argc > 1) {
        mainWindow.openSlide(argv[1]);
    }

    spdlog::info("Entering event loop");
    int result = app.exec();
    spdlog::info("SlideIO Viewer exiting with code {}", result);
    return result;
}
