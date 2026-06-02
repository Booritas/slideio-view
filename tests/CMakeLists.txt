# Core layer tests
add_executable(slideio-viewer-core-tests
    core/TileKeyTest.cpp
    core/TilePyramidTest.cpp
    core/ViewportTest.cpp
    core/CoordinateSystemTest.cpp
    core/HistogramTest.cpp
    core/ScaleBarTest.cpp
)

target_link_libraries(slideio-viewer-core-tests
    PRIVATE slideio-viewer-core Catch2::Catch2WithMain
)

add_test(NAME core-tests COMMAND slideio-viewer-core-tests)

# Infrastructure layer tests
add_executable(slideio-viewer-infra-tests
    infra/LruTileCacheTest.cpp
    infra/PerfLogTest.cpp
)

target_link_libraries(slideio-viewer-infra-tests
    PRIVATE slideio-viewer-infra spdlog::spdlog Catch2::Catch2WithMain
)

add_test(NAME infra-tests COMMAND slideio-viewer-infra-tests)

# UI layer tests — compile LogSettings.cpp standalone (Qt6::Core only, no widgets)
# so the unit test does not pull in OpenGL/Widgets.
add_executable(slideio-viewer-ui-tests
    ui/LogSettingsTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/src/LogSettings.cpp
)

target_include_directories(slideio-viewer-ui-tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/include
)

target_link_libraries(slideio-viewer-ui-tests
    PRIVATE Qt6::Core spdlog::spdlog Catch2::Catch2WithMain
)

add_test(NAME ui-tests COMMAND slideio-viewer-ui-tests)

# On Windows the Qt6Core.dll must be on PATH for the test to run. Use the
# release bin dir discovered by the root CMakeLists (_qt_release_bin_dir).
if(WIN32 AND _qt_release_bin_dir)
    set_tests_properties(ui-tests PROPERTIES
        ENVIRONMENT "PATH=${_qt_release_bin_dir};$ENV{PATH}"
    )
endif()
