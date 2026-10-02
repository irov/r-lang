set(R_STD_LIST_PUBLIC_SOURCES
    source/back.c
    source/back_mut.c
    source/clear.c
    source/create.c
    source/front.c
    source/front_mut.c
    source/get.c
    source/get_mut.c
    source/insert_after.c
    source/insert_before.c
    source/iter.c
    source/next.c
    source/pop_back.c
    source/pop_front.c
    source/push_back.c
    source/push_front.c
    source/remove.c
)
r_add_library_module(r_std_list std.list std_list ${R_STD_LIST_PUBLIC_SOURCES})
target_sources(r_std_list PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/source/list_status.c"
)
target_include_directories(r_std_list PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/include"
)
target_link_libraries(r_std_list PUBLIC r_std_alloc)
