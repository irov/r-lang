set(R_STD_FS_PUBLIC_SOURCES
    source/as_error.c
    source/close_directory.c
    source/close_file.c
    source/create_directory.c
    source/create_directory_beneath.c
    source/file_metadata.c
    source/flush.c
    source/iterate.c
    source/lock.c
    source/metadata.c
    source/metadata_beneath.c
    source/next.c
    source/open_directory.c
    source/open_directory_beneath.c
    source/open_file.c
    source/open_file_beneath.c
    source/path_clone.c
    source/path_from_utf8.c
    source/path_from_utf8_bytes.c
    source/path_is_absolute.c
    source/path_join.c
    source/path_to_utf8.c
    source/read.c
    source/read_at.c
    source/read_at_into.c
    source/read_file.c
    source/read_into.c
    source/read_file_beneath.c
    source/remove_directory_beneath.c
    source/remove_file_beneath.c
    source/rename_beneath.c
    source/seek.c
    source/sync.c
    source/try_lock.c
    source/unlock.c
    source/write.c
    source/write_all.c
    source/write_all_at.c
    source/write_all_at_from.c
    source/write_all_from.c
    source/write_file_atomic_no_replace.c
    source/write_file_atomic_no_replace_beneath.c
    source/write_from.c
)
r_add_library_module(r_std_fs std.fs std_fs ${R_STD_FS_PUBLIC_SOURCES})
target_sources(r_std_fs PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_close.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_control.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_iteration.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_namespace.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_open.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_payload.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_read_file.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/async_atomic_write.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/handle.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/path.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/source/position_deadline.c"
)
target_include_directories(r_std_fs PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/include"
)
target_include_directories(r_std_fs PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/error/include"
)
target_link_libraries(r_std_fs PUBLIC
    r_std_alloc
    r_std_io
    r_std_string
    r_std_time
)
if(APPLE AND R_FRONTEND_BUILD_TESTS)
    target_compile_definitions(r_std_fs PRIVATE R_RUNTIME_DARWIN_FS_LANE_TESTING=1)
endif()
