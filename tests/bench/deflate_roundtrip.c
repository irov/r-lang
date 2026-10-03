#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>

/* The C mirror of bench/deflate_roundtrip.r: compress2/uncompress from the system zlib. */
int main(void) {
    const size_t iterations = (size_t)200u;
    unsigned char *payload = malloc(65536u);
    unsigned char *packed = malloc(compressBound(65536u));
    unsigned char *restored = malloc(65536u);
    size_t total = 0u;

    if (payload == NULL || packed == NULL || restored == NULL)
        return 71;
    for (size_t index = 0; index < 65536u; ++index) {
        payload[index] = (unsigned char)(((index * 31u + index / 7u) % 96u) + 32u);
    }
    for (size_t round = 0; round < iterations; ++round) {
        uLongf packed_length = compressBound(65536u);
        uLongf restored_length = 65536u;
        if (compress2(packed, &packed_length, payload, 65536u, 6) != Z_OK)
            return 65;
        if (uncompress(restored, &restored_length, packed, packed_length) != Z_OK)
            return 65;
        if (restored_length != 65536u)
            return 70;
        total += (size_t)restored_length + restored[(round * 1000u) % 65536u];
    }
    free(payload);
    free(packed);
    free(restored);
    return (int)(total % 109u);
}
