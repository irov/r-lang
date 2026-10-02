#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/own_alloc.r: malloc and free one uint32_t per iteration. The volatile
   sink keeps the allocation observable so the optimizer cannot elide the pair. */
static uint32_t *volatile sink;

int main(void) {
    const size_t iterations = (size_t)20000000u;
    uint32_t total = 0u;
    size_t index;

    for (index = 0u; index < iterations; index += 1u) {
        uint32_t *boxed = malloc(sizeof(uint32_t));
        if (boxed == NULL) {
            return 71;
        }
        *boxed = (uint32_t)index;
        sink = boxed;
        total ^= *sink;
        free(boxed);
    }
    return (int)(total % 109u);
}
