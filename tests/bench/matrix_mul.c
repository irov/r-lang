#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/matrix_mul.r: the same product over flat uint32_t arrays. */
static uint32_t multiply(const uint32_t *a, const uint32_t *b, uint32_t *c, size_t n) {
    for (size_t i = 0; i < n; i += 1) {
        for (size_t j = 0; j < n; j += 1) {
            uint32_t sum = 0;
            for (size_t k = 0; k < n; k += 1)
                sum += a[i * n + k] * b[k * n + j];
            c[i * n + j] = sum;
        }
    }
    return c[n + 1];
}
int main(void) {
    size_t n = 96;
    uint32_t *a = calloc(n * n, 4), *b = calloc(n * n, 4), *c = calloc(n * n, 4);
    for (size_t i = 0; i < n * n; i += 1) {
        a[i] = (uint32_t)((i * 7) % 13);
        b[i] = (uint32_t)((i * 5) % 11);
    }
    uint32_t check = 0;
    for (size_t round = 0; round < 200; round += 1) {
        check += multiply(a, b, c, n);
        a[round] += 1;
    }
    free(a);
    free(b);
    free(c);
    return (int)(check % 109u);
}
