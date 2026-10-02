set(R_STD_TEST_PUBLIC_SOURCES
    source/allocation_attempts.c
    source/fail_allocation_at.c
)
r_add_library_module(r_std_test std.test std_test ${R_STD_TEST_PUBLIC_SOURCES})
target_include_directories(r_std_test PUBLIC "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_link_libraries(r_std_test PUBLIC r_runtime_0_1)
