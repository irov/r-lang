set(R_STD_IO_PUBLIC_SOURCES
    source/as_error.c
    source/close_input.c
    source/close_output.c
    source/flush.c
    source/read.c
    source/read_into.c
    source/stderr.c
    source/stdin.c
    source/stdout.c
    source/write.c
    source/write_all.c
    source/write_all_from.c
    source/write_from.c
    source/write_shared.c
)
r_add_library_module(r_std_io std.io std_io ${R_STD_IO_PUBLIC_SOURCES})
target_sources(r_std_io PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/io/source/async_close.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/io/source/async_stream.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/io/source/console.c"
)
target_include_directories(r_std_io PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/io/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/runtime/darwin/include"
)
target_include_directories(r_std_io PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/async/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/error/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/time/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/runtime/darwin/include"
)
target_link_libraries(r_std_io PUBLIC
    r_std_arc
    r_std_array
    r_std_async
    r_std_time
)
