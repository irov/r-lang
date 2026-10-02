set(R_STD_STRING_PUBLIC_SOURCES
    source/append_str.c
    source/append_utf8.c
    source/as_bytes.c
    source/as_str.c
    source/capacity.c
    source/clear.c
    source/create.c
    source/from_bytes.c
    source/from_str.c
    source/from_utf8.c
    source/into_bytes.c
    source/len.c
    source/push_scalar.c
    source/reserve.c
    source/truncate.c
    source/with_capacity.c
)
r_add_library_module(r_std_string std.string std_string ${R_STD_STRING_PUBLIC_SOURCES})
target_sources(r_std_string PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/source/string_status.c"
)
target_include_directories(r_std_string PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/text/include"
)
target_link_libraries(r_std_string PUBLIC r_std_alloc)
