set(R_STD_UTF8_PUBLIC_SOURCES
    source/is_valid.c
    source/validate.c
)
r_add_library_module(r_std_utf8 std.utf8 std_utf8 ${R_STD_UTF8_PUBLIC_SOURCES})
target_link_libraries(r_std_utf8 PUBLIC r_core)
