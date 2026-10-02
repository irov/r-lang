#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The C mirror of bench/format_int.r: snprintf into a fresh heap string per iteration. */
int main(void) {
    const size_t iterations = (size_t)5000000u;
    size_t total = 0u;
    size_t index;

    for (index = 0u; index < iterations; index += 1u) {
        char *text = malloc(32u);
        int written;
        if (text == NULL) {
            return 71;
        }
        written = snprintf(text, 32u, "%zu", index);
        if (written < 0) {
            free(text);
            return 71;
        }
        total += strlen(text);
        free(text);
    }
    return (int)(total % 109u);
}
