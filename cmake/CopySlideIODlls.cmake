# CopySlideIODlls.cmake -- Copy the correct (debug/release) SlideIO DLLs
# Called as: cmake -D CONFIG=... -D SLIDEIO_ROOT=... -D OUTPUT_DIR=... -P CopySlideIODlls.cmake

if(CONFIG STREQUAL "Debug")
    set(_src_dir "${SLIDEIO_ROOT}/debug/bin")
else()
    set(_src_dir "${SLIDEIO_ROOT}/release/bin")
endif()

file(GLOB _dlls "${_src_dir}/*.dll")
foreach(_dll ${_dlls})
    get_filename_component(_name "${_dll}" NAME)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${OUTPUT_DIR}/${_name}"
    )
endforeach()
