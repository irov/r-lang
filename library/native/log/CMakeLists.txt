# The native provider of std.log::panic_reports (Library R-SLIB-LOG-0005): a queue of the panic
# reports that nothing observed, filled by the panic sink of the runtime, which every hosted
# program links. The library link manifest library/r/links.json names it std.log.native.
add_library(r_std_log_native STATIC source/r_std_log_native.c)
target_include_directories(r_std_log_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(r_std_log_native PRIVATE "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_compile_features(r_std_log_native PUBLIC c_std_17)
set_target_properties(r_std_log_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(NOT MSVC)
    target_compile_options(r_std_log_native PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_std_log_native PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_std_log_native PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_std_log_native PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_std_log_native PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

# The archives that a program importing std.log links after its generated code.
set(R_STD_LOG_NATIVE_LINK_ARCHIVES
    "$<TARGET_FILE:r_std_log_native>"
    CACHE INTERNAL "Archives of the std.log native provider")
set(R_STD_LOG_NATIVE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE INTERNAL "Header directory of the std.log native provider")
