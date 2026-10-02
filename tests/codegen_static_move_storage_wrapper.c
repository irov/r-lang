#include "r_runtime_array.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void r_test_array_destroy(RRuntimeArray *array);

#define r_runtime_array_destroy r_test_array_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_array_destroy

static uint8_t r_test_drop_order[10];
static size_t r_test_drop_count;

static void r_test_array_destroy(RRuntimeArray *array) {
    if ((r_test_drop_count < 10U) && (array->length == 1U) && (array->data != NULL)) {
        r_test_drop_order[r_test_drop_count] = *(const uint8_t *)array->data;
    }
    r_test_drop_count += 1U;
    r_runtime_array_destroy(array);
}

int main(int argc, char *argv[]) {
    static const uint8_t expected[] = {10U, 9U, 6U, 4U, 3U, 2U, 8U, 7U, 5U, 1U};
    size_t index;
    int status;

    r_test_drop_count = 0U;
    status = r_generated_main(argc, argv);
    if ((status != 0) || (r_test_drop_count != 10U)) {
        (void)fprintf(
            stderr, "generated status=%d, observed %zu static drops\n", status, r_test_drop_count);
        return 1;
    }
    for (index = 0U; index < 10U; ++index) {
        if (r_test_drop_order[index] != expected[index]) {
            (void)fprintf(stderr,
                          "static drop %zu: expected %u, observed %u\n",
                          index,
                          (unsigned)expected[index],
                          (unsigned)r_test_drop_order[index]);
            return 1;
        }
    }
    return 0;
}
