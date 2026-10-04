#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/struct_update.r: the same particle update over a heap array. */
typedef struct {
    int32_t x, y, vx, vy;
} particle;
int main(void) {
    particle *items = malloc(4096 * sizeof(particle));
    for (int32_t i = 0; i < 4096; i += 1)
        items[i] = (particle){i % 640, (i * 7) % 480, (i % 5) - 2, (i % 7) - 3};
    int64_t check = 0;
    for (int32_t step = 0; step < 10000; step += 1) {
        for (size_t i = 0; i < 4096; i += 1) {
            items[i].x += items[i].vx;
            items[i].y += items[i].vy;
            if (items[i].x < 0 || items[i].x >= 640)
                items[i].vx = -items[i].vx;
            if (items[i].y < 0 || items[i].y >= 480)
                items[i].vy = -items[i].vy;
        }
        check += items[(size_t)step % 4096].x;
    }
    free(items);
    return (int)(check % 109);
}
