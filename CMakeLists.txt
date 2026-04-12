cmake_minimum_required(VERSION 3.20)
project(slideio-viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(SLIDEIO_VIEWER_BUILD_TESTS "Build unit tests" ON)
option(SLIDEIO_VIEWER_BUILD_BENCHMARKS "Build performance benchmarks" OFF)

# Module path for custom find modules and compiler warnings
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# SlideIO from pre-built install
if(NOT SLIDEIO_ROOT)
    set(SLIDEIO_ROOT "D:/Projects/slideio/slideio/build/install" CACHE PATH "SlideIO install directory")
endif()
# Normalize path to forward slashes to avoid CMake escape issues
file(TO_CMAKE_PATH "${SLIDEIO_ROOT}" SLIDEIO_ROOT)
find_package(SlideIO REQUIRED)

# Conan-provided dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

# Qt settings
set(CMAKE_AUTOMOC ON)

# Layer libraries (strict dependency order)
add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(src/infra)
add_subdirectory(src/ui)

# Main executable
add_executable(slideio-viewer WIN32 src/main.cpp)
target_link_libraries(slideio-viewer PRIVATE slideio-viewer-ui spdlog::spdlog)

# Copy SlideIO DLLs to output directory on Windows (config-aware)
if(WIN32)
    add_custom_command(TARGET slideio-viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D "CONFIG=$<CONFIG>"
            -D "SLIDEIO_ROOT=${SLIDEIO_ROOT}"
            -D "OUTPUT_DIR=$<TARGET_FILE_DIR:slideio-viewer>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopySlideIODlls.cmake"
        COMMENT "Copying SlideIO DLLs for $<CONFIG>..."
    )
endif()

# ── Install rules ──────────────────────────────────────────────────────────────

include(GNUInstallDirs)

# Executable
install(TARGETS slideio-viewer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# SlideIO DLLs
if(WIN32)
    install(DIRECTORY "${SLIDEIO_ROOT}/release/bin/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        FILES_MATCHING PATTERN "*.dll"
    )
    install(DIRECTORY "${SLIDEIO_ROOT}/debug/bin/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Debug
        FILES_MATCHING PATTERN "*.dll"
    )
endif()

# Qt runtime deployment (copies Qt DLLs, plugins, and platforms)
# Requires Qt 6.5+ which ships qt_generate_deploy_app_script
# Note: Conan Qt packages are typically Release-only, so windeployqt
# fails for Debug configs (missing debug Qt DLLs like Qt6Cored.dll).
# We skip Qt deployment for Debug; run Debug builds from the build dir instead.
qt_generate_deploy_app_script(
    TARGET slideio-viewer
    OUTPUT_SCRIPT _qt_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    NO_TRANSLATIONS
)
# Create a wrapper that skips deployment for Debug config
set(_qt_deploy_wrapper "${CMAKE_CURRENT_BINARY_DIR}/qt_deploy_wrapper-$<CONFIG>.cmake")
file(GENERATE OUTPUT "${_qt_deploy_wrapper}" CONTENT
"if(NOT \"$<CONFIG>\" STREQUAL \"Debug\")
    include(\"${_qt_deploy_script}\")
else()
    message(STATUS \"Skipping Qt deployment for Debug config (Conan Qt is release-only)\")
endif()
")
install(SCRIPT "${_qt_deploy_wrapper}")

# ── Packaging (CPack) ──────────────────────────────────────────────────────────

set(CPACK_PACKAGE_NAME "SlideIO Viewer")
set(CPACK_PACKAGE_VENDOR "SlideIO")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform pathology slide viewer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SlideIO Viewer")
set(CPACK_PACKAGE_EXECUTABLES "slideio-viewer" "SlideIO Viewer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(WIN32)
    # NSIS installer
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "SlideIO Viewer ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "SlideIO Viewer")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\SlideIO Viewer.lnk' '$INSTDIR\\\\bin\\\\slideio-viewer.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\SlideIO Viewer.lnk'"
    )
    # File associations for common slide formats
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKCU 'Software\\\\Classes\\\\.svs\\\\OpenWithProgids' 'SlideIOViewer.svs' ''
        WriteRegStr HKCU 'Software\\\\Classes\\\\.ndpi\\\\OpenWithProgids' 'SlideIOViewer.ndpi' ''
        WriteRegStr HKCU 'Software\\\\Classes\\\\.czi\\\\OpenWithProgids' 'SlideIOViewer.czi' ''
        WriteRegStr HKCU 'Software\\\\Classes\\\\SlideIOViewer.svs\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\slideio-viewer.exe\\\" \\\"%1\\\"'
        WriteRegStr HKCU 'Software\\\\Classes\\\\SlideIOViewer.ndpi\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\slideio-viewer.exe\\\" \\\"%1\\\"'
        WriteRegStr HKCU 'Software\\\\Classes\\\\SlideIOViewer.czi\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\slideio-viewer.exe\\\" \\\"%1\\\"'
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKCU 'Software\\\\Classes\\\\SlideIOViewer.svs'
        DeleteRegKey HKCU 'Software\\\\Classes\\\\SlideIOViewer.ndpi'
        DeleteRegKey HKCU 'Software\\\\Classes\\\\SlideIOViewer.czi'
    ")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

# Tests
if(SLIDEIO_VIEWER_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 REQUIRED)
    add_subdirectory(tests)
endif()

# Benchmarks
if(SLIDEIO_VIEWER_BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
    add_subdirectory(tests/benchmarks)
endif()
