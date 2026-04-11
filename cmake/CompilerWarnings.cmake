# CompilerWarnings.cmake -- warning flags per compiler

function(set_project_warnings target_name)
    set(MSVC_WARNINGS
        /W4
        /w14242  # conversion, possible loss of data
        /w14254  # operator conversion, possible loss of data
        /w14263  # function does not override base class virtual
        /w14265  # class has virtual functions but destructor is not virtual
        /w14287  # unsigned/negative constant mismatch
        /we4289  # loop control variable used outside for-loop
        /w14296  # expression is always false
        /w14311  # pointer truncation
        /w14545  # expression before comma evaluates to function
        /w14546  # function call before comma missing argument list
        /w14547  # operator before comma has no effect
        /w14549  # operator before comma has no effect
        /w14555  # expression has no effect
        /w14619  # pragma warning: nonexistent warning number
        /w14640  # thread-unsafe static member initialization
        /w14826  # conversion is sign-extended
        /w14905  # wide string literal cast to LPSTR
        /w14906  # string literal cast to LPWSTR
        /w14928  # illegal copy-initialization
        /permissive-
    )

    set(GCC_CLANG_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )

    set(GCC_ONLY_WARNINGS
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE ${MSVC_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(${target_name} PRIVATE ${GCC_CLANG_WARNINGS} ${GCC_ONLY_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE ${GCC_CLANG_WARNINGS})
    endif()
endfunction()
