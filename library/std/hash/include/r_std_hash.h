#ifndef R_STD_HASH_H
#define R_STD_HASH_H

#include <stddef.h>
#include <stdint.h>

/* A zero-length view may carry a null data pointer; every nonzero view names a valid region. */
typedef struct RStdHashByteView {
    const uint8_t *data;
    size_t length;
} RStdHashByteView;

typedef struct RStdHashMd5Digest {
    uint8_t bytes[16];
} RStdHashMd5Digest;

typedef struct RStdHashSha1Digest {
    uint8_t bytes[20];
} RStdHashSha1Digest;

typedef struct RStdHashSha256Digest {
    uint8_t bytes[32];
} RStdHashSha256Digest;

typedef struct RStdHashSha512Digest {
    uint8_t bytes[64];
} RStdHashSha512Digest;

/* Ownership: source is a shared call-bounded borrow. No allocation or mutation occurs. */
uint32_t r_std_hash_crc32(RStdHashByteView source);
RStdHashMd5Digest r_std_hash_md5(RStdHashByteView source);
RStdHashSha1Digest r_std_hash_sha1(RStdHashByteView source);
RStdHashSha256Digest r_std_hash_sha256(RStdHashByteView source);
RStdHashSha512Digest r_std_hash_sha512(RStdHashByteView source);

#endif
