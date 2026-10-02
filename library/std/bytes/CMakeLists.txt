set(R_STD_BYTES_PUBLIC_SOURCES
    source/append.c
    source/append_u8.c
    source/append_u16_le.c
    source/append_u32_le.c
    source/append_u64_le.c
    source/compare.c
    source/copy.c
    source/copy_within.c
    source/ends_with.c
    source/equal.c
    source/fill.c
    source/find.c
    source/find_slice.c
    source/starts_with.c
    source/with_capacity.c
)
r_add_library_module(r_std_bytes std.bytes std_bytes ${R_STD_BYTES_PUBLIC_SOURCES})
target_sources(r_std_bytes PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/source/bytes.c"
)
target_include_directories(r_std_bytes PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/include"
)
target_link_libraries(r_std_bytes PUBLIC r_std_alloc)
