#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint64_t r_fuzz_next(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13U;
    value ^= value >> 7U;
    value ^= value << 17U;
    *state = value;
    return value;
}

static size_t r_fuzz_option(int argc, char **argv, const char *prefix, size_t fallback) {
    int index;
    size_t prefix_length = strlen(prefix);
    for (index = 1; index < argc; ++index) {
        if (strncmp(argv[index], prefix, prefix_length) == 0) {
            char *end = NULL;
            unsigned long parsed = strtoul(argv[index] + prefix_length, &end, 10);
            if ((end != NULL) && (*end == '\0') && (parsed <= (unsigned long)SIZE_MAX)) {
                return (size_t)parsed;
            }
        }
    }
    return fallback;
}

int main(int argc, char **argv) {
    size_t runs = r_fuzz_option(argc, argv, "-runs=", 1000U);
    size_t maximum_length = r_fuzz_option(argc, argv, "-max_len=", 4096U);
    uint8_t *bytes;
    uint64_t random_state = UINT64_C(0x6a09e667f3bcc909);
    size_t run;
    if (maximum_length == SIZE_MAX) {
        return 2;
    }
    bytes = malloc(maximum_length + 1U);
    if (bytes == NULL) {
        return 2;
    }
    for (run = 0U; run < runs; ++run) {
        size_t length = maximum_length == 0U
                            ? 0U
                            : (size_t)(r_fuzz_next(&random_state) % (maximum_length + 1U));
        size_t index;
        for (index = 0U; index < length; ++index) {
            bytes[index] = (uint8_t)r_fuzz_next(&random_state);
        }
        (void)LLVMFuzzerTestOneInput(bytes, length);
    }
    free(bytes);
    (void)printf("standalone fuzz: %zu inputs\n", runs);
    return 0;
}
