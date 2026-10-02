set(R_STD_BITS_PUBLIC_SOURCES
    source/align_byte.c
    source/read.c
)
r_add_library_module(r_std_bits std.bits std_bits ${R_STD_BITS_PUBLIC_SOURCES})
