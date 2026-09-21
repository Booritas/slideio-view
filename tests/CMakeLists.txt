# Core layer tests
add_executable(slideio-viewer-core-tests
    core/TileKeyTest.cpp
    core/TilePyramidTest.cpp
    core/ViewportTest.cpp
    core/CoordinateSystemTest.cpp
    core/HistogramTest.cpp
    core/LevelUnreliableRegistryTest.cpp
    core/ScaleBarTest.cpp
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
