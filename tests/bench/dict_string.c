#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The C mirror of bench/dict_string.r: open addressing over owned key strings, hashing the bytes.
 */
typedef struct {
    char *key;
    size_t length;
    uint32_t value;
    int used;
} slot;
static uint64_t hash(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i += 1) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ull;
    }
    return h ^ (h >> 29);
}
int main(void) {
    size_t cap = 8192;
    slot *table = calloc(cap, sizeof(slot));
    char **keys = malloc(4096 * sizeof(char *));
    size_t *lens = malloc(4096 * sizeof(size_t));
    for (uint32_t i = 0; i < 4096; i += 1) {
        char buf[32];
        int n = snprintf(buf, sizeof buf, "key-%u", i);
        keys[i] = malloc((size_t)n + 1);
        memcpy(keys[i], buf, (size_t)n + 1);
        lens[i] = (size_t)n;
        size_t at = hash(buf, (size_t)n) & (cap - 1);
        while (table[at].used)
            at = (at + 1) & (cap - 1);
        table[at].key = malloc((size_t)n + 1);
        memcpy(table[at].key, buf, (size_t)n + 1);
        table[at].length = (size_t)n;
        table[at].value = i * 3;
        table[at].used = 1;
    }
    uint32_t sum = 0;
    for (size_t n = 0; n < 10000000u; n += 1) {
        size_t k = (n * 7) & 4095;
        size_t at = hash(keys[k], lens[k]) & (cap - 1);
        uint32_t found = 1;
        while (table[at].used) {
            if (table[at].length == lens[k] && memcmp(table[at].key, keys[k], lens[k]) == 0) {
                found = table[at].value;
                break;
            }
            at = (at + 1) & (cap - 1);
        }
        sum += found;
    }
    return (int)(sum % 109u);
}
