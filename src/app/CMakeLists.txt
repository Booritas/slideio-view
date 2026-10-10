add_library(slideio-viewer-app STATIC
    src/SlideViewerService.cpp
    src/AnnotationModel.cpp
    include/slideio/viewer/app/AnnotationModel.h
)

target_include_directories(slideio-viewer-app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(slideio-viewer-app
    PUBLIC slideio-viewer-core
    PUBLIC Qt6::Core
)

target_compile_features(slideio-viewer-app PUBLIC cxx_std_17)

include(CompilerWarnings)
set_project_warnings(slideio-viewer-app)
