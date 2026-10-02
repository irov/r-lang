# The native provider of std.alloc::usage (Library R-SLIB-ALLOC-0004): the counters of the budget
# of the calling thread, read from the runtime, which every hosted program links. The library
# link manifest library/r/links.json names it std.alloc.native.
add_library(r_std_alloc_native STATIC source/r_std_alloc_native.c)
target_include_directories(r_std_alloc_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(r_std_alloc_native PRIVATE "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_compile_features(r_std_alloc_native PUBLIC c_std_17)
set_target_properties(r_std_alloc_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(NOT MSVC)
    target_compile_options(r_std_alloc_native PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_std_alloc_native PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_std_alloc_native PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_std_alloc_native PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_std_alloc_native PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

# The archives that a program importing std.alloc::usage links after its generated code.
set(R_STD_ALLOC_NATIVE_LINK_ARCHIVES
    "$<TARGET_FILE:r_std_alloc_native>"
    CACHE INTERNAL "Archives of the std.alloc native provider")
set(R_STD_ALLOC_NATIVE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE INTERNAL "Header directory of the std.alloc native provider")
