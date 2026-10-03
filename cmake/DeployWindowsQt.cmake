# Runs windeployqt against the installed executable and then checks its work.
#
# Invoked from CMakeLists.txt via install(SCRIPT ...). Expects these variables,
# set by surrounding install(CODE) blocks:
#   WINDEPLOYQT_EXECUTABLE     - path to windeployqt.exe
#   SLIDEIO_VIEWER_BIN_DIR     - installed bin directory
#   SLIDEIO_VIEWER_PLUGIN_DIR  - installed plugins directory
#   SLIDEIO_VIEWER_QT_PLUGIN_SRC_DIR - Qt's own plugins directory

set(_exe "${SLIDEIO_VIEWER_BIN_DIR}/slideio-viewer.exe")
if(NOT EXISTS "${_exe}")
    message(FATAL_ERROR "DeployWindowsQt: executable not found at '${_exe}'")
endif()

if("${CMAKE_INSTALL_CONFIG_NAME}" STREQUAL "Debug")
    set(_config_flag "--debug")
    set(_suffix "d")
else()
    set(_config_flag "--release")
    set(_suffix "")
endif()

message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE} ${_exe}")
execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            ${_config_flag}
            --dir "${SLIDEIO_VIEWER_BIN_DIR}"
            --plugindir "${SLIDEIO_VIEWER_PLUGIN_DIR}"
            --no-translations
            "${_exe}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (exit ${_rc})")
endif()

# windeployqt deploys only the platform plugin it believes the application needs,
# which on Windows is qwindows alone. The offscreen plugin is how the package is
# checked without a desktop session -- `slideio-viewer -platform offscreen` is the
# smoke test, and without this it aborts with STATUS_STACK_BUFFER_OVERRUN rather
# than reporting a missing plugin. It is also the only way to run the application
# headless at all, so it belongs in the package regardless of the test.
set(_offscreen "${SLIDEIO_VIEWER_QT_PLUGIN_SRC_DIR}/platforms/qoffscreen${_suffix}.dll")
if(NOT EXISTS "${_offscreen}")
    message(FATAL_ERROR "Qt offscreen platform plugin not found at '${_offscreen}'")
endif()
file(COPY "${_offscreen}" DESTINATION "${SLIDEIO_VIEWER_PLUGIN_DIR}/platforms")

# windeployqt can exit 0 having staged nothing useful -- a wrong --debug/--release
# pairing is the common way. An installer that builds cleanly and contains no Qt
# is the exact failure this packaging work exists to remove, so assert rather
# than trust the exit code.
foreach(_required
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Core${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Gui${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Widgets${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6OpenGLWidgets${_suffix}.dll"
        "${SLIDEIO_VIEWER_PLUGIN_DIR}/platforms/qwindows${_suffix}.dll"
        "${SLIDEIO_VIEWER_PLUGIN_DIR}/platforms/qoffscreen${_suffix}.dll")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "windeployqt did not produce '${_required}'")
    endif()
endforeach()

# The MSVC runtime, installed by InstallRequiredSystemLibraries in CMakeLists.txt
# rather than by windeployqt. Asserted here because its absence is invisible on
# any machine that has Visual Studio -- which is every Windows CI runner and
# every developer box, so no test run anywhere can catch it by behaviour.
#
# Only when packaging. A developer installing into build/install has Visual
# Studio by definition, and failing their ordinary `./build.sh` would be a poor
# trade for a check that only matters to an artifact someone else downloads.
if(SLIDEIO_VIEWER_PACKAGING)
    foreach(_runtime vcruntime140.dll msvcp140.dll)
        if(NOT EXISTS "${SLIDEIO_VIEWER_BIN_DIR}/${_runtime}")
            message(FATAL_ERROR
                "${_runtime} was not installed beside the executable. The package "
                "would fail on a machine without Visual Studio. Check that "
                "InstallRequiredSystemLibraries resolved CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS.")
        endif()
    endforeach()
else()
    message(STATUS "Not packaging; MSVC runtime deployment not required")
endif()
message(STATUS "Qt runtime deployed to ${SLIDEIO_VIEWER_BIN_DIR}")
