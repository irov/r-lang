set(R_STD_DICT_PUBLIC_SOURCES
    source/clear.c
    source/contains.c
    source/create.c
    source/get.c
    source/get_mut.c
    source/insert.c
    source/iter.c
    source/next.c
    source/remove.c
    source/reserve.c
    source/with_capacity.c
)
r_add_library_module(r_std_dict std.dict std_dict ${R_STD_DICT_PUBLIC_SOURCES})
target_sources(r_std_dict PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/source/dict_status.c"
)
target_include_directories(r_std_dict PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/include"
)
target_link_libraries(r_std_dict PUBLIC r_std_alloc)
