#include <stddef.h>
#include <stdint.h>

/* The C mirror of bench/checked_arith.r: plain int64_t arithmetic without overflow checks. */
int main(void) {
    const size_t iterations = (size_t)200000000u;
    uint32_t state = 12345u;
    int64_t total = 0;
    int64_t reduced;
    size_t index;

    for (index = 0u; index < iterations; index += 1u) {
        state = state * 1664525u + 1013904223u;
        total = total + (int64_t)(state >> 24) - 100;
    }
    reduced = ((total % 109) + 109) % 109;
    return (int)reduced;
}
