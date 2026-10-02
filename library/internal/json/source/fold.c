#include "r_library_json_internal.h"

static uint32_t r_json_fold_next(RStdJsonByteView text, size_t *offset) {
    uint32_t scalar;
    uint32_t minimum;
    unsigned remaining;
    uint8_t c;
    while (*offset < text.length && (text.data[*offset] == '_' || text.data[*offset] == '-'))
        ++*offset;
    if (*offset == text.length)
        return 0x110000U;
    c = text.data[(*offset)++];
    if (c < 0x80U)
        return r_json_simple_fold(c);
    if (c >= 0xc2U && c <= 0xdfU) {
        scalar = c & 0x1fU;
        minimum = 0x80U;
        remaining = 1U;
    } else if (c >= 0xe0U && c <= 0xefU) {
        scalar = c & 15U;
        minimum = 0x800U;
        remaining = 2U;
    } else if (c >= 0xf0U && c <= 0xf4U) {
        scalar = c & 7U;
        minimum = 0x10000U;
        remaining = 3U;
    } else
        return UINT32_MAX;
    while (remaining-- != 0U) {
        if (*offset == text.length)
            return UINT32_MAX;
        c = text.data[(*offset)++];
        if ((c & 0xc0U) != 0x80U)
            return UINT32_MAX;
        scalar = (scalar << 6U) | (c & 0x3fU);
    }
    if (scalar < minimum || scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU))
        return UINT32_MAX;
    return r_json_simple_fold(scalar);
}
bool r_json_name_equal(RStdJsonByteView left, RStdJsonByteView right, bool ignore_case) {
    size_t a = 0U, b = 0U;
    if (!ignore_case)
        return r_json_view_equal(left, right);
    for (;;) {
        uint32_t x = r_json_fold_next(left, &a);
        uint32_t y = r_json_fold_next(right, &b);
        if (x == UINT32_MAX || y == UINT32_MAX || x != y)
            return false;
        if (x == 0x110000U)
            return true;
    }
}
