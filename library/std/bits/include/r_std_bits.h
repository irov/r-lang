#ifndef R_STD_BITS_H
#define R_STD_BITS_H

#include <stddef.h>
#include <stdint.h>

/* A zero-length view may carry a null data pointer. The view never owns its storage. */
typedef struct RStdBitsByteView {
    const uint8_t *data;
    size_t length;
} RStdBitsByteView;

/*
 * Copy reader state. Its all-zero representation is a reader positioned at the first bit.
 * Library-produced canonical states keep bit_count in 0..7 and hold limited to those low bits.
 */
typedef struct RStdBitsLsbReader {
    size_t byte_index;
    uint64_t hold;
    uint8_t bit_count;
} RStdBitsLsbReader;

typedef enum RStdBitsReadErrorCode {
    R_STD_BITS_READ_ERROR_UNEXPECTED_END = 0,
    R_STD_BITS_READ_ERROR_INVALID_WIDTH = 1
} RStdBitsReadErrorCode;

typedef struct RStdBitsReadError {
    RStdBitsReadErrorCode code;
    size_t byte_index;
} RStdBitsReadError;

typedef struct RStdBitsReadResult {
    _Bool is_ok;
    uint64_t value;
    RStdBitsReadError error;
} RStdBitsReadResult;

/*
 * Ownership: input is a shared call-bounded borrow and reader is an exclusive call-bounded
 * borrow. Widths above 64 and incoming bit counts above 64 report invalid width. Success advances
 * reader; every failure preserves reader byte-for-byte. The all-zero state or a state produced by
 * this module is canonical. No allocation occurs.
 */
RStdBitsReadResult
r_std_bits_read(RStdBitsByteView input, RStdBitsLsbReader *reader, uint8_t width);

/*
 * Ownership: reader is an exclusive call-bounded borrow. Partial-byte buffered bits are discarded
 * so that the next read begins at a byte boundary; complete buffered bytes are preserved. No
 * allocation or failure occurs.
 */
void r_std_bits_align_byte(RStdBitsLsbReader *reader);

#endif
