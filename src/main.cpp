#include <QApplication>
#include <QSurfaceFormat>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "slideio/viewer/ui/MainWindow.h"

int main(int argc, char* argv[])
{
    // Set up logging: file + stderr
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "slideio-viewer.log", 10 * 1024 * 1024, 3);
    auto consoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("viewer",
        spdlog::sinks_init_list{fileSink, consoleSink});
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);

    spdlog::info("SlideIO Viewer starting");

    // Request OpenGL 3.3 Core Profile
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("SlideIO Viewer");
    app.setOrganizationName("SlideIO");

    slideio::viewer::ui::MainWindow mainWindow;
    mainWindow.resize(1280, 800);
    mainWindow.show();

    if (argc > 1) {
        mainWindow.openSlide(argv[1]);
    }

    spdlog::info("Entering event loop");
    int result = app.exec();
    spdlog::info("SlideIO Viewer exiting with code {}", result);
    return result;
}
