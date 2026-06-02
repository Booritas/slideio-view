#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSurfaceFormat>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/MainWindow.h"
#include "slideio/viewer/ui/LogSettings.h"

#include <cstdlib>
#include <optional>
#include <string>

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
    // main logger's sinks but is OFF by default. Its level is decided below,
    // once QApplication exists, from SLIDEIO_PERF_LOG (env wins) or the
    // persisted setting (Tools -> Settings -> Logs).
    auto perfLogger = std::make_shared<spdlog::logger>("perf",
        spdlog::sinks_init_list{fileSink, consoleSink});
    perfLogger->set_level(spdlog::level::off);
    spdlog::register_logger(perfLogger);

    spdlog::info("SlideIO Viewer starting, log file: {}", logPath.toStdString());

    // Request OpenGL 3.3 Core Profile
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // Apply persisted log settings now that QSettings (org/app identity set
    // above) is safe to use. App level: persisted value, else the debug default.
    // Perf level: SLIDEIO_PERF_LOG wins when set; otherwise the persisted flag.
    {
        using namespace slideio::viewer::ui;
        applyAppLevel(levelFromString(readAppLevelSetting()));

        const char* perfEnv = std::getenv("SLIDEIO_PERF_LOG");
        std::optional<std::string> perfEnvOpt;
        if (perfEnv) {
            perfEnvOpt = std::string(perfEnv);
        }
        const bool perfEnabled = resolveStartupPerfEnabled(perfEnvOpt, readPerfEnabledSetting());
        applyPerfEnabled(perfEnabled);
        if (perfEnabled) {
            spdlog::info("Performance logging ENABLED");
        }
    }

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
