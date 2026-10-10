add_library(slideio-viewer-app STATIC
    src/SlideViewerService.cpp
    src/AnnotationModel.cpp
    src/AnnotationPersistenceService.cpp
    # Listed so AUTOMOC runs moc on them: it does not scan headers that live in a
    # different directory from their .cpp.
    include/slideio/viewer/app/AnnotationModel.h
    include/slideio/viewer/app/AnnotationPersistenceService.h
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
