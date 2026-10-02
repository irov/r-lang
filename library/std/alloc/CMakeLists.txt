set(R_STD_ALLOC_PUBLIC_SOURCES
    source/bytes.c
    source/into_value.c
    source/try_new.c
)
r_add_library_module(r_std_alloc std.alloc std_alloc ${R_STD_ALLOC_PUBLIC_SOURCES})
target_include_directories(r_std_alloc PUBLIC "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_link_libraries(r_std_alloc PUBLIC r_runtime_0_1)
