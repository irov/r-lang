#include "r_std_utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int r_test_expect_valid(const uint8_t *data, size_t length) {
    const RStdUtf8View view = {data, length};
    const RCoreValidateUtf8Result result = r_std_utf8_validate(view);

    R_TEST_CHECK(r_std_utf8_is_valid(view));
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value.data == data);
    R_TEST_CHECK(result.value.length == length);
    return 0;
}

static int r_test_valid_views(void) {
    const uint8_t mixed_text[] = {
        'R',
        UINT8_C(0xc2),
        UINT8_C(0xa2),
        UINT8_C(0xe2),
        UINT8_C(0x82),
        UINT8_C(0xac),
        UINT8_C(0xf0),
        UINT8_C(0x90),
        UINT8_C(0x8d),
        UINT8_C(0x88),
    };
    const struct {
        const uint8_t *data;
        size_t length;
    } cases[] = {
        {(const uint8_t[]){UINT8_C(0x00)}, 1U},
        {(const uint8_t[]){UINT8_C(0x7f)}, 1U},
        {(const uint8_t[]){UINT8_C(0xc2), UINT8_C(0x80)}, 2U},
        {(const uint8_t[]){UINT8_C(0xdf), UINT8_C(0xbf)}, 2U},
        {(const uint8_t[]){UINT8_C(0xe0), UINT8_C(0xa0), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xe0), UINT8_C(0xbf), UINT8_C(0xbf)}, 3U},
        {(const uint8_t[]){UINT8_C(0xe1), UINT8_C(0x80), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xec), UINT8_C(0xbf), UINT8_C(0xbf)}, 3U},
        {(const uint8_t[]){UINT8_C(0xed), UINT8_C(0x80), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xed), UINT8_C(0x9f), UINT8_C(0xbf)}, 3U},
        {(const uint8_t[]){UINT8_C(0xee), UINT8_C(0x80), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xef), UINT8_C(0xbf), UINT8_C(0xbf)}, 3U},
        {(const uint8_t[]){UINT8_C(0xf0), UINT8_C(0x90), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf0), UINT8_C(0xbf), UINT8_C(0xbf), UINT8_C(0xbf)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf3), UINT8_C(0xbf), UINT8_C(0xbf), UINT8_C(0xbf)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf4), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf4), UINT8_C(0x8f), UINT8_C(0xbf), UINT8_C(0xbf)}, 4U},
    };
    size_t index;

    R_TEST_CHECK(r_test_expect_valid(NULL, 0U) == 0);
    R_TEST_CHECK(r_test_expect_valid(mixed_text, sizeof(mixed_text)) == 0);

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        R_TEST_CHECK(r_test_expect_valid(cases[index].data, cases[index].length) == 0);
    }
    return 0;
}

static int r_test_expect_invalid(const uint8_t *data, size_t length) {
    static const uint8_t prefix[] = {
        'R',
        UINT8_C(0xc2),
        UINT8_C(0xa2),
        UINT8_C(0xe2),
        UINT8_C(0x82),
        UINT8_C(0xac),
        UINT8_C(0xf0),
        UINT8_C(0x90),
        UINT8_C(0x8d),
        UINT8_C(0x88),
    };
    uint8_t prefixed[sizeof(prefix) + 4U];
    RStdUtf8View view = {data, length};
    RCoreValidateUtf8Result result = r_std_utf8_validate(view);

    R_TEST_CHECK(!r_std_utf8_is_valid(view));
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error.index == 0U);

    R_TEST_CHECK(length <= sizeof(prefixed) - sizeof(prefix));
    (void)memcpy(prefixed, prefix, sizeof(prefix));
    (void)memcpy(prefixed + sizeof(prefix), data, length);
    view = (RStdUtf8View){prefixed, sizeof(prefix) + length};
    result = r_std_utf8_validate(view);

    R_TEST_CHECK(!r_std_utf8_is_valid(view));
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error.index == sizeof(prefix));
    return 0;
}

