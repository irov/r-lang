#include "r_runtime_allocator.h"
#include "r_runtime_own.h"
#include "r_std_array.h"

#include <stdbool.h>
#include <stdint.h>

static void r_test_own_release(RRuntimeOwn *owner);
static RStdArrayPushResult r_test_push(RStdArray *array, void *value);
#define r_runtime_own_release r_test_own_release
#define r_std_array_push r_test_push
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_std_array_push
#undef r_runtime_own_release

static size_t releases;
static size_t push_calls;
static size_t second_push_length;
static bool valid = true;
static bool failed_growth;

static void r_test_own_release(RRuntimeOwn *owner) {
    static const int32_t expected[] = {29, 17};
    if (owner->allocation == NULL || releases >= 2U ||
        *(int32_t *)owner->allocation != expected[releases]) {
        valid = false;
    }
    ++releases;
    r_runtime_own_release(owner);
}

static RStdArrayPushResult r_test_push(RStdArray *array, void *value) {
    RStdArrayPushResult result;

    ++push_calls;
    if (push_calls == 2U) {
        second_push_length = array->length;
    }
    if (array->length != 1U) {
        return r_std_array_push(array, value);
    }
    r_runtime_allocator_set_failure(array->allocator, UINT64_C(1));
    result = r_std_array_push(array, value);
    failed_growth = r_runtime_allocator_attempt_count(array->allocator) == UINT64_C(1) &&
                    result.status == R_STD_ARRAY_CALL_ERROR;
    r_runtime_allocator_set_failure(array->allocator, UINT64_C(0));
    return result;
}

int main(int argc, char *argv[]) {
    int status = r_generated_main(argc, argv);
    if (status != 0) {
        if (push_calls != 2U) {
            return 40;
        }
        if (second_push_length != 1U) {
            return 41;
        }
        return 42 + status;
    }
    if (!valid) {
        return 10;
    }
    if (releases != 2U) {
        return 20;
    }
    return failed_growth ? 0 : 30;
}
