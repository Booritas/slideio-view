# Stamps the version, git revision, dependency versions and LICENSE text into
# a generated header. cmake/BuildInfo.cmake, included from the top-level list
# file, collects the values.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/slideio/viewer/ui/BuildInfo.h"
    @ONLY)

add_library(slideio-viewer-ui STATIC
    src/AboutDialog.cpp
    src/AboutInfo.cpp
    src/AnnotationCountExport.cpp
    src/CsvFormat.cpp
    src/AnnotationMeasurementExport.cpp
    src/TextAnnotationLayout.cpp
    src/CalibrationPolicy.cpp
    src/CalibrationDialog.cpp
    src/MeasurementLabel.cpp
    src/LayersPanelPolicy.cpp
    src/LayersPanel.cpp
    src/AnnotationImportSummary.cpp
    src/AnnotationInteraction.cpp
    src/AnnotationShapeName.cpp
    src/AnnotationListPolicy.cpp
    src/AnnotationListPanel.cpp
    src/AnnotationPropertiesEdit.cpp
    src/AnnotationPropertiesPanel.cpp
    src/AnnotationSettings.cpp
    src/AppPaths.cpp
    src/AssociatedImageWindow.cpp
    src/ColorProfilePolicy.cpp
    src/DriverFilters.cpp
    src/FullScreenBorder.cpp
    src/PathDisplay.cpp
    src/RecentFiles.cpp
    src/ShortcutPolicy.cpp
    src/PreferencesDialog.cpp
    src/ViewportController.cpp
    src/ViewportWidget.cpp
    src/MinimapWidget.cpp
    src/ScaleBarWidget.cpp
    src/ZoomIndicatorWidget.cpp
    src/StatusBarManager.cpp
    src/ChannelMixerPanel.cpp
    src/ChannelHistogramView.cpp
    src/SceneThumbnailPanel.cpp
    src/ZTNavigationWidget.cpp
    src/LoadingOverlay.cpp
    src/SlidePropertiesPanel.cpp
    src/MetadataPanel.cpp
    src/SlideProfilesDialog.cpp
    src/WorkspaceChangeDialog.cpp
    src/MainWindow.cpp
)

target_include_directories(slideio-viewer-ui
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # Only AboutInfo.cpp reads the generated header; keeping it private stops it
    # leaking into every target that links the UI layer.
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(slideio-viewer-ui
    PUBLIC slideio-viewer-core slideio-viewer-app slideio-viewer-infra
    PUBLIC Qt6::Widgets Qt6::OpenGLWidgets
    PRIVATE spdlog::spdlog
)

target_compile_features(slideio-viewer-ui PUBLIC cxx_std_17)

# FullScreenBorder.cpp calls Qt's documented fix for menus hidden behind a
# full-screen OpenGL window, which in Qt 6 is a private Windows API. Conan's Qt
# package ships the private headers but no Qt6::GuiPrivate target, so they are
# found here and given to that one file alone -- nothing else may reach Qt's
# internals by accident.
if(WIN32)
    find_path(SLIDEIO_VIEWER_QTGUI_PRIVATE_DIR qpa/qplatformwindow_p.h
        PATH_SUFFIXES "${Qt6_VERSION}/QtGui" "${Qt6_VERSION_STRING}/QtGui"
        REQUIRED)
    get_filename_component(_qtgui_versioned "${SLIDEIO_VIEWER_QTGUI_PRIVATE_DIR}" DIRECTORY)
    get_filename_component(_qt_include "${_qtgui_versioned}/../.." ABSOLUTE)
    get_filename_component(_qt_version "${_qtgui_versioned}" NAME)
    set_source_files_properties(src/FullScreenBorder.cpp PROPERTIES INCLUDE_DIRECTORIES
        "${_qt_include}/QtCore/${_qt_version};${_qt_include}/QtCore/${_qt_version}/QtCore;${_qtgui_versioned};${SLIDEIO_VIEWER_QTGUI_PRIVATE_DIR}")
endif()

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
