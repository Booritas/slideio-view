# Core layer tests
add_executable(slideio-viewer-core-tests
    core/TileKeyTest.cpp
    core/TilePyramidTest.cpp
    core/ViewportTest.cpp
    core/CoordinateSystemTest.cpp
    core/HistogramTest.cpp
    core/LevelUnreliableRegistryTest.cpp
    core/ScaleBarTest.cpp
    core/TileSamplingTest.cpp
)

target_link_libraries(slideio-viewer-core-tests
    PRIVATE slideio-viewer-core Catch2::Catch2WithMain
)

add_test(NAME core-tests COMMAND slideio-viewer-core-tests)

# Infrastructure layer tests
add_executable(slideio-viewer-infra-tests
    infra/LruTileCacheTest.cpp
    infra/TileLoadSchedulerTest.cpp
)

target_link_libraries(slideio-viewer-infra-tests
    PRIVATE slideio-viewer-infra Catch2::Catch2WithMain
)

add_test(NAME infra-tests COMMAND slideio-viewer-infra-tests)

# UI layer tests
add_executable(slideio-viewer-ui-tests
    ui/AboutInfoTest.cpp
    ui/DriverFiltersTest.cpp
    ui/PathDisplayTest.cpp
    ui/SlideDropTest.cpp
)

target_link_libraries(slideio-viewer-ui-tests
    PRIVATE slideio-viewer-ui Catch2::Catch2WithMain
)

add_test(NAME ui-tests COMMAND slideio-viewer-ui-tests)

# Unlike the core and infra test binaries, this one actually pulls in Qt and
# SlideIO symbols, and nothing deploys runtime dependencies into the build
# tree. Copying the DLLs next to the executable is not enough: slideio.dll
# loads a family of sibling slideio-*.dll drivers, so point the test at the
# install directories instead.
if(WIN32)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        # _qt_bin_dir comes from slideio_viewer_qt_bin_dir() in the top-level
        # list file. Conan resolves one Qt package per configuration, so this is
        # already the right one for the configuration being built -- unlike the
        # pair of per-config directories this used to select between.
        set(_slideio_bin_dir "$<IF:$<CONFIG:Debug>,${SLIDEIO_ROOT}/debug/bin,${SLIDEIO_ROOT}/release/bin>")
        set_tests_properties(ui-tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_slideio_bin_dir};PATH=path_list_prepend:${_qt_bin_dir}")
    else()
        message(WARNING
            "CMake < 3.22: the ui-tests target cannot have its PATH set up automatically. "
            "Run slideio-viewer-ui-tests with the Qt and SlideIO bin directories on PATH.")
    endif()
endif()
