set(R_STD_RANDOM_PUBLIC_SOURCES
    source/fill.c
)
r_add_library_module(r_std_random std.random std_random ${R_STD_RANDOM_PUBLIC_SOURCES})
