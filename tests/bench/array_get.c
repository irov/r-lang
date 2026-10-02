#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/array_get.r: a length-carrying vector read through a bounds check. */
typedef struct Vector {
    uint32_t *data;
    size_t length;
} Vector;

int main(void) {
    const size_t iterations = (size_t)200000000u;
    Vector values;
    uint32_t total = 0u;
    size_t index;

    values.data = malloc(4096u * sizeof(uint32_t));
    if (values.data == NULL) {
        return 71;
    }
    values.length = 4096u;
    for (index = 0u; index < 4096u; index += 1u) {
        values.data[index] = (uint32_t)index * 2654435761u;
    }
    for (index = 0u; index < iterations; index += 1u) {
        const size_t slot = index & 4095u;
        const uint32_t *item = slot < values.length ? &values.data[slot] : NULL;
        if (item == NULL) {
            free(values.data);
            return 70;
        }
        total ^= *item + (uint32_t)index;
    }
    free(values.data);
    return (int)(total % 109u);
}
