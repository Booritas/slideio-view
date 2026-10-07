add_library(slideio-viewer-infra STATIC
    src/SlideIOAdapter.cpp
    src/LruTileCache.cpp
    src/TileLoadScheduler.cpp
    src/Prefetcher.cpp
    src/QSettingsColorProfileOverrideStore.cpp
)

target_include_directories(slideio-viewer-infra PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(slideio-viewer-infra
    PUBLIC slideio-viewer-core Qt6::Core
    PRIVATE SlideIO::slideio SlideIO::core SlideIO::transformer spdlog::spdlog
)

target_compile_features(slideio-viewer-infra PUBLIC cxx_std_17)

include(CompilerWarnings)
set_project_warnings(slideio-viewer-infra)
