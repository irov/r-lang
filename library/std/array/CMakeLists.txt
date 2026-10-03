set(R_STD_ARRAY_PUBLIC_SOURCES
    source/as_slice.c
    source/as_slice_mut.c
    source/capacity.c
    source/clear.c
    source/create.c
    source/filled.c
    source/get.c
    source/get_mut.c
    source/pop.c
    source/push.c
    source/remove.c
    source/reserve.c
    source/with_capacity.c
)
r_add_library_module(r_std_array std.array std_array ${R_STD_ARRAY_PUBLIC_SOURCES})
target_sources(r_std_array PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/source/array_status.c"
)
target_include_directories(r_std_array PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/containers/include"
)
target_link_libraries(r_std_array PUBLIC r_std_alloc)
