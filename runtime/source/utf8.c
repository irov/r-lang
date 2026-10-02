#include "r_runtime_utf8.h"

#include <stddef.h>
#include <stdint.h>

static _Bool r_runtime_utf8_continuation(uint8_t byte) {
    return (byte >= UINT8_C(0x80)) && (byte <= UINT8_C(0xbf));
}

_Bool r_runtime_utf8_validate(const uint8_t *bytes, size_t length, size_t *invalid_index) {
    size_t index = 0U;

    if ((bytes == NULL) && (length != 0U)) {
        if (invalid_index != NULL) {
            *invalid_index = 0U;
        }
        return 0;
    }
    while (index < length) {
        const uint8_t first = bytes[index];
        size_t width;
        _Bool valid;

        if (first <= UINT8_C(0x7f)) {
            width = 1U;
            valid = 1;
        } else if ((first >= UINT8_C(0xc2)) && (first <= UINT8_C(0xdf))) {
            width = 2U;
            valid = ((length - index) >= width) && r_runtime_utf8_continuation(bytes[index + 1U]);
        } else if (first == UINT8_C(0xe0)) {
            width = 3U;
            valid = ((length - index) >= width) && (bytes[index + 1U] >= UINT8_C(0xa0)) &&
                    (bytes[index + 1U] <= UINT8_C(0xbf)) &&
                    r_runtime_utf8_continuation(bytes[index + 2U]);
        } else if (((first >= UINT8_C(0xe1)) && (first <= UINT8_C(0xec))) ||
                   ((first >= UINT8_C(0xee)) && (first <= UINT8_C(0xef)))) {
            width = 3U;
            valid = ((length - index) >= width) && r_runtime_utf8_continuation(bytes[index + 1U]) &&
                    r_runtime_utf8_continuation(bytes[index + 2U]);
        } else if (first == UINT8_C(0xed)) {
            width = 3U;
            valid = ((length - index) >= width) && (bytes[index + 1U] >= UINT8_C(0x80)) &&
                    (bytes[index + 1U] <= UINT8_C(0x9f)) &&
                    r_runtime_utf8_continuation(bytes[index + 2U]);
        } else if (first == UINT8_C(0xf0)) {
            width = 4U;
            valid = ((length - index) >= width) && (bytes[index + 1U] >= UINT8_C(0x90)) &&
                    (bytes[index + 1U] <= UINT8_C(0xbf)) &&
                    r_runtime_utf8_continuation(bytes[index + 2U]) &&
                    r_runtime_utf8_continuation(bytes[index + 3U]);
        } else if ((first >= UINT8_C(0xf1)) && (first <= UINT8_C(0xf3))) {
            width = 4U;
            valid = ((length - index) >= width) && r_runtime_utf8_continuation(bytes[index + 1U]) &&
                    r_runtime_utf8_continuation(bytes[index + 2U]) &&
                    r_runtime_utf8_continuation(bytes[index + 3U]);
        } else if (first == UINT8_C(0xf4)) {
            width = 4U;
            valid = ((length - index) >= width) && (bytes[index + 1U] >= UINT8_C(0x80)) &&
                    (bytes[index + 1U] <= UINT8_C(0x8f)) &&
                    r_runtime_utf8_continuation(bytes[index + 2U]) &&
                    r_runtime_utf8_continuation(bytes[index + 3U]);
        } else {
            width = 1U;
            valid = 0;
        }
        if (!valid) {
            if (invalid_index != NULL) {
                *invalid_index = index;
            }
            return 0;
        }
        index += width;
    }
    if (invalid_index != NULL) {
        *invalid_index = length;
    }
    return 1;
}

_Bool r_runtime_utf8_is_scalar_boundary(const uint8_t *bytes, size_t length, size_t index) {
    if ((index > length) || ((bytes == NULL) && (length != 0U))) {
        return 0;
    }
    return (index == 0U) || (index == length) || !r_runtime_utf8_continuation(bytes[index]);
}

_Bool r_runtime_utf8_encode(uint32_t scalar, uint8_t output[4], size_t *length) {
    if ((output == NULL) || (length == NULL) || (scalar > UINT32_C(0x10ffff)) ||
        ((scalar >= UINT32_C(0xd800)) && (scalar <= UINT32_C(0xdfff)))) {
        return 0;
    }
    if (scalar <= UINT32_C(0x7f)) {
        output[0] = (uint8_t)scalar;
        *length = 1U;
    } else if (scalar <= UINT32_C(0x7ff)) {
        output[0] = (uint8_t)(UINT32_C(0xc0) | (scalar >> 6U));
        output[1] = (uint8_t)(UINT32_C(0x80) | (scalar & UINT32_C(0x3f)));
        *length = 2U;
    } else if (scalar <= UINT32_C(0xffff)) {
        output[0] = (uint8_t)(UINT32_C(0xe0) | (scalar >> 12U));
        output[1] = (uint8_t)(UINT32_C(0x80) | ((scalar >> 6U) & UINT32_C(0x3f)));
        output[2] = (uint8_t)(UINT32_C(0x80) | (scalar & UINT32_C(0x3f)));
        *length = 3U;
    } else {
        output[0] = (uint8_t)(UINT32_C(0xf0) | (scalar >> 18U));
        output[1] = (uint8_t)(UINT32_C(0x80) | ((scalar >> 12U) & UINT32_C(0x3f)));
        output[2] = (uint8_t)(UINT32_C(0x80) | ((scalar >> 6U) & UINT32_C(0x3f)));
        output[3] = (uint8_t)(UINT32_C(0x80) | (scalar & UINT32_C(0x3f)));
        *length = 4U;
    }
    return 1;
}
