cmake_minimum_required(VERSION 3.20)
project(slideio-viewer VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(SLIDEIO_VIEWER_BUILD_TESTS "Build unit tests" ON)
option(SLIDEIO_VIEWER_BUILD_BENCHMARKS "Build performance benchmarks" OFF)

# Module path for custom find modules and compiler warnings
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# SlideIO is built from source out of the extern/slideio submodule; build.sh
# and build-debug.sh drive that build and leave the result in the install tree
# below. Setting SLIDEIO_ROOT links against an install maintained elsewhere
# instead, and the submodule is then neither built nor needed.
if(NOT SLIDEIO_ROOT)
    set(SLIDEIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/slideio/build/install"
        CACHE PATH "SlideIO install directory")
endif()
# Normalize path to forward slashes to avoid CMake escape issues
file(TO_CMAKE_PATH "${SLIDEIO_ROOT}" SLIDEIO_ROOT)

# Without this, an unbuilt submodule reaches FindSlideIO.cmake and comes back
# as "Could NOT find SlideIO", which names neither the submodule nor the script
# that populates it. The two ways the tree can be missing need different
# answers, so say which one this is.
if(NOT EXISTS "${SLIDEIO_ROOT}/release")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/slideio/CMakeLists.txt")
        message(FATAL_ERROR
            "The extern/slideio submodule is not initialised. Run:\n"
            "  git submodule update --init --recursive")
    endif()
    message(FATAL_ERROR
        "No SlideIO install found at '${SLIDEIO_ROOT}'.\n"
        "Run ./build.sh (or ./build-debug.sh) to build the extern/slideio "
        "submodule, or set SLIDEIO_ROOT to an existing install directory.")
endif()

find_package(SlideIO REQUIRED)

# Conan-provided dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

# slideio_viewer_qt_bin_dir(), used by both the macOS and Windows deployment
# steps below to find Qt's own deployment tools.
include(DeployQt)

# Build stamp for the About dialog. Runs after the find_package calls above so
# it can read the versions they resolved.
include(BuildInfo)

# Qt settings. Without AUTORCC, resources/icons.qrc is listed as a source but
# never compiled, so ":/icons/app.png" resolves to nothing and every lookup of
# it silently yields a null pixmap.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
    # Self-contained bundle: dependent dylibs/frameworks are copied into
    # Contents/Frameworks/ at install time, located at runtime via
    # @executable_path/../Frameworks. macdeployqt handles Qt + plugins;
    # BundleUtilities::fixup_bundle handles SlideIO and transitive deps.
    set_target_properties(slideio-viewer PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if(UNIX AND NOT APPLE)
    # The package is self-contained under one prefix, so the executable looks for
    # its Qt and SlideIO libraries beside itself rather than in the system.
    set_target_properties(slideio-viewer PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib")
endif()

if(APPLE)

    slideio_viewer_qt_bin_dir(_qt_bin_dir)
    find_program(MACDEPLOYQT_EXECUTABLE
        NAMES macdeployqt
        PATHS "${_qt_bin_dir}"
        NO_DEFAULT_PATH)
    if(NOT MACDEPLOYQT_EXECUTABLE)
        find_program(MACDEPLOYQT_EXECUTABLE NAMES macdeployqt)
    endif()
    if(NOT MACDEPLOYQT_EXECUTABLE)
        # Not a warning: without it the bundle carries no Qt and fails on the
        # first machine that is not this one, which is the whole failure mode
        # this packaging work exists to remove.
        message(FATAL_ERROR "macdeployqt not found in '${_qt_bin_dir}' or on PATH; "
                            "the macOS bundle cannot be built without it.")
    endif()
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

# Install tree shape. The developer workflow wants build/install/release/bin and
# build/install/debug/bin side by side; a package wants bin/ at the prefix root,
# or the .deb ends up carrying /opt/slideio-viewer/release/bin/slideio-viewer.
#
# build.sh and package.sh each pass this explicitly, so a cached value from one
# can never leak into the other -- an option() default would not, since it only
# applies when the cache is empty.
set(SLIDEIO_VIEWER_INSTALL_LAYOUT "per-config" CACHE STRING
    "Install tree shape: per-config (release/bin, debug/bin) or flat (bin)")
set_property(CACHE SLIDEIO_VIEWER_INSTALL_LAYOUT PROPERTY STRINGS per-config flat)

if(SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "flat")
    set(_install_subdir "")
elseif(SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "per-config")
    # Generator expressions are evaluated at install time, so this works for both
    # single-config (Make/Ninja) and multi-config (Visual Studio/Xcode) generators.
    set(_install_subdir "$<LOWER_CASE:$<CONFIG>>/")
else()
    message(FATAL_ERROR
        "SLIDEIO_VIEWER_INSTALL_LAYOUT must be per-config or flat, "
        "got '${SLIDEIO_VIEWER_INSTALL_LAYOUT}'")
endif()

set(_install_bin     "${_install_subdir}bin")
set(_install_lib     "${_install_subdir}lib")
set(_install_plugins "${_install_subdir}plugins")

# The DragNDrop generator wants slideio-viewer.app at the root of the disk image,
# not inside a bin/ directory the user would have to open first.
if(APPLE AND SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "flat")
    set(_install_app ".")
else()
    set(_install_app "${_install_bin}")
endif()

# Executable
install(TARGETS slideio-viewer
    RUNTIME DESTINATION ${_install_bin}
    BUNDLE DESTINATION ${_install_app}
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

# Qt runtime deployment. windeployqt resolves the module set, the platform
# plugin and the style plugins from the binary itself, which does not drift the
# way a hand-maintained module list does.
include(DeployQt)
slideio_viewer_qt_bin_dir(_qt_bin_dir)
message(STATUS "Qt bin dir: ${_qt_bin_dir}")

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        PATHS "${_qt_bin_dir}"
        NO_DEFAULT_PATH)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR
            "windeployqt was not found in '${_qt_bin_dir}'. The Windows package "
            "cannot be built without it.")
    endif()
    # The MSVC runtime. windeployqt's --compiler-runtime is driven by heuristics
    # over VCINSTALLDIR that fired on neither the CI runner nor a developer box
    # with the redistributable plainly installed, so use CMake's own mechanism:
    # it resolves the DLLs from the toolchain that actually compiled the binary.
    # Without them the application fails on a machine without Visual Studio, and
    # nothing in CI can notice because every Windows runner has it.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
    include(InstallRequiredSystemLibraries)
    install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${_install_bin})

    install(CODE "set(WINDEPLOYQT_EXECUTABLE \"${WINDEPLOYQT_EXECUTABLE}\")")
    install(CODE "set(SLIDEIO_VIEWER_BIN_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_bin}\")")
    install(CODE "set(SLIDEIO_VIEWER_PLUGIN_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_plugins}\")")
    get_filename_component(_qt_root "${_qt_bin_dir}" DIRECTORY)
    install(CODE "set(SLIDEIO_VIEWER_QT_PLUGIN_SRC_DIR \"${_qt_root}/plugins\")")
    # The flat layout is the packaging layout: package.sh is what sets it. Some
    # checks matter only for an artifact someone else will download.
    if(SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "flat")
        install(CODE "set(SLIDEIO_VIEWER_PACKAGING ON)")
    endif()
    install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindowsQt.cmake")
endif()

if(UNIX AND NOT APPLE)
    # No deployment tool ships for Linux, so the module set is named explicitly.
    # These are the modules slideio-viewer-ui links, plus the DBus and XcbQpa
    # libraries the xcb platform plugin pulls in.
    get_filename_component(_qt_root "${_qt_bin_dir}" DIRECTORY)

    # Required: without any of these the application cannot reach its event loop,
    # so a missing one is worth stopping the build for.
    set(_qt_libs_required Core Gui Widgets OpenGL OpenGLWidgets XcbQpa)
    # Present only in some Qt builds. Conan's qt recipe defaults with_dbus=False
    # and this project does not override it, so libQt6DBus.so is simply absent --
    # a FATAL_ERROR on these would stop the Linux configure before a single
    # object was compiled, over a library nothing here needs.
    set(_qt_libs_optional Network DBus)

    foreach(_mod ${_qt_libs_required} ${_qt_libs_optional})
        file(GLOB _qt_lib_files "${_qt_root}/lib/libQt6${_mod}.so*")
        if(_qt_lib_files)
            install(FILES ${_qt_lib_files} DESTINATION ${_install_lib})
        elseif(_mod IN_LIST _qt_libs_required)
            message(FATAL_ERROR "Qt module libQt6${_mod}.so not found under ${_qt_root}/lib")
        else()
            message(STATUS "Qt module libQt6${_mod}.so not present; not packaging it")
        endif()
    endforeach()

    # The whole platforms directory, so libqoffscreen.so travels with libqxcb.so:
    # the package is smoke-tested headless, and a viewer that cannot start
    # without a display cannot be checked by anything but a human.
    set(_qt_plugins_required platforms xcbglintegrations)
    # qtbase builds style plugins only for APPLE and WIN32 -- Fusion is compiled
    # into QtWidgets on Linux -- so plugins/styles does not exist in a Linux Qt.
    set(_qt_plugins_optional styles imageformats)

    foreach(_plugin_dir ${_qt_plugins_required} ${_qt_plugins_optional})
        if(EXISTS "${_qt_root}/plugins/${_plugin_dir}")
            install(DIRECTORY "${_qt_root}/plugins/${_plugin_dir}" DESTINATION ${_install_plugins})
        elseif(_plugin_dir IN_LIST _qt_plugins_required)
            message(FATAL_ERROR "Qt plugin directory '${_plugin_dir}' not found under ${_qt_root}/plugins")
        else()
            message(STATUS "Qt plugin directory '${_plugin_dir}' not present; not packaging it")
        endif()
    endforeach()

    # SlideIO's own shared libraries. There is no Linux equivalent of the
    # CopySlideIODlls.cmake step the Windows build uses.
    #
    # lib/, not bin/: SlideIO installs RUNTIME to bin/ and LIBRARY to lib/, so on
    # Unix bin/ holds only its command line tools. Guarded, because an install()
    # whose source directory is missing copies nothing and says nothing -- the
    # OPTIONAL failure mode this whole change exists to remove.
    if(NOT EXISTS "${SLIDEIO_ROOT}/release/lib")
        message(FATAL_ERROR "SlideIO shared libraries not found at '${SLIDEIO_ROOT}/release/lib'")
    endif()
    install(DIRECTORY "${SLIDEIO_ROOT}/release/lib/"
        DESTINATION ${_install_lib}
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        FILES_MATCHING PATTERN "*.so*")
    install(DIRECTORY "${SLIDEIO_ROOT}/debug/lib/"
        DESTINATION ${_install_lib}
        CONFIGURATIONS Debug
        FILES_MATCHING PATTERN "*.so*")

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/slideio-viewer.desktop"
        DESTINATION "share/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.png"
        DESTINATION "share/icons/hicolor/256x256/apps"
        RENAME "slideio-viewer.png")
