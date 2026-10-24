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
    src/AnnotationMeasurement.cpp
    src/AnnotationMerge.cpp
    src/VertexEditing.cpp
    src/BoxResize.cpp
    src/AnnotationDecoration.cpp
    src/AnnotationCategories.cpp
    src/Calibration.cpp
    src/AnnotationLayer.cpp
    src/LevelRaster.cpp
    src/WorkspaceSwitchPolicy.cpp
    src/Annotation.cpp
    src/Iso8601.cpp
    src/AnnotationSerialization.cpp
    src/AnnotationGeoJson.cpp
    src/AnnotationImport.cpp
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

# No fused multiply-add. Clang contracts `a + t * b` into one FMA by default on
# arm64, skipping the rounding of the product: a point lying exactly on a
# polygon edge then lands 3.6e-15 off it and misses a zero-tolerance hit test,
# and a stored annotation measures differently on Apple Silicon than on x86.
# MSVC does not contract without /fp:contract, so it needs nothing.
if(NOT MSVC)
    target_compile_options(slideio-viewer-core PRIVATE -ffp-contract=off)
endif()

include(CompilerWarnings)
set_project_warnings(slideio-viewer-core)