static int r_test_invalid_views(void) {
    const struct {
        const uint8_t *data;
        size_t length;
    } cases[] = {
        /* Stray continuation bytes and forbidden C0/C1 overlong leads. */
        {(const uint8_t[]){UINT8_C(0x80)}, 1U},
        {(const uint8_t[]){UINT8_C(0xbf)}, 1U},
        {(const uint8_t[]){UINT8_C(0xc0), UINT8_C(0x80)}, 2U},
        {(const uint8_t[]){UINT8_C(0xc1), UINT8_C(0xbf)}, 2U},

        /* Truncated two-, three-, and four-byte encodings. */
        {(const uint8_t[]){UINT8_C(0xc2)}, 1U},
        {(const uint8_t[]){UINT8_C(0xe0)}, 1U},
        {(const uint8_t[]){UINT8_C(0xe0), UINT8_C(0xa0)}, 2U},
        {(const uint8_t[]){UINT8_C(0xe1)}, 1U},
        {(const uint8_t[]){UINT8_C(0xe1), UINT8_C(0x80)}, 2U},
        {(const uint8_t[]){UINT8_C(0xed)}, 1U},
        {(const uint8_t[]){UINT8_C(0xed), UINT8_C(0x80)}, 2U},
        {(const uint8_t[]){UINT8_C(0xf0)}, 1U},
        {(const uint8_t[]){UINT8_C(0xf0), UINT8_C(0x90)}, 2U},
        {(const uint8_t[]){UINT8_C(0xf0), UINT8_C(0x90), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xf1)}, 1U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80)}, 2U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xf4)}, 1U},
        {(const uint8_t[]){UINT8_C(0xf4), UINT8_C(0x80)}, 2U},
        {(const uint8_t[]){UINT8_C(0xf4), UINT8_C(0x80), UINT8_C(0x80)}, 3U},

        /* A non-continuation byte in every required continuation position. */
        {(const uint8_t[]){UINT8_C(0xc2), UINT8_C(0x7f)}, 2U},
        {(const uint8_t[]){UINT8_C(0xc2), UINT8_C(0xc0)}, 2U},
        {(const uint8_t[]){UINT8_C(0xe1), UINT8_C(0x7f), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xe1), UINT8_C(0xc0), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xe1), UINT8_C(0x80), UINT8_C(0x7f)}, 3U},
        {(const uint8_t[]){UINT8_C(0xe1), UINT8_C(0x80), UINT8_C(0xc0)}, 3U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x7f), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0xc0), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80), UINT8_C(0x7f), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80), UINT8_C(0xc0), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x7f)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf1), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0xc0)}, 4U},

        /* Overlong encodings, UTF-16 surrogates, and values above U+10FFFF. */
        {(const uint8_t[]){UINT8_C(0xe0), UINT8_C(0x80), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xe0), UINT8_C(0x9f), UINT8_C(0xbf)}, 3U},
        {(const uint8_t[]){UINT8_C(0xed), UINT8_C(0xa0), UINT8_C(0x80)}, 3U},
        {(const uint8_t[]){UINT8_C(0xed), UINT8_C(0xbf), UINT8_C(0xbf)}, 3U},
        {(const uint8_t[]){UINT8_C(0xf0), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf0), UINT8_C(0x8f), UINT8_C(0xbf), UINT8_C(0xbf)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf4), UINT8_C(0x90), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf4), UINT8_C(0xbf), UINT8_C(0xbf), UINT8_C(0xbf)}, 4U},

        /* RFC 3629 excludes every historical five-/six-byte or reserved lead. */
        {(const uint8_t[]){UINT8_C(0xf5), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf6), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf7), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0x80)}, 4U},
        {(const uint8_t[]){UINT8_C(0xf8)}, 1U},
        {(const uint8_t[]){UINT8_C(0xf9)}, 1U},
        {(const uint8_t[]){UINT8_C(0xfa)}, 1U},
        {(const uint8_t[]){UINT8_C(0xfb)}, 1U},
        {(const uint8_t[]){UINT8_C(0xfc)}, 1U},
        {(const uint8_t[]){UINT8_C(0xfd)}, 1U},
        {(const uint8_t[]){UINT8_C(0xfe)}, 1U},
        {(const uint8_t[]){UINT8_C(0xff)}, 1U},
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        R_TEST_CHECK(r_test_expect_invalid(cases[index].data, cases[index].length) == 0);
    }
    return 0;
}

int main(void) {
    if (r_test_valid_views() != 0) {
        return 1;
    }
    if (r_test_invalid_views() != 0) {
        return 1;
    }
    return 0;
}
