function(r_add_library_module target r_module module_token)
    list(LENGTH ARGN public_operation_count)
    add_library(${target} STATIC
        "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/diagnostics/source/module_descriptor.c"
        ${ARGN}
    )
    target_link_libraries(${target} PRIVATE r_library_internal_headers)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE
        R_LIBRARY_MODULE_TOKEN=${module_token}
        R_LIBRARY_MODULE_R_NAME=${r_module}
        R_LIBRARY_MODULE_TARGET=${target}
        R_LIBRARY_MODULE_ABI_REVISION=1
        R_LIBRARY_MODULE_PUBLIC_OPERATION_COUNT=${public_operation_count}
    )
    target_compile_features(${target} PUBLIC c_std_17)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes
        )
    endif()

    if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PUBLIC
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()

    if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PUBLIC
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
    endif()
endfunction()
