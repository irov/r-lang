#include "r_runtime_own.h"

#include <stdbool.h>
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
static bool r_test_drop_order;

void r_test_own_release(RRuntimeOwn *owner) {
    if ((owner->allocation == NULL) || (*(const int32_t *)owner->allocation != INT32_C(9))) {
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
    if ((status != 0) || !r_test_drop_order || (r_test_own_release_count != 1U)) {
        return 1;
    }
    return 0;
}
