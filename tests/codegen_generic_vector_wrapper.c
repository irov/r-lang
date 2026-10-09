#include "r_runtime_own.h"
#include "r_std_array.h"
#include <stdbool.h>
#include <stdint.h>

void r_test_own_release(RRuntimeOwn *owner);
RStdArrayPushResult r_test_push(RStdArray *array, void *value);
RStdArrayAllocValueResult
r_test_with_capacity(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity);
#define r_runtime_own_release r_test_own_release
#define r_std_array_push r_test_push
#define r_std_array_with_capacity r_test_with_capacity
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_array_with_capacity
#undef r_std_array_push
#undef r_runtime_own_release

static size_t releases;
static size_t pushes;
static bool valid = true;

void r_test_own_release(RRuntimeOwn *owner) {
    static const int32_t expected[] = {17, 29};
    if (owner->allocation == NULL || releases >= 2U ||
        *(int32_t *)owner->allocation != expected[releases]) {
        valid = false;
    }
    ++releases;
    r_runtime_own_release(owner);
}

/* The generated push helper only reaches the library entry when the array must grow, so the
 * reserved capacity is dropped: every R push then goes through r_std_array_push. */
RStdArrayAllocValueResult
r_test_with_capacity(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity) {
    (void)capacity;
    return r_std_array_with_capacity(allocator, element, 0U);
}

RStdArrayPushResult r_test_push(RStdArray *array, void *value) {
    ++pushes;
    if (pushes == 3U) {
        return (RStdArrayPushResult){R_STD_ARRAY_CALL_ERROR, R_STD_ALLOC_ERROR_OUT_OF_MEMORY};
    }
    return r_std_array_push(array, value);
}

int main(int argc, char *argv[]) {
    int status = r_generated_main(argc, argv);
    return status == 0 && valid && releases == 2U && pushes == 3U ? 0 : 1;
}
