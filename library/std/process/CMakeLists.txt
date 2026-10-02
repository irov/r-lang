set(R_STD_PROCESS_PUBLIC_SOURCES
    source/abort.c
    source/arg.c
    source/as_error.c
    source/clear_environment.c
    source/command_create.c
    source/environment.c
    source/exit.c
    source/id.c
    source/remove_environment.c
    source/set_stdio.c
    source/spawn.c
    source/take_stderr.c
    source/take_stdin.c
    source/take_stdout.c
    source/terminate.c
    source/wait.c
    source/working_directory.c
)
r_add_library_module(r_std_process std.process std_process ${R_STD_PROCESS_PUBLIC_SOURCES})
target_sources(r_std_process PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/async_terminate.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/async_wait.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/async_spawn.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/async_support.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/child.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/command.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/source/native_text.c"
)
target_include_directories(r_std_process PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/filesystem/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/include"
)
target_include_directories(r_std_process PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/error/include"
)
target_link_libraries(r_std_process
    PUBLIC r_std_fs r_std_io
    PRIVATE r_std_env r_std_time r_runtime_0_1
)
if(R_FRONTEND_BUILD_TESTS)
    target_compile_definitions(r_std_process PRIVATE R_LIBRARY_PROCESS_TESTING=1)
endif()
