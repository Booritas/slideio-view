# Locates the Qt runtime for deployment.
#
# The previous implementation globbed an absolute path into one developer's
# conan cache. Qt6::qmake is an imported target CMakeDeps generates, so its
# location resolves wherever the cache actually lives -- and on a CI runner,
# which is the case the glob could never serve.

function(slideio_viewer_qt_bin_dir out_var)
    if(NOT TARGET Qt6::qmake)
        message(FATAL_ERROR
            "Qt6::qmake was not defined by find_package(Qt6); cannot locate the "
            "Qt runtime to deploy.")
    endif()
    get_target_property(_qmake_loc Qt6::qmake IMPORTED_LOCATION)
    if(NOT _qmake_loc)
        message(FATAL_ERROR "Qt6::qmake has no IMPORTED_LOCATION; cannot locate the Qt runtime.")
    endif()
    get_filename_component(_bin_dir "${_qmake_loc}" DIRECTORY)
    set(${out_var} "${_bin_dir}" PARENT_SCOPE)
endfunction()
