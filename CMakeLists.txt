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

# Main executable. Per-platform icon plumbing:
#   * Qt resource (.qrc) ships the PNG inside the exe so QApplication::setWindowIcon
#     can use it at runtime on every platform (window/title-bar/taskbar icon).
#   * Windows: app.rc references app.ico, embedded as a Win32 ICON resource so
#     Explorer/file-manager shows it on the .exe itself.
#   * macOS: app.icns is bundled as the app's icon via CFBundleIconFile.
set(_app_icon_qrc       "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc")
set(_app_icon_rc        "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
set(_app_icon_icns_path "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.icns")

set(_executable_sources src/main.cpp ${_app_icon_qrc})
if(WIN32 AND EXISTS "${_app_icon_rc}")
    list(APPEND _executable_sources "${_app_icon_rc}")
endif()
if(APPLE AND EXISTS "${_app_icon_icns_path}")
    list(APPEND _executable_sources "${_app_icon_icns_path}")
endif()

add_executable(slideio-viewer WIN32 MACOSX_BUNDLE ${_executable_sources})
target_link_libraries(slideio-viewer PRIVATE slideio-viewer-ui spdlog::spdlog)

if(APPLE)
    # Dev-only: keep link-time dylib directories (conan cache, SlideIO install/lib)
    # in the installed binary's rpath, and add SlideIO install/bin where transitive
    # deps like libglog.*.dylib live. For a distributable build, copy dylibs/frameworks
    # into .app/Contents/Frameworks and rewrite install names instead.
    set_target_properties(slideio-viewer PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$<IF:$<CONFIG:Debug>,${SLIDEIO_ROOT}/debug/bin,${SLIDEIO_ROOT}/release/bin>")
endif()

if(APPLE AND EXISTS "${_app_icon_icns_path}")
    set_source_files_properties("${_app_icon_icns_path}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(slideio-viewer PROPERTIES
        MACOSX_BUNDLE_ICON_FILE app.icns
        MACOSX_BUNDLE_BUNDLE_NAME "SlideIO Viewer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.slideio.viewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

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

# Install into the project tree by default. Combined with the per-config
# subdirectories below, that yields:
#   build/install/release/bin/  (cmake --install ... --config Release)
#   build/install/debug/bin/    (cmake --install ... --config Debug)
# Pass `cmake --install ... --prefix <path>` to override at install time.
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/build/install"
    CACHE PATH "Install prefix" FORCE)

# Per-config install layout. Generator expressions are evaluated at install
# time, so this works for both single-config (Make/Ninja) and multi-config
# (Visual Studio/Xcode) generators.
set(_install_bin     "$<LOWER_CASE:$<CONFIG>>/bin")
set(_install_plugins "$<LOWER_CASE:$<CONFIG>>/plugins")

# Executable
install(TARGETS slideio-viewer
    RUNTIME DESTINATION ${_install_bin}
    BUNDLE DESTINATION ${_install_bin}
)

# SlideIO DLLs
if(WIN32)
    install(DIRECTORY "${SLIDEIO_ROOT}/release/bin/"
        DESTINATION ${_install_bin}
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        FILES_MATCHING PATTERN "*.dll"
    )
    install(DIRECTORY "${SLIDEIO_ROOT}/debug/bin/"
        DESTINATION ${_install_bin}
        CONFIGURATIONS Debug
        FILES_MATCHING PATTERN "*.dll"
    )
endif()

# Qt runtime deployment — copy DLLs and plugins per config.
# Conan provides separate Qt packages for Release and Debug builds:
#   Release: contains Qt6Core.dll (no 'd' suffix)
#   Debug:   contains Qt6Cored.dll
# We locate each by its distinctive Core DLL.
function(_find_qt_bin_dir _dll_name _out_var)
    file(GLOB_RECURSE _candidates "d:/conan2/p/b/qt*/p/bin/${_dll_name}")
    foreach(_candidate ${_candidates})
        get_filename_component(_dir "${_candidate}" DIRECTORY)
        set(${_out_var} "${_dir}" PARENT_SCOPE)
        return()
    endforeach()
    foreach(_prefix ${CMAKE_PREFIX_PATH})
        if(EXISTS "${_prefix}/../bin/${_dll_name}")
            get_filename_component(_dir "${_prefix}/../bin" ABSOLUTE)
            set(${_out_var} "${_dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${_out_var} "" PARENT_SCOPE)
endfunction()

_find_qt_bin_dir("Qt6Core.dll"  _qt_release_bin_dir)
_find_qt_bin_dir("Qt6Cored.dll" _qt_debug_bin_dir)

set(_qt_modules Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets Qt6Network)

if(_qt_release_bin_dir)
    get_filename_component(_qt_release_root "${_qt_release_bin_dir}" DIRECTORY)
    message(STATUS "Qt Release bin dir: ${_qt_release_bin_dir}")
    foreach(_mod ${_qt_modules})
        install(FILES "${_qt_release_bin_dir}/${_mod}.dll"
            DESTINATION ${_install_bin}
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel
            OPTIONAL
        )
    endforeach()
    install(FILES "${_qt_release_bin_dir}/D3Dcompiler_47.dll"
        DESTINATION ${_install_bin}
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        OPTIONAL)
    install(DIRECTORY "${_qt_release_root}/plugins/platforms"
            "${_qt_release_root}/plugins/styles"
            "${_qt_release_root}/plugins/imageformats"
        DESTINATION ${_install_plugins}
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        OPTIONAL)
endif()

if(_qt_debug_bin_dir)
    get_filename_component(_qt_debug_root "${_qt_debug_bin_dir}" DIRECTORY)
    message(STATUS "Qt Debug bin dir: ${_qt_debug_bin_dir}")
    foreach(_mod ${_qt_modules})
        install(FILES "${_qt_debug_bin_dir}/${_mod}d.dll"
            DESTINATION ${_install_bin}
            CONFIGURATIONS Debug
            OPTIONAL
        )
    endforeach()
    install(FILES "${_qt_debug_bin_dir}/D3Dcompiler_47.dll"
        DESTINATION ${_install_bin}
        CONFIGURATIONS Debug
        OPTIONAL)
    install(DIRECTORY "${_qt_debug_root}/plugins/platforms"
            "${_qt_debug_root}/plugins/styles"
            "${_qt_debug_root}/plugins/imageformats"
        DESTINATION ${_install_plugins}
        CONFIGURATIONS Debug
        OPTIONAL)
endif()

if(_qt_release_bin_dir OR _qt_debug_bin_dir)
    # qt.conf tells Qt where to find plugins (../plugins is relative to bin/)
    install(CODE [[
        string(TOLOWER "${CMAKE_INSTALL_CONFIG_NAME}" _config_lower)
        file(WRITE "${CMAKE_INSTALL_PREFIX}/${_config_lower}/bin/qt.conf"
            "[Paths]\nPlugins=../plugins\n")
    ]])
endif()

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
