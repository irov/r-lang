#include "r_runtime_own.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void r_test_own_release(RRuntimeOwn *owner);

#define r_runtime_own_release r_test_own_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_own_release

static size_t r_test_own_release_count;
static bool r_test_drop_order;
static const int32_t expected[] = {9, 99, 9, 99, 9, 99};

static void r_test_own_release(RRuntimeOwn *owner) {
    if ((owner->allocation == NULL) ||
        (r_test_own_release_count >= sizeof(expected) / sizeof(expected[0])) ||
        (*(const int32_t *)owner->allocation != expected[r_test_own_release_count])) {
        r_test_drop_order = false;
    }
    r_test_own_release_count += 1U;
    r_runtime_own_release(owner);
}

int main(int argc, char *argv[]) {
    int status;

    r_test_own_release_count = 0U;
    r_test_drop_order = true;
    status = r_generated_main(argc, argv);
    if ((status != 0) || !r_test_drop_order ||
        (r_test_own_release_count != sizeof(expected) / sizeof(expected[0]))) {
        return 1;
    }
    return 0;
}
