set(R_CORE_PUBLIC_SOURCES
    source/validate_utf8.c
)
r_add_library_module(r_core core core ${R_CORE_PUBLIC_SOURCES})
target_include_directories(r_core PUBLIC "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_link_libraries(r_core PUBLIC r_runtime_0_1)
