set(R_STD_CONVERT_PUBLIC_SOURCES
    source/checked.c
    source/parse_c_double.c
    source/parse_c_float.c
    source/parse_c_long_double.c
    source/parse_f32.c
    source/parse_f64.c
    source/parse_suffix.c
)
r_add_library_module(r_std_convert std.convert std_convert ${R_STD_CONVERT_PUBLIC_SOURCES})
target_sources(r_std_convert PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/source/darwin_float_parse.c"
)
target_include_directories(r_std_convert PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/include"
)
target_link_libraries(r_std_convert PUBLIC r_std_string)
