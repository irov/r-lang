#include <stddef.h>
#include <stdint.h>

/* The C mirror of bench/async_scoped_start.r: the same computation as direct calls. */
static uint32_t step(uint32_t accumulator, uint32_t value) {
    return (accumulator ^ value) * 31u + 7u;
}

int main(void) {
    const size_t iterations = (size_t)2000000u;
    uint32_t accumulator = 0u;
    uint32_t value = 0u;
    size_t index;

    for (index = 0u; index < iterations; index += 1u) {
        accumulator = step(accumulator, value);
        value += 1u;
    }
    return (int)(accumulator % 109u);
}
