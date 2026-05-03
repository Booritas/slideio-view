# CopySlideIODlls.cmake -- Copy the correct (debug/release) SlideIO DLLs
# (and matching PDBs, when present) into the build output directory.
#
# Called as: cmake -D CONFIG=... -D SLIDEIO_ROOT=... -D OUTPUT_DIR=... -P CopySlideIODlls.cmake
#
# PDBs are looked up in the same per-config install bin/ as the DLLs, so VS
# can step into SlideIO sources while debugging. As a fallback (e.g. when the
# install layout doesn't ship PDBs), the SlideIO build tree's sibling
# bin/<Config>/ directory is also scanned. Either location can be overridden
# via SLIDEIO_PDB_DIR.

if(CONFIG STREQUAL "Debug")
    set(_dll_src_dir "${SLIDEIO_ROOT}/debug/bin")
else()
    set(_dll_src_dir "${SLIDEIO_ROOT}/release/bin")
endif()

file(GLOB _dlls "${_dll_src_dir}/*.dll")
foreach(_dll ${_dlls})
    get_filename_component(_name "${_dll}" NAME)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${OUTPUT_DIR}/${_name}"
    )
endforeach()

# PDB search dirs: explicit override first, then the per-config install bin
# (where the user installed PDBs alongside DLLs), then the SlideIO build tree
# bin/<Config>/ (for builds that didn't install PDBs).
set(_pdb_search_dirs "")
if(DEFINED SLIDEIO_PDB_DIR AND NOT SLIDEIO_PDB_DIR STREQUAL "")
    list(APPEND _pdb_search_dirs "${SLIDEIO_PDB_DIR}")
endif()
list(APPEND _pdb_search_dirs "${_dll_src_dir}")
get_filename_component(_slideio_build_dir "${SLIDEIO_ROOT}" DIRECTORY)
list(APPEND _pdb_search_dirs "${_slideio_build_dir}/bin/${CONFIG}")

set(_pdbs_copied 0)
foreach(_pdb_dir ${_pdb_search_dirs})
    if(NOT EXISTS "${_pdb_dir}")
        continue()
    endif()
    file(GLOB _pdbs "${_pdb_dir}/*.pdb")
    foreach(_pdb ${_pdbs})
        get_filename_component(_name "${_pdb}" NAME)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_pdb}" "${OUTPUT_DIR}/${_name}"
        )
        math(EXPR _pdbs_copied "${_pdbs_copied} + 1")
    endforeach()
    if(_pdbs_copied GREATER 0)
        break()
    endif()
endforeach()
