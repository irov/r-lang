#include "r_std_hash.h"

static const uint32_t r_std_hash_crc32_table[16] = {
    UINT32_C(0x00000000),
    UINT32_C(0x1db71064),
    UINT32_C(0x3b6e20c8),
    UINT32_C(0x26d930ac),
    UINT32_C(0x76dc4190),
    UINT32_C(0x6b6b51f4),
    UINT32_C(0x4db26158),
    UINT32_C(0x5005713c),
    UINT32_C(0xedb88320),
    UINT32_C(0xf00f9344),
    UINT32_C(0xd6d6a3e8),
    UINT32_C(0xcb61b38c),
    UINT32_C(0x9b64c2b0),
    UINT32_C(0x86d3d2d4),
    UINT32_C(0xa00ae278),
    UINT32_C(0xbdbdf21c),
};

uint32_t r_std_hash_crc32(RStdHashByteView source) {
    uint32_t crc = UINT32_MAX;
    size_t byte_index;

    for (byte_index = 0U; byte_index < source.length; ++byte_index) {
        crc ^= (uint32_t)source.data[byte_index];
        crc = (crc >> 4U) ^ r_std_hash_crc32_table[crc & UINT32_C(0x0f)];
        crc = (crc >> 4U) ^ r_std_hash_crc32_table[crc & UINT32_C(0x0f)];
    }
    return crc ^ UINT32_MAX;
}
