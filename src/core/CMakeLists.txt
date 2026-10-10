add_library(slideio-viewer-core STATIC
    src/TileKey.cpp
    src/TilePyramid.cpp
    src/Viewport.cpp
    src/CoordinateSystem.cpp
    src/Histogram.cpp
    src/LevelUnreliableRegistry.cpp
    src/ScaleBar.cpp
    src/TileSampling.cpp
    src/ColorProfileInfo.cpp
    src/ColorManagement.cpp
    src/SlideId.cpp
    src/AnnotationGeometry.cpp
    src/Annotation.cpp
    src/Iso8601.cpp
    src/AnnotationSerialization.cpp
    src/AutosavePolicy.cpp
)

target_include_directories(slideio-viewer-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(slideio-viewer-core PUBLIC cxx_std_17)

# PRIVATE: nlohmann appears in no core public header, so core keeps its
# "links nothing" shape from every consumer's point of view, and still links
# no Qt.
target_link_libraries(slideio-viewer-core PRIVATE nlohmann_json::nlohmann_json)

include(CompilerWarnings)
set_project_warnings(slideio-viewer-core)
