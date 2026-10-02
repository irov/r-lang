set(R_STD_ARC_PUBLIC_SOURCES
    source/clone.c
    source/clone_weak.c
    source/downgrade.c
    source/from_raw.c
    source/get_mut.c
    source/into_raw.c
    source/ptr_eq.c
    source/strong_count.c
    source/try_unwrap.c
    source/upgrade.c
    source/weak_count.c
)
r_add_library_module(r_std_arc std.arc std_arc ${R_STD_ARC_PUBLIC_SOURCES})
target_sources(r_std_arc PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/ownership/source/arc_status.c"
)
target_include_directories(r_std_arc PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/ownership/include"
)
target_include_directories(r_std_arc PUBLIC "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_link_libraries(r_std_arc PUBLIC r_runtime_0_1)
