add_library(slideio-viewer-core STATIC
    src/TileKey.cpp
    src/TilePyramid.cpp
    src/Viewport.cpp
    src/CoordinateSystem.cpp
    src/Histogram.cpp
)

target_include_directories(slideio-viewer-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(slideio-viewer-core PUBLIC cxx_std_17)

include(CompilerWarnings)
set_project_warnings(slideio-viewer-core)
