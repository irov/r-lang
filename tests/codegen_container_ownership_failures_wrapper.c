#include "r_runtime_allocator.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_std_alloc.h"
#include "r_std_dict.h"
#include "r_std_list.h"

#include <stdbool.h>
#include <stdint.h>

void r_test_own_release(RRuntimeOwn *owner);
RStdAllocTryNewResult
r_test_alloc_try_new(RRuntimeAllocator *allocator, RRuntimeTypeInfo type, void *staged_value);
RStdListInsertResult r_test_list_push_back(RStdList *target, void *staged_value);
RStdDictInsertResult
r_test_dict_insert(RStdDict *target, void *staged_key, void *staged_value, void *replaced_storage);

#define r_runtime_own_release r_test_own_release
#define r_std_alloc_try_new r_test_alloc_try_new
#define r_std_dict_insert r_test_dict_insert
#define r_std_list_push_back r_test_list_push_back
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_list_push_back
#undef r_std_dict_insert
#undef r_std_alloc_try_new
#undef r_runtime_own_release

static size_t releases;
static bool valid = true;
static bool alloc_failed;
static bool list_failed;
static bool dict_failed;

static bool r_test_same_owner(const RRuntimeOwn *left, const RRuntimeOwn *right) {
    return (left->allocation == right->allocation) && (left->type.size == right->type.size) &&
           (left->type.alignment == right->type.alignment) &&
           (left->type.move_initialize == right->type.move_initialize) &&
           (left->type.drop == right->type.drop) &&
           (left->allocation_alignment == right->allocation_alignment);
}

void r_test_own_release(RRuntimeOwn *owner) {
    static const int32_t expected[] = {11, 22, 33};

    if ((owner->allocation == NULL) || (releases >= 3U) ||
        (*(const int32_t *)owner->allocation != expected[releases])) {
        valid = false;
    }
    ++releases;
    r_runtime_own_release(owner);
}

RStdAllocTryNewResult
r_test_alloc_try_new(RRuntimeAllocator *allocator, RRuntimeTypeInfo type, void *staged_value) {
    const RRuntimeOwn snapshot = *(const RRuntimeOwn *)staged_value;
    RStdAllocTryNewResult result;

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    result = r_std_alloc_try_new(allocator, type, staged_value);
    alloc_failed = (r_runtime_allocator_attempt_count(allocator) == UINT64_C(1)) &&
                   (result.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR) &&
                   (result.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY) &&
                   r_test_same_owner((const RRuntimeOwn *)staged_value, &snapshot);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return result;
}

/* The push allocates one node (for `own i32*`): it must report OUT_OF_MEMORY, leave the list
 * empty and hand the staged owner back to the R catch clause, which checks its value. */
RStdListInsertResult r_test_list_push_back(RStdList *target, void *staged_value) {
    const RRuntimeOwn snapshot = *(const RRuntimeOwn *)staged_value;
    RStdListInsertResult result;

    r_runtime_allocator_set_failure(target->allocator, UINT64_C(1));
    result = r_std_list_push_back(target, staged_value);
    list_failed = (r_runtime_allocator_attempt_count(target->allocator) == UINT64_C(1)) &&
                  (result.status == R_STD_LIST_CALL_ERROR) &&
                  (result.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY) && (target->length == 0U) &&
                  (target->first == NULL) &&
                  r_test_same_owner((const RRuntimeOwn *)staged_value, &snapshot);
    r_runtime_allocator_set_failure(target->allocator, UINT64_C(0));
    return result;
}

RStdDictInsertResult
r_test_dict_insert(RStdDict *target, void *staged_key, void *staged_value, void *replaced_storage) {
    const int32_t key = *(const int32_t *)staged_key;
    const RRuntimeOwn snapshot = *(const RRuntimeOwn *)staged_value;
    RStdDictInsertResult result;

    r_runtime_allocator_set_failure(target->allocator, UINT64_C(1));
    result = r_std_dict_insert(target, staged_key, staged_value, replaced_storage);
    dict_failed = (r_runtime_allocator_attempt_count(target->allocator) == UINT64_C(1)) &&
                  (result.status == R_STD_DICT_CALL_ERROR) &&
                  (result.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY) && (key == INT32_C(7)) &&
                  (*(const int32_t *)staged_key == INT32_C(7)) && (target->length == 0U) &&
                  (target->slots == NULL) && (target->entries == NULL) &&
                  r_test_same_owner((const RRuntimeOwn *)staged_value, &snapshot);
    r_runtime_allocator_set_failure(target->allocator, UINT64_C(0));
    return result;
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    return (status == 0) && valid && alloc_failed && list_failed && dict_failed && (releases == 3U)
               ? 0
               : 1;
}