endif()

# macOS gets its qt.conf from macdeployqt, which knows where it put the plugins.
# This must stay after the deploy script above: windeployqt writes a qt.conf of
# its own when it relocates plugins, and ours has to be the one that survives.
if(WIN32 OR (UNIX AND NOT APPLE))
    # qt.conf tells Qt where to find plugins (../plugins is relative to bin/).
    # The directory is injected rather than recomputed here, so the layout option
    # stays the single place that decides the tree shape.
    install(CODE "set(SLIDEIO_VIEWER_BIN_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_bin}\")")
    install(CODE [[
        file(WRITE "${SLIDEIO_VIEWER_BIN_DIR}/qt.conf" "[Paths]\nPlugins=../plugins\n")
    ]])
endif()

# macOS: bundle Qt + SlideIO into the .app so it runs on a machine without
# conan or SlideIO installed. macdeployqt handles Qt frameworks/plugins;
# BundleUtilities::fixup_bundle walks the rest of the dependency graph.
if(APPLE)
    install(CODE "set(MACDEPLOYQT_EXECUTABLE \"${MACDEPLOYQT_EXECUTABLE}\")")
    install(CODE "set(SLIDEIO_ROOT \"${SLIDEIO_ROOT}\")")
    install(CODE "set(SLIDEIO_VIEWER_APP_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_app}\")")
    get_filename_component(_qt_root "${_qt_bin_dir}" DIRECTORY)
    install(CODE "set(SLIDEIO_VIEWER_QT_PLUGIN_SRC_DIR \"${_qt_root}/plugins\")")
    install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployMacOSBundle.cmake")
