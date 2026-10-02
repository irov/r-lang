set(R_STD_THREAD_PUBLIC_SOURCES
    source/clone_thread.c
    source/current.c
    source/detach.c
    source/join.c
    source/panic_category.c
    source/panic_text.c
    source/park.c
    source/sleep_nanoseconds.c
    source/spawn.c
    source/spawn_scoped.c
    source/unpark.c
    source/yield_now.c
)
r_add_library_module(r_std_thread std.thread std_thread ${R_STD_THREAD_PUBLIC_SOURCES})
target_sources(r_std_thread PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/thread/source/thread.c"
)
target_include_directories(r_std_thread
    PUBLIC
        "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include"
        "${R_LIBRARY_REPOSITORY_ROOT}/library/std/string/include"
    PRIVATE "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/thread/include"
)
target_link_libraries(r_std_thread PUBLIC r_runtime_0_1 r_std_string Threads::Threads)

if(R_FRONTEND_BUILD_TESTS)
    target_compile_definitions(r_std_thread PRIVATE R_STD_THREAD_TESTING=1)
endif()
