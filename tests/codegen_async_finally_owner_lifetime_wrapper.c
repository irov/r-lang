#include "r_runtime_own.h"

#include <stddef.h>
#include <stdint.h>

void r_test_own_release(RRuntimeOwn *owner);

#define r_runtime_own_release r_test_own_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release

static size_t r_test_own_release_count;
static int32_t r_test_released_value;

void r_test_own_release(RRuntimeOwn *owner) {
    const int32_t *value = r_runtime_own_get(owner);

    if (value != NULL) {
        r_test_released_value = *value;
    }
    r_test_own_release_count += 1U;
    r_runtime_own_release(owner);
}

int main(int argc, char *argv[]) {
    int status;

    r_test_own_release_count = 0U;
    r_test_released_value = INT32_C(0);
    status = r_generated_main(argc, argv);
    if ((status != 0) || (r_test_own_release_count != 1U) ||
        (r_test_released_value != INT32_C(84))) {
        return 1;
    }
    return 0;
}