endif()

# ── Packaging (CPack) ──────────────────────────────────────────────────────────

set(CPACK_PACKAGE_NAME "SlideIO Viewer")
set(CPACK_PACKAGE_VENDOR "SlideIO")
# CPackDeb refuses to build without a maintainer, and the failure is a
# FATAL_ERROR in the DEB generator that also stops the TGZ beside it. Same
# address the SlideIO library's own packaging uses.
set(CPACK_PACKAGE_CONTACT "admin@slideio.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform pathology slide viewer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SlideIO Viewer")
set(CPACK_PACKAGE_EXECUTABLES "slideio-viewer" "SlideIO Viewer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Self-describing artifact names, so three platforms' packages can share one
# release page without a reader having to open them to tell them apart. The .deb
# is excluded: CPACK_DEBIAN_FILE_NAME DEB-DEFAULT gives it the Debian-native
# name, which tooling depends on.
if(WIN32)
    set(_package_platform "windows-x86_64")
elseif(APPLE)
    set(_package_platform "macos-arm64")
else()
    set(_package_platform "linux-x86_64")
endif()
set(CPACK_PACKAGE_FILE_NAME "slideio-viewer-${PROJECT_VERSION}-${_package_platform}")

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
    set(CPACK_DMG_VOLUME_NAME "SlideIO Viewer ${PROJECT_VERSION}")
    # Do not turn the licence into a click-through agreement on the disk image.
    # The DragNDrop generator attaches CPACK_RESOURCE_FILE_LICENSE as a software
    # licence agreement, and an image carrying one cannot be mounted without
    # someone to accept it: hdiutil reports "attach canceled" in any
    # non-interactive context, the smoke test included.
    #
    # Nothing is lost. The licence ships inside the bundle and the About dialog
    # has it on its own tab; a click-through on BSD-3-Clause buys the reader
    # nothing and costs everyone a dialog. The NSIS installer keeps its licence
    # page, where one is conventional.
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackLinuxOptions.cmake")
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
