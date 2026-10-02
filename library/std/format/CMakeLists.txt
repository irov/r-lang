set(R_STD_FORMAT_PUBLIC_SOURCES
    source/append_c_double.c
    source/append_c_float.c
    source/append_c_long_double.c
    source/append_char.c
    source/append_f32.c
    source/append_f64.c
    source/append_str.c
    source/append_suffix.c
    source/as_str.c
    source/clear.c
    source/create.c
    source/finish.c
    source/with_capacity.c
)
r_add_library_module(r_std_format std.format std_format ${R_STD_FORMAT_PUBLIC_SOURCES})
target_sources(r_std_format PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/source/darwin_float_format.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/source/format_spec.c"
)
target_include_directories(r_std_format PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/include"
)
target_link_libraries(r_std_format PUBLIC
    r_std_convert
    r_std_string
)
