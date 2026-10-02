#include "r_std_random.h"

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

/* R-SLIB-RANDOM-0001: an empty view is left alone, even with a null data pointer. */
static int r_test_empty(void) {
    uint8_t guard[4] = {7U, 7U, 7U, 7U};
    r_std_random_fill((RStdRandomMutSlice){NULL, 0U});
    r_std_random_fill((RStdRandomMutSlice){guard, 0U});
    R_TEST_CHECK(guard[0] == 7U && guard[3] == 7U);
    return 0;
}

/* Every length is filled exactly: the bytes beyond the view keep their value, and two fills of
   64 bytes differ (a collision has probability 2^-512). */
static int r_test_lengths(void) {
    uint8_t first[80];
    uint8_t second[80];
    size_t length;

    for (length = 1U; length <= 64U; length += 1U) {
        (void)memset(first, 0xa5, sizeof(first));
        r_std_random_fill((RStdRandomMutSlice){first, length});
        R_TEST_CHECK(first[length] == 0xa5U && first[79] == 0xa5U);
    }
    (void)memset(first, 0, sizeof(first));
    (void)memset(second, 0, sizeof(second));
    r_std_random_fill((RStdRandomMutSlice){first, 64U});
    r_std_random_fill((RStdRandomMutSlice){second, 64U});
    R_TEST_CHECK(memcmp(first, second, 64U) != 0);
    return 0;
}

/* Over 256 KiB every byte value occurs; a missing value has probability below 2^-1400. */
static int r_test_spread(void) {
    static uint8_t block[262144];
    unsigned counts[256];
    size_t index;

    (void)memset(counts, 0, sizeof(counts));
    r_std_random_fill((RStdRandomMutSlice){block, sizeof(block)});
    for (index = 0U; index < sizeof(block); index += 1U) {
        counts[block[index]] += 1U;
    }
    for (index = 0U; index < 256U; index += 1U) {
        R_TEST_CHECK(counts[index] != 0U);
    }
    return 0;
}

int main(void) {
    if ((r_test_empty() != 0) || (r_test_lengths() != 0) || (r_test_spread() != 0)) {
        return 1;
    }
    return 0;
}
