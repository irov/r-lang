#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The C mirror of bench/string_append.r: a doubling byte buffer with memcpy appends. */
int main(void) {
    const size_t iterations = (size_t)20000000u;
    unsigned char *bytes = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    size_t total = 0u;
    size_t index;

    for (index = 0u; index < iterations; index += 1u) {
        if (length + 8u > capacity) {
            size_t next = capacity == 0u ? 16u : capacity * 2u;
            unsigned char *grown = realloc(bytes, next);
            if (grown == NULL) {
                free(bytes);
                return 71;
            }
            bytes = grown;
            capacity = next;
        }
        memcpy(bytes + length, "abcdefgh", 8u);
        length += 8u;
        total += length;
        if ((index & 4095u) == 4095u) {
            length = 0u;
        }
    }
    free(bytes);
    return (int)(total % 109u);
}
