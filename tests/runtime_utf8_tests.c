#include "r_runtime_utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int r_runtime_utf8_fail(const char *message) {
    (void)fprintf(stderr, "runtime UTF-8 test failure: %s\n", message);
    return 1;
}

int main(void) {
    static const uint8_t valid[] = {
        UINT8_C(0x41),
        UINT8_C(0xc2),
        UINT8_C(0xa2),
        UINT8_C(0xe2),
        UINT8_C(0x82),
        UINT8_C(0xac),
        UINT8_C(0xf4),
        UINT8_C(0x8f),
        UINT8_C(0xbf),
        UINT8_C(0xbf),
    };
    static const uint8_t overlong[] = {UINT8_C(0xc0), UINT8_C(0x80)};
    static const uint8_t surrogate[] = {
        UINT8_C(0x61),
        UINT8_C(0xed),
        UINT8_C(0xa0),
        UINT8_C(0x80),
    };
    static const uint8_t truncated[] = {UINT8_C(0x62), UINT8_C(0xe2), UINT8_C(0x82)};
    static const uint8_t encoded_max[] = {
        UINT8_C(0xf4),
        UINT8_C(0x8f),
        UINT8_C(0xbf),
        UINT8_C(0xbf),
    };
    uint8_t encoded[4] = {0U, 0U, 0U, 0U};
    size_t length = 0U;
    size_t invalid = 0U;

    if (!r_runtime_utf8_validate(valid, sizeof(valid), &invalid) || (invalid != sizeof(valid))) {
        return r_runtime_utf8_fail("valid sequence");
    }
    if (r_runtime_utf8_validate(overlong, sizeof(overlong), &invalid) || (invalid != 0U)) {
        return r_runtime_utf8_fail("overlong sequence");
    }
    if (r_runtime_utf8_validate(surrogate, sizeof(surrogate), &invalid) || (invalid != 1U)) {
        return r_runtime_utf8_fail("surrogate sequence");
    }
    if (r_runtime_utf8_validate(truncated, sizeof(truncated), &invalid) || (invalid != 1U)) {
        return r_runtime_utf8_fail("truncated sequence");
    }
    if (!r_runtime_utf8_is_scalar_boundary(valid, sizeof(valid), 1U) ||
        r_runtime_utf8_is_scalar_boundary(valid, sizeof(valid), 2U) ||
        !r_runtime_utf8_is_scalar_boundary(valid, sizeof(valid), 3U) ||
        r_runtime_utf8_is_scalar_boundary(valid, sizeof(valid), sizeof(valid) + 1U)) {
        return r_runtime_utf8_fail("scalar boundaries");
    }
    if (!r_runtime_utf8_encode(UINT32_C(0x10ffff), encoded, &length) ||
        (length != sizeof(encoded_max)) ||
        (memcmp(encoded, encoded_max, sizeof(encoded_max)) != 0) ||
        r_runtime_utf8_encode(UINT32_C(0xd800), encoded, &length) ||
        r_runtime_utf8_encode(UINT32_C(0x110000), encoded, &length)) {
        return r_runtime_utf8_fail("scalar encoding");
    }
    (void)puts("runtime_utf8_ok");
    return 0;
}
