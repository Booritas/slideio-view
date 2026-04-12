# FindSlideIO.cmake -- locate the SlideIO pre-built library
#
# User sets SLIDEIO_ROOT to point to the install directory
# (e.g. D:/Projects/slideio/slideio/build/install)
#
# This module creates IMPORTED targets: SlideIO::slideio, SlideIO::core
# Supports multi-config generators (MSVC) with separate debug/release layouts.

if(NOT SLIDEIO_ROOT)
    message(FATAL_ERROR "SLIDEIO_ROOT must be set to the SlideIO install directory")
endif()

set(_slideio_release_prefix "${SLIDEIO_ROOT}/release")
set(_slideio_debug_prefix   "${SLIDEIO_ROOT}/debug")

find_path(SlideIO_INCLUDE_DIR
    NAMES slideio/slideio/slideio.hpp
    PATHS "${_slideio_release_prefix}/include"
    NO_DEFAULT_PATH
)

find_library(SlideIO_LIBRARY_RELEASE
    NAMES slideio
    PATHS "${_slideio_release_prefix}/lib"
    NO_DEFAULT_PATH
)

find_library(SlideIO_LIBRARY_DEBUG
    NAMES slideio_d
    PATHS "${_slideio_debug_prefix}/lib"
    NO_DEFAULT_PATH
)

find_library(SlideIO_CORE_LIBRARY_RELEASE
    NAMES slideio-core
    PATHS "${_slideio_release_prefix}/lib"
    NO_DEFAULT_PATH
)

find_library(SlideIO_CORE_LIBRARY_DEBUG
    NAMES slideio-core_d
    PATHS "${_slideio_debug_prefix}/lib"
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SlideIO
    REQUIRED_VARS SlideIO_LIBRARY_RELEASE SlideIO_CORE_LIBRARY_RELEASE SlideIO_INCLUDE_DIR
)

if(SlideIO_FOUND AND NOT TARGET SlideIO::slideio)
    add_library(SlideIO::slideio SHARED IMPORTED)
    set_target_properties(SlideIO::slideio PROPERTIES
        IMPORTED_IMPLIB_RELEASE "${SlideIO_LIBRARY_RELEASE}"
        IMPORTED_LOCATION_RELEASE "${_slideio_release_prefix}/bin/slideio.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SlideIO_INCLUDE_DIR}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
    )
    if(SlideIO_LIBRARY_DEBUG)
        set_target_properties(SlideIO::slideio PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${SlideIO_LIBRARY_DEBUG}"
            IMPORTED_LOCATION_DEBUG "${_slideio_debug_prefix}/bin/slideio_d.dll"
        )
    else()
        set_target_properties(SlideIO::slideio PROPERTIES
            MAP_IMPORTED_CONFIG_DEBUG Release
        )
    endif()

    add_library(SlideIO::core SHARED IMPORTED)
    set_target_properties(SlideIO::core PROPERTIES
        IMPORTED_IMPLIB_RELEASE "${SlideIO_CORE_LIBRARY_RELEASE}"
        IMPORTED_LOCATION_RELEASE "${_slideio_release_prefix}/bin/slideio-core.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SlideIO_INCLUDE_DIR}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
    )
    if(SlideIO_CORE_LIBRARY_DEBUG)
        set_target_properties(SlideIO::core PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${SlideIO_CORE_LIBRARY_DEBUG}"
            IMPORTED_LOCATION_DEBUG "${_slideio_debug_prefix}/bin/slideio-core_d.dll"
        )
    else()
        set_target_properties(SlideIO::core PROPERTIES
            MAP_IMPORTED_CONFIG_DEBUG Release
        )
    endif()
endif()

mark_as_advanced(
    SlideIO_INCLUDE_DIR
    SlideIO_LIBRARY_RELEASE SlideIO_LIBRARY_DEBUG
    SlideIO_CORE_LIBRARY_RELEASE SlideIO_CORE_LIBRARY_DEBUG
)
