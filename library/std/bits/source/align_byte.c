#include "r_std_bits.h"

void r_std_bits_align_byte(RStdBitsLsbReader *reader) {
    const uint8_t discard = (uint8_t)(reader->bit_count % 8U);

    reader->hold >>= discard;
    reader->bit_count = (uint8_t)(reader->bit_count - discard);
}
