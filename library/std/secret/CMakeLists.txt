set(R_STD_SECRET_PUBLIC_SOURCES
    source/as_slice.c
    source/as_slice_mut.c
    source/constant_time_equal.c
    source/from_bytes.c
    source/len.c
    source/with_length.c
    source/zeroize.c
)
r_add_library_module(r_std_secret std.secret std_secret ${R_STD_SECRET_PUBLIC_SOURCES})
target_link_libraries(r_std_secret PUBLIC r_std_alloc)
