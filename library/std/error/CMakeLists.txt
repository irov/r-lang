set(R_STD_ERROR_PUBLIC_SOURCES
    source/diagnostic.c
    source/from_address.c
    source/from_alloc.c
    source/from_async.c
    source/from_barrier.c
    source/from_boundary.c
    source/from_bytes.c
    source/from_duration.c
    source/from_format.c
    source/from_path.c
    source/from_parse.c
    source/from_range.c
    source/from_string.c
    source/from_thread.c
    source/name.c
)
r_add_library_module(r_std_error std.error std_error ${R_STD_ERROR_PUBLIC_SOURCES})
target_include_directories(r_std_error PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/net/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/sync/include"
)
target_link_libraries(r_std_error PUBLIC
    r_std_async
    r_std_bytes
    r_std_fs
    r_std_string
    r_std_thread
    r_std_time
)
