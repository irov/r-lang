#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_runtime_string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* R-OWN-0020 (L26): the clone glue calls these allocating runtime steps directly. The k-th
 * attempt of the program fails at its k-th step, so the attempts sweep every step of one clone;
 * the program reports how many attempts failed. */
RRuntimeArrayStatus r_test_array_with_capacity(RRuntimeArray *array,
                                               RRuntimeAllocator *allocator,
                                               RRuntimeTypeInfo element,
                                               size_t capacity);
RRuntimeStringStatus r_test_string_from_valid_utf8(RRuntimeString *string,
                                                   RRuntimeAllocator *allocator,
                                                   const uint8_t *bytes,
                                                   size_t length);
RRuntimeListStatus r_test_list_push_back(RRuntimeList *list, void *value, void **stored_value);
RRuntimeDictStatus r_test_dict_with_capacity(RRuntimeDict *dict,
                                             RRuntimeAllocator *allocator,
                                             RRuntimeDictKeyInfo key,
                                             RRuntimeTypeInfo value,
                                             uint64_t seed,
                                             size_t capacity);
RRuntimeOwnStatus r_test_own_create(RRuntimeAllocator *allocator,
                                    RRuntimeTypeInfo type,
                                    void *value,
                                    RRuntimeOwn *result);

#define r_runtime_array_with_capacity r_test_array_with_capacity
#define r_runtime_string_from_valid_utf8 r_test_string_from_valid_utf8
#define r_runtime_list_push_back r_test_list_push_back
#define r_runtime_dict_with_capacity r_test_dict_with_capacity
#define r_runtime_own_create r_test_own_create
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_create
#undef r_runtime_dict_with_capacity
#undef r_runtime_list_push_back
#undef r_runtime_string_from_valid_utf8
#undef r_runtime_array_with_capacity

static unsigned long calls;
static unsigned long fail_at = 1U;
static unsigned long injected;

static bool r_test_step(void) {
    calls += 1U;
    if (calls != fail_at) {
        return false;
    }
    fail_at += 1U;
    calls = 0U;
    injected += 1U;
    return true;
}

RRuntimeArrayStatus r_test_array_with_capacity(RRuntimeArray *array,
                                               RRuntimeAllocator *allocator,
                                               RRuntimeTypeInfo element,
                                               size_t capacity) {
    if (r_test_step()) {
        return R_RUNTIME_ARRAY_ALLOCATION_FAILED;
    }
    return r_runtime_array_with_capacity(array, allocator, element, capacity);
}

RRuntimeStringStatus r_test_string_from_valid_utf8(RRuntimeString *string,
                                                   RRuntimeAllocator *allocator,
                                                   const uint8_t *bytes,
                                                   size_t length) {
    if (r_test_step()) {
        return R_RUNTIME_STRING_ALLOCATION_FAILED;
    }
    return r_runtime_string_from_valid_utf8(string, allocator, bytes, length);
}

RRuntimeListStatus r_test_list_push_back(RRuntimeList *list, void *value, void **stored_value) {
    if (r_test_step()) {
        return R_RUNTIME_LIST_ALLOCATION_FAILED;
    }
    return r_runtime_list_push_back(list, value, stored_value);
}

RRuntimeDictStatus r_test_dict_with_capacity(RRuntimeDict *dict,
                                             RRuntimeAllocator *allocator,
                                             RRuntimeDictKeyInfo key,
                                             RRuntimeTypeInfo value,
                                             uint64_t seed,
                                             size_t capacity) {
    if (r_test_step()) {
        return R_RUNTIME_DICT_ALLOCATION_FAILED;
    }
    return r_runtime_dict_with_capacity(dict, allocator, key, value, seed, capacity);
}

RRuntimeOwnStatus r_test_own_create(RRuntimeAllocator *allocator,
                                    RRuntimeTypeInfo type,
                                    void *value,
                                    RRuntimeOwn *result) {
    if (r_test_step()) {
        return R_RUNTIME_OWN_ALLOCATION_FAILED;
    }
    return r_runtime_own_create(allocator, type, value, result);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    /* One clone of two nodes takes 19 allocating steps; each earlier attempt failed once. */
    return (status == (int)injected) && (injected + 1U == fail_at) && (injected == 19U) ? 0 : 1;
}
