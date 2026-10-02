#include <stddef.h>
#include <stdint.h>

/* The C mirror of bench/index_slice.r: a pointer-and-length view without bounds checks. */
static uint32_t fold(const uint32_t *values, size_t length, uint32_t seed) {
    uint32_t accumulator = seed;
    size_t index;

    for (index = 0u; index < length; index += 1u) {
        accumulator = accumulator * 3u + values[index];
    }
    return accumulator;
}

int main(void) {
    const size_t count = 4096u;
    const size_t rounds = 50000u;
    uint32_t data[4096];
    uint32_t state = 12345u;
    uint32_t accumulator = 0u;
    size_t index;
    size_t round;

    for (index = 0u; index < count; index += 1u) {
        state = state * 1664525u + 1013904223u;
        data[index] = state;
    }
    for (round = 0u; round < rounds; round += 1u) {
        accumulator = fold(data, count, accumulator);
    }
    return (int)(accumulator % 109u);
}
