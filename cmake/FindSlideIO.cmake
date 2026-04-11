# FindSlideIO.cmake -- locate the SlideIO pre-built library
#
# User sets SLIDEIO_ROOT to point to the install directory
# (e.g. D:/Projects/slideio/slideio/build/install)
#
# This module creates an IMPORTED target SlideIO::slideio

if(NOT SLIDEIO_ROOT)
    message(FATAL_ERROR "SLIDEIO_ROOT must be set to the SlideIO install directory")
endif()

# Select release or debug subdirectory
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_slideio_subdir "debug")
    set(_slideio_lib_suffix "_d")
else()
    set(_slideio_subdir "release")
    set(_slideio_lib_suffix "")
endif()

set(_slideio_prefix "${SLIDEIO_ROOT}/${_slideio_subdir}")

find_path(SlideIO_INCLUDE_DIR
    NAMES slideio/slideio/slideio.hpp
    PATHS "${_slideio_prefix}/include"
    NO_DEFAULT_PATH
)

find_library(SlideIO_LIBRARY
    NAMES "slideio${_slideio_lib_suffix}"
    PATHS "${_slideio_prefix}/lib"
    NO_DEFAULT_PATH
)

find_library(SlideIO_CORE_LIBRARY
    NAMES "slideio-core${_slideio_lib_suffix}"
    PATHS "${_slideio_prefix}/lib"
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SlideIO
    REQUIRED_VARS SlideIO_LIBRARY SlideIO_CORE_LIBRARY SlideIO_INCLUDE_DIR
)

if(SlideIO_FOUND AND NOT TARGET SlideIO::slideio)
    add_library(SlideIO::slideio SHARED IMPORTED)
    set_target_properties(SlideIO::slideio PROPERTIES
        IMPORTED_IMPLIB "${SlideIO_LIBRARY}"
        IMPORTED_LOCATION "${_slideio_prefix}/bin/slideio${_slideio_lib_suffix}.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SlideIO_INCLUDE_DIR}"
    )

    add_library(SlideIO::core SHARED IMPORTED)
    set_target_properties(SlideIO::core PROPERTIES
        IMPORTED_IMPLIB "${SlideIO_CORE_LIBRARY}"
        IMPORTED_LOCATION "${_slideio_prefix}/bin/slideio-core${_slideio_lib_suffix}.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SlideIO_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(SlideIO_INCLUDE_DIR SlideIO_LIBRARY SlideIO_CORE_LIBRARY)
