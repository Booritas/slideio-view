add_library(slideio-viewer-ui STATIC
    src/AppPaths.cpp
    src/AssociatedImageWindow.cpp
    src/DriverFilters.cpp
    src/ViewportController.cpp
    src/ViewportWidget.cpp
    src/MinimapWidget.cpp
    src/ZoomIndicatorWidget.cpp
    src/StatusBarManager.cpp
    src/ChannelMixerPanel.cpp
    src/SceneThumbnailPanel.cpp
    src/ZTNavigationWidget.cpp
    src/LoadingOverlay.cpp
    src/SlidePropertiesPanel.cpp
    src/MainWindow.cpp
)

target_include_directories(slideio-viewer-ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(slideio-viewer-ui
    PUBLIC slideio-viewer-core slideio-viewer-infra
    PUBLIC Qt6::Widgets Qt6::OpenGLWidgets
    PRIVATE spdlog::spdlog
)

target_compile_features(slideio-viewer-ui PUBLIC cxx_std_17)

set_target_properties(slideio-viewer-ui PROPERTIES
    AUTOMOC ON
    AUTOMOC_MOC_OPTIONS ""
)

# AUTOMOC needs to find Q_OBJECT headers in the include directory
set_property(TARGET slideio-viewer-ui APPEND PROPERTY
    AUTOGEN_TARGET_DEPENDS ""
)

# Tell AUTOMOC where to find headers with Q_OBJECT
file(GLOB_RECURSE _ui_headers "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(slideio-viewer-ui PRIVATE ${_ui_headers})

include(CompilerWarnings)
set_project_warnings(slideio-viewer-ui)
