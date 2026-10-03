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
            # The MSVC runtime is deliberately NOT excluded. Without it the
            # installed application fails on a clean machine with a missing
            # VCRUNTIME140.dll dialog, and no CI runner can notice because every
            # Windows runner has MSVC installed. windeployqt copies the
            # redistributable when VCINSTALLDIR is set, which msvc-dev-cmd does
            # in CI and a Developer Command Prompt does locally.
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
message(STATUS "Qt runtime deployed to ${SLIDEIO_VIEWER_BIN_DIR}")
