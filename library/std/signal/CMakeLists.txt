set(R_STD_SIGNAL_PUBLIC_SOURCES
    source/listen.c
    source/next.c
    source/raise.c
)
r_add_library_module(r_std_signal std.signal std_signal ${R_STD_SIGNAL_PUBLIC_SOURCES})
target_sources(r_std_signal PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/signal/source/async_next.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/signal/source/listener.c"
)
target_include_directories(r_std_signal PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/process/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/signal/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/include"
)
target_link_libraries(r_std_signal
    PUBLIC r_std_process
    PRIVATE r_runtime_0_1
)
