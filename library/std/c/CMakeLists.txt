set(R_STD_C_PUBLIC_SOURCES
    source/adopt_handle.c
    source/attach_thread.c
    source/checked.c
    source/copy_utf8.c
    source/detach_thread.c
    source/handle_pointer.c
    source/link_available.c
    source/release_handle.c
    source/runtime_as_error.c
    source/string_as_error.c
    source/string_as_ptr.c
    source/string_as_slice.c
    source/string_from_str.c
    source/target.c
    source/validate_utf8.c
)
r_add_library_module(r_std_c std.c std_c ${R_STD_C_PUBLIC_SOURCES})
target_include_directories(r_std_c PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/error/include"
)
target_link_libraries(r_std_c PUBLIC
    r_std_alloc
    r_std_convert
    r_std_string
)
