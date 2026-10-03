# Collects the values that BuildInfo.h.in stamps into the generated header: the
# git revision, the configure timestamp, the resolved dependency versions and
# the text of the LICENSE file.
#
# Everything here is evaluated at configure time. The git revision would go
# stale as soon as the next commit landed, so .git/HEAD and .git/index are
# registered as configure dependencies: committing (or staging) touches one of
# them and CMake re-runs before the next build.

set(SLIDEIO_VIEWER_GIT_REVISION "unknown")

find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result)

    if(_git_result EQUAL 0 AND _git_revision)
        # Untracked files are ignored: the build tree and other scratch live in
        # the working copy all the time, and they say nothing about whether the
        # binary matches the named commit.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_git_status)
            string(APPEND _git_revision "-dirty")
        endif()
        set(SLIDEIO_VIEWER_GIT_REVISION "${_git_revision}")
    endif()

    foreach(_git_file "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD"
                      "${CMAKE_CURRENT_SOURCE_DIR}/.git/index")
        if(EXISTS "${_git_file}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_file}")
        endif()
    endforeach()
endif()

string(TIMESTAMP SLIDEIO_VIEWER_BUILD_DATE "%Y-%m-%d" UTC)

# find_package sets these from the package config files. A source build that
# resolved a dependency some other way leaves them empty, and "unknown" reads
# better in the dialog than a dangling "spdlog :".
set(SLIDEIO_VIEWER_JSON_VERSION "${nlohmann_json_VERSION}")
set(SLIDEIO_VIEWER_SPDLOG_VERSION "${spdlog_VERSION}")
foreach(_version_var SLIDEIO_VIEWER_JSON_VERSION SLIDEIO_VIEWER_SPDLOG_VERSION)
    if(NOT ${_version_var})
        set(${_version_var} "unknown")
    endif()
endforeach()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" SLIDEIO_VIEWER_LICENSE_TEXT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
