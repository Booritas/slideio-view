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
    message(WARNING "macdeployqt not available; Qt frameworks/plugins NOT embedded")
endif()

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
