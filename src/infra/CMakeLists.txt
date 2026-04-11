add_library(slideio-viewer-infra STATIC
    src/SlideIOAdapter.cpp
    src/SlideIOAdapterPool.cpp
    src/LruTileCache.cpp
    src/TileLoadScheduler.cpp
    src/Prefetcher.cpp
)

target_include_directories(slideio-viewer-infra PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(slideio-viewer-infra
    PUBLIC slideio-viewer-core
    PRIVATE SlideIO::slideio SlideIO::core spdlog::spdlog
)

target_compile_features(slideio-viewer-infra PUBLIC cxx_std_17)

include(CompilerWarnings)
set_project_warnings(slideio-viewer-infra)
