#include "r_runtime_allocator.h"
#include "r_std_list.h"

#include <stdbool.h>
#include <stdint.h>

RStdListInsertResult r_test_list_push_back(RStdList *target, void *staged_value);

#define r_std_list_push_back r_test_list_push_back
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_list_push_back

static bool borrow_failed;
static bool value_failed;

/* The push allocates one node with the element inline. The first push of a `const Point*` and
 * the first push of an `i32` fail, so that each R catch clause receives its staged element back
 * and checks it. */
RStdListInsertResult r_test_list_push_back(RStdList *target, void *staged_value) {
    bool *failed = NULL;
    RStdListInsertResult result;

    if (!borrow_failed && (target->element.size == sizeof(void *))) {
        failed = &borrow_failed;
    } else if (!value_failed && (target->element.size == sizeof(int32_t))) {
        failed = &value_failed;
    }
    if (failed == NULL) {
        return r_std_list_push_back(target, staged_value);
    }
    r_runtime_allocator_set_failure(target->allocator, UINT64_C(1));
    result = r_std_list_push_back(target, staged_value);
    *failed = (result.status == R_STD_LIST_CALL_ERROR) &&
              (result.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY) && (target->length == 0U);
    r_runtime_allocator_set_failure(target->allocator, UINT64_C(0));
    return result;
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    return (status == 0) && borrow_failed && value_failed ? 0 : 1;
}
