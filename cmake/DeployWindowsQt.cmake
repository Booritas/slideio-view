# Runs windeployqt against the installed executable and then checks its work.
#
# Invoked from CMakeLists.txt via install(SCRIPT ...). Expects these variables,
# set by surrounding install(CODE) blocks:
#   WINDEPLOYQT_EXECUTABLE     - path to windeployqt.exe
#   SLIDEIO_VIEWER_BIN_DIR     - installed bin directory
#   SLIDEIO_VIEWER_PLUGIN_DIR  - installed plugins directory

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
            --no-compiler-runtime
            "${_exe}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (exit ${_rc})")
endif()

# windeployqt can exit 0 having staged nothing useful -- a wrong --debug/--release
# pairing is the common way. An installer that builds cleanly and contains no Qt
# is the exact failure this packaging work exists to remove, so assert rather
# than trust the exit code.
foreach(_required
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Core${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Gui${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Widgets${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6OpenGLWidgets${_suffix}.dll"
        "${SLIDEIO_VIEWER_PLUGIN_DIR}/platforms/qwindows${_suffix}.dll")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "windeployqt did not produce '${_required}'")
    endif()
endforeach()
message(STATUS "Qt runtime deployed to ${SLIDEIO_VIEWER_BIN_DIR}")
