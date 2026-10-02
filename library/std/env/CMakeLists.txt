set(R_STD_ENV_PUBLIC_SOURCES
    source/arguments.c
    source/as_error.c
    source/get.c
    source/remove.c
    source/set.c
    source/variables.c
)
r_add_library_module(r_std_env std.env std_env ${R_STD_ENV_PUBLIC_SOURCES})
target_sources(r_std_env PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/environment/source/environment.c"
)
target_include_directories(r_std_env PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/environment/include"
)
target_include_directories(r_std_env PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/error/include"
)
find_package(Threads REQUIRED)
target_link_libraries(r_std_env PUBLIC
    r_std_array
    r_std_dict
    r_std_string
    Threads::Threads
)
