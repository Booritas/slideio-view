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
    core/ColorProfileInfoTest.cpp
    core/ColorManagementTest.cpp
    core/SlideIdTest.cpp
    core/ColorProfileOverrideTest.cpp
)

target_link_libraries(slideio-viewer-core-tests
    PRIVATE slideio-viewer-core Catch2::Catch2WithMain
)

add_test(NAME core-tests COMMAND slideio-viewer-core-tests)

# Infrastructure layer tests
add_executable(slideio-viewer-infra-tests
    infra/LruTileCacheTest.cpp
    infra/TileLoadSchedulerTest.cpp
    infra/ColorManagedAdapterTest.cpp
    infra/QSettingsColorProfileOverrideStoreTest.cpp
)

target_link_libraries(slideio-viewer-infra-tests
    PRIVATE slideio-viewer-infra Catch2::Catch2WithMain Qt6::Core
)

add_test(NAME infra-tests COMMAND slideio-viewer-infra-tests)

# UI layer tests
add_executable(slideio-viewer-ui-tests
    ui/AboutInfoTest.cpp
    ui/ColorProfilePolicyTest.cpp
    ui/DriverFiltersTest.cpp
    ui/PathDisplayTest.cpp
    ui/SlideDropTest.cpp
)

target_link_libraries(slideio-viewer-ui-tests
    PRIVATE slideio-viewer-ui Catch2::Catch2WithMain
)

add_test(NAME ui-tests COMMAND slideio-viewer-ui-tests)

# Both this binary and the UI one pull in SlideIO symbols, and nothing deploys
# runtime dependencies into the build tree. Copying the DLLs next to the
# executable is not enough: slideio.dll loads a family of sibling slideio-*.dll
# drivers, so point the tests at the install directories instead.
#
# SLIDEIO_VIEWER_TEST_IMAGES points at the image corpus, which is not part of
# this repository; tests needing it skip when it is unset or the file is absent.
if(WIN32)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        # _qt_bin_dir comes from slideio_viewer_qt_bin_dir() in the top-level
        # list file. Conan resolves one Qt package per configuration, so this is
        # already the right one for the configuration being built -- unlike the
        # pair of per-config directories this used to select between.
        set(_slideio_bin_dir "$<IF:$<CONFIG:Debug>,${SLIDEIO_ROOT}/debug/bin,${SLIDEIO_ROOT}/release/bin>")
        set_tests_properties(ui-tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_slideio_bin_dir};PATH=path_list_prepend:${_qt_bin_dir}")
        set_tests_properties(infra-tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_slideio_bin_dir};PATH=path_list_prepend:${_qt_bin_dir}")
    else()
        message(WARNING
            "CMake < 3.22: the ui-tests and infra-tests targets cannot have their PATH set up "
            "automatically. Run them with the Qt and SlideIO bin directories on PATH.")
    endif()
endif()
