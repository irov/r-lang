#include "r_runtime_0_1.h"
#include "r_runtime_array.h"

#include <stddef.h>
#include <stdint.h>

static void r_test_array_destroy(RRuntimeArray *array);

#define r_runtime_array_destroy r_test_array_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_array_destroy

static size_t r_test_array_drop_count;

static void r_test_array_destroy(RRuntimeArray *array) {
    ++r_test_array_drop_count;
    r_runtime_array_destroy(array);
}

int main(int argc, char *argv[]) {
    const int generated_status = r_generated_main(argc, argv);

    if (generated_status != 0) {
        return generated_status;
    }
    if (r_test_array_drop_count != 9U) {
        return (int)__LINE__;
    }
    return 0;
}
