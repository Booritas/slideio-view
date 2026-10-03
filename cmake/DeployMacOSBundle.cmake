# Bundles dependent Qt + SlideIO dylibs into the .app so it runs on a machine
# without conan or the SlideIO install on disk.
#
# Invoked from CMakeLists.txt via install(SCRIPT ...). Expects these variables
# (set by surrounding install(CODE) blocks):
#   MACDEPLOYQT_EXECUTABLE - macdeployqt path; empty/missing = skip Qt step
#   SLIDEIO_ROOT           - SlideIO install root (without per-config subdir)
#
# The SlideIO install layout already uses @rpath/lib*.dylib install names and
# inter-dep references, so we only need to copy them next to the executable
# under Contents/Frameworks/ — the @executable_path/../Frameworks rpath baked
# into the binary at link time then resolves them at launch.

# _cfg indexes SlideIO's own install tree further down, which has its own
# per-config layout unrelated to ours. The bundle location comes from
# SLIDEIO_VIEWER_APP_DIR, which CMakeLists.txt derives from the install layout.
string(TOLOWER "${CMAKE_INSTALL_CONFIG_NAME}" _cfg)
set(_bundle "${SLIDEIO_VIEWER_APP_DIR}/slideio-viewer.app")

if(NOT EXISTS "${_bundle}")
    message(FATAL_ERROR "DeployMacOSBundle: bundle not found at '${_bundle}'")
endif()

# Step 1: Qt frameworks/plugins/qt.conf via Qt's own deployment tool.
if(MACDEPLOYQT_EXECUTABLE AND EXISTS "${MACDEPLOYQT_EXECUTABLE}")
    message(STATUS "macdeployqt: ${MACDEPLOYQT_EXECUTABLE} ${_bundle}")
    execute_process(
        COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${_bundle}" "-verbose=1"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "macdeployqt failed (exit ${_rc})")
    endif()
else()
    # Not a warning. A bundle with no Qt embedded runs on the machine that built
    # it and nowhere else, which is precisely the failure that stayed hidden in
    # the Windows deployment for as long as it did.
    message(FATAL_ERROR "macdeployqt not available; the bundle cannot be built without it")
endif()

# macdeployqt embeds only the platform plugin it believes the application needs,
# which on macOS is libqcocoa. The offscreen plugin is how the package is checked
# without a window server -- `slideio-viewer -platform offscreen` is the smoke
# test -- and it is the only way to run the viewer headless at all. Without it
# the probe aborts instead of reporting a missing plugin, which is what the
# equivalent gap did on Windows.
set(_offscreen "${SLIDEIO_VIEWER_QT_PLUGIN_SRC_DIR}/platforms/libqoffscreen.dylib")
if(NOT EXISTS "${_offscreen}")
    message(FATAL_ERROR "Qt offscreen platform plugin not found at '${_offscreen}'")
endif()
file(COPY "${_offscreen}" DESTINATION "${_bundle}/Contents/PlugIns/platforms")

foreach(_required
        "${_bundle}/Contents/PlugIns/platforms/libqcocoa.dylib"
        "${_bundle}/Contents/PlugIns/platforms/libqoffscreen.dylib")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "macdeployqt did not produce '${_required}'")
    endif()
endforeach()

# Step 2: SlideIO + transitive dylibs (libglog, etc.) from SlideIO install bin/.
set(_slideio_src_dir "${SLIDEIO_ROOT}/${_cfg}/bin")
if(NOT EXISTS "${_slideio_src_dir}")
    message(FATAL_ERROR "SlideIO dylib source not found: ${_slideio_src_dir}")
endif()

file(GLOB _slideio_dylibs LIST_DIRECTORIES false "${_slideio_src_dir}/*.dylib")
file(COPY ${_slideio_dylibs}
    DESTINATION "${_bundle}/Contents/Frameworks"
    FOLLOW_SYMLINK_CHAIN)
message(STATUS "Bundled SlideIO dylibs from ${_slideio_src_dir}")
