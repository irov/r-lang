#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/array_push.r: a doubling vector of uint32_t. */
int main(void) {
    const size_t iterations = (size_t)50000000u;
    size_t capacity = 0u;
    size_t length = 0u;
    uint32_t *values = NULL;
    uint32_t total = 0u;
    size_t index;

    for (index = 0u; index < iterations; index += 1u) {
        if (length == capacity) {
            size_t next = capacity == 0u ? 4u : capacity * 2u;
            uint32_t *grown = realloc(values, next * sizeof(uint32_t));
            if (grown == NULL) {
                free(values);
                return 71;
            }
            values = grown;
            capacity = next;
        }
        values[length] = (uint32_t)index * 2654435761u;
        length += 1u;
    }
    for (index = 0u; index < length; index += 1u) {
        total ^= values[index];
    }
    free(values);
    return (int)(total % 109u);
}
