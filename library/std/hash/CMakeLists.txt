set(R_STD_HASH_PUBLIC_SOURCES
    source/crc32.c
    source/md5.c
    source/sha1.c
    source/sha256.c
    source/sha512.c
)
r_add_library_module(r_std_hash std.hash std_hash ${R_STD_HASH_PUBLIC_SOURCES})
