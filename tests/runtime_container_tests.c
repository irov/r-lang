#include "r_runtime_allocator.h"
#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_runtime_rc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct RRuntimeContainerTestValue {
    uint32_t value;
} RRuntimeContainerTestValue;

static uint32_t r_runtime_container_drop_log[8];
static size_t r_runtime_container_drop_count;
static size_t r_runtime_container_initialize_count;
static size_t r_runtime_container_initialize_drop_count;
static _Bool r_runtime_container_initialize_alignment_valid;
static uint16_t r_runtime_container_initialize_drop_checksum;

typedef struct RRuntimeContainerInitializeContext {
    size_t size;
    size_t alignment;
    uint8_t first;
    uint8_t last;
} RRuntimeContainerInitializeContext;

static void r_runtime_container_initialize(void *destination, const void *context_pointer) {
    const RRuntimeContainerInitializeContext *context = context_pointer;
    uint8_t *bytes = destination;

    r_runtime_container_initialize_count += 1U;
    if ((destination == NULL) || (context == NULL) || (context->size < 2U) ||
        (context->alignment == 0U) ||
        (((uintptr_t)destination & (uintptr_t)(context->alignment - 1U)) != (uintptr_t)0U)) {
        r_runtime_container_initialize_alignment_valid = 0;
        return;
    }
    (void)memset(bytes, 0, context->size);
    bytes[0] = context->first;
    bytes[context->size - 1U] = context->last;
}

static void r_runtime_container_initialize_drop(void *value) {
    const uint8_t *bytes = value;

    r_runtime_container_initialize_drop_count += 1U;
    r_runtime_container_initialize_drop_checksum =
        (uint16_t)((uint16_t)bytes[0] + (uint16_t)bytes[63]);
}

static void r_runtime_container_test_drop(void *value) {
    const RRuntimeContainerTestValue *test_value = value;
    if (r_runtime_container_drop_count < 8U) {
        r_runtime_container_drop_log[r_runtime_container_drop_count] = test_value->value;
    }
    r_runtime_container_drop_count += 1U;
}

static uint64_t r_runtime_container_test_hash(const void *value) {
    const RRuntimeContainerTestValue *test_value = value;
    return (uint64_t)test_value->value * UINT64_C(0x9e3779b1);
}

static _Bool r_runtime_container_test_equal(const void *left, const void *right) {
    const RRuntimeContainerTestValue *left_value = left;
    const RRuntimeContainerTestValue *right_value = right;
    return left_value->value == right_value->value;
}

static int r_runtime_container_fail(const char *message) {
    (void)fprintf(stderr, "runtime container test failure: %s\n", message);
    return 1;
}

static int r_runtime_allocator_test(void) {
    RRuntimeAllocator allocator;
    void *allocation = NULL;
    void *preserved;
    RRuntimeAllocationStatus status;

    r_runtime_allocator_initialize(&allocator);
    status = r_runtime_allocator_allocate(&allocator,
                                          R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U,
                                          R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
                                          &allocation);
    if ((status != R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) || (allocation != NULL) ||
        (r_runtime_allocator_attempt_count(&allocator) != 0U)) {
        return r_runtime_container_fail("allocation size precedence");
    }
    status = r_runtime_allocator_allocate(
        &allocator, 1U, R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U, &allocation);
    if ((status != R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) || (allocation != NULL) ||
        (r_runtime_allocator_attempt_count(&allocator) != 0U)) {
        return r_runtime_container_fail("unsupported alignment precedence");
    }
    status = r_runtime_allocator_allocate(&allocator, 128U, 64U, &allocation);
    if ((status != R_RUNTIME_ALLOCATION_OK) || (allocation == NULL) ||
        (((uintptr_t)allocation & (uintptr_t)63U) != (uintptr_t)0U)) {
        return r_runtime_container_fail("aligned allocation");
    }
    ((unsigned char *)allocation)[0] = UINT8_C(0x5a);
    preserved = allocation;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    status = r_runtime_allocator_reallocate(&allocator, allocation, 128U, 256U, 64U, &allocation);
    if ((status != R_RUNTIME_ALLOCATION_EXHAUSTED) || (allocation != preserved) ||
        (((unsigned char *)allocation)[0] != UINT8_C(0x5a))) {
        r_runtime_allocator_deallocate(preserved, 64U);
        return r_runtime_container_fail("failed reallocation did not preserve owner");
    }
    r_runtime_allocator_deallocate(allocation, 64U);
    return 0;
}

static int r_runtime_container_zero_sentinel_test(void) {
    RRuntimeArray array = {0};
    RRuntimeDict dict = {0};
    RRuntimeList list = {0};

    r_runtime_array_clear(&array);
    r_runtime_array_destroy(&array);
    r_runtime_dict_clear(&dict);
    r_runtime_dict_destroy(&dict);
    r_runtime_list_clear(&list);
    r_runtime_list_destroy(&list);
    return 0;
}

static int r_runtime_array_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeArray array;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        r_runtime_container_test_drop,
    };
    RRuntimeContainerTestValue first = {1U};
    RRuntimeContainerTestValue second = {2U};
    RRuntimeContainerTestValue third = {3U};
    RRuntimeContainerTestValue removed = {0U};
    RRuntimeContainerTestValue failed = {4U};
    size_t old_length;
    size_t old_capacity;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_array_initialize(&array, &allocator, type);
    if ((r_runtime_array_push(&array, &first) != R_RUNTIME_ARRAY_OK) ||
        (r_runtime_array_push(&array, &second) != R_RUNTIME_ARRAY_OK) ||
        (r_runtime_array_push(&array, &third) != R_RUNTIME_ARRAY_OK)) {
        r_runtime_array_destroy(&array);
        return r_runtime_container_fail("push");
    }
    if (!r_runtime_array_remove(&array, 1U, &removed) || (removed.value != 2U) ||
        (((const RRuntimeContainerTestValue *)r_runtime_array_get(&array, 1U))->value != 3U)) {
        r_runtime_array_destroy(&array);
        return r_runtime_container_fail("remove and shift");
    }
    old_length = array.length;
    old_capacity = array.capacity;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if (r_runtime_array_reserve(&array, old_capacity + 1U) != R_RUNTIME_ARRAY_ALLOCATION_FAILED ||
        (array.length != old_length) || (array.capacity != old_capacity) || (failed.value != 4U)) {
        r_runtime_array_destroy(&array);
        return r_runtime_container_fail("reserve transaction");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    r_runtime_container_drop_count = 0U;
    r_runtime_array_destroy(&array);
    if ((r_runtime_container_drop_count != 2U) || (r_runtime_container_drop_log[0] != 3U) ||
        (r_runtime_container_drop_log[1] != 1U)) {
        return r_runtime_container_fail("reverse destruction");
    }
    return 0;
}

static int r_runtime_list_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeList list;
    RRuntimeListIterator iterator;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        r_runtime_container_test_drop,
    };
    RRuntimeContainerTestValue first = {1U};
    RRuntimeContainerTestValue second = {2U};
    RRuntimeContainerTestValue middle = {3U};
    RRuntimeContainerTestValue failed = {4U};
    RRuntimeContainerTestValue removed = {0U};
    void *stored_first = NULL;
    void *stored_second = NULL;
    void *stored_middle = NULL;
    void *failed_storage = NULL;
    const RRuntimeContainerTestValue *next;

    r_runtime_allocator_initialize(&allocator);
    if (r_runtime_list_initialize(&list, &allocator, type) != R_RUNTIME_LIST_OK) {
        return r_runtime_container_fail("list initialize");
    }
    if ((r_runtime_list_push_back(&list, &first, &stored_first) != R_RUNTIME_LIST_OK) ||
        (r_runtime_list_push_back(&list, &second, &stored_second) != R_RUNTIME_LIST_OK) ||
        (r_runtime_list_insert_after(&list, stored_first, &middle, &stored_middle) !=
         R_RUNTIME_LIST_OK)) {
        r_runtime_list_destroy(&list);
        return r_runtime_container_fail("list insert");
    }
    if ((r_runtime_list_get(&list, 0U) != stored_first) ||
        (r_runtime_list_get(&list, 1U) != stored_middle) ||
        (r_runtime_list_get(&list, 2U) != stored_second) ||
        (((const RRuntimeContainerTestValue *)stored_first)->value != 1U) ||
        (((const RRuntimeContainerTestValue *)stored_second)->value != 2U)) {
        r_runtime_list_destroy(&list);
        return r_runtime_container_fail("list stable order");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_list_push_front(&list, &failed, &failed_storage) !=
         R_RUNTIME_LIST_ALLOCATION_FAILED) ||
        (list.length != 3U) || (failed_storage != NULL) || (failed.value != 4U)) {
        r_runtime_list_destroy(&list);
        return r_runtime_container_fail("list failed insertion transaction");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    iterator = r_runtime_list_iter(&list);
    next = r_runtime_list_next(&iterator);
    if ((next == NULL) || (next->value != 1U)) {
        r_runtime_list_destroy(&list);
        return r_runtime_container_fail("list iterator first");
    }
    next = r_runtime_list_next(&iterator);
    if ((next == NULL) || (next->value != 3U)) {
        r_runtime_list_destroy(&list);
        return r_runtime_container_fail("list iterator second");
    }
    if (!r_runtime_list_remove(&list, stored_middle, &removed) || (removed.value != 3U) ||
        (r_runtime_list_get(&list, 0U) != stored_first) ||
        (r_runtime_list_get(&list, 1U) != stored_second)) {
        r_runtime_list_destroy(&list);
        return r_runtime_container_fail("list remove");
    }
    r_runtime_container_drop_count = 0U;
    r_runtime_list_destroy(&list);
    if ((r_runtime_container_drop_count != 2U) || (r_runtime_container_drop_log[0] != 2U) ||
        (r_runtime_container_drop_log[1] != 1U)) {
        return r_runtime_container_fail("list destruction");
    }
    return 0;
}

static int r_runtime_dict_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeDict dict;
    RRuntimeDictIterator iterator;
    RRuntimeDictEntryRef entry;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        NULL,
    };
    RRuntimeDictKeyInfo key_type = {
        {
            sizeof(RRuntimeContainerTestValue),
            _Alignof(RRuntimeContainerTestValue),
            NULL,
            NULL,
        },
        r_runtime_container_test_hash,
        r_runtime_container_test_equal,
    };
    RRuntimeContainerTestValue first_key = {1U};
    RRuntimeContainerTestValue first_value = {10U};
    RRuntimeContainerTestValue second_key = {2U};
    RRuntimeContainerTestValue second_value = {20U};
    RRuntimeContainerTestValue duplicate_key = {1U};
    RRuntimeContainerTestValue replacement = {11U};
    RRuntimeContainerTestValue replaced = {0U};
    RRuntimeContainerTestValue removed = {0U};
    RRuntimeContainerTestValue failed_key = {3U};
    RRuntimeContainerTestValue failed_value = {30U};
    _Bool did_replace = 0;
    uint32_t number;
    size_t iteration_count;

    r_runtime_allocator_initialize(&allocator);
    if (r_runtime_dict_initialize(&dict, &allocator, key_type, type, UINT64_C(0x12345678)) !=
        R_RUNTIME_DICT_OK) {
        return r_runtime_container_fail("dict initialize");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_dict_insert(&dict, &failed_key, &failed_value, NULL, &did_replace) !=
         R_RUNTIME_DICT_ALLOCATION_FAILED) ||
        (dict.length != 0U) || (failed_key.value != 3U) || (failed_value.value != 30U)) {
        r_runtime_dict_destroy(&dict);
        return r_runtime_container_fail("dict failed insertion transaction");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    if ((r_runtime_dict_insert(&dict, &first_key, &first_value, NULL, &did_replace) !=
         R_RUNTIME_DICT_OK) ||
        did_replace ||
        (r_runtime_dict_insert(&dict, &second_key, &second_value, NULL, &did_replace) !=
         R_RUNTIME_DICT_OK) ||
        did_replace) {
        r_runtime_dict_destroy(&dict);
        return r_runtime_container_fail("dict insert");
    }
    if ((r_runtime_dict_insert(&dict, &duplicate_key, &replacement, &replaced, &did_replace) !=
         R_RUNTIME_DICT_OK) ||
        !did_replace || (replaced.value != 10U) ||
        (((const RRuntimeContainerTestValue *)r_runtime_dict_get(&dict, &first_key))->value !=
         11U) ||
        !r_runtime_dict_contains(&dict, &second_key)) {
        r_runtime_dict_destroy(&dict);
        return r_runtime_container_fail("dict replace and lookup");
    }
    iterator = r_runtime_dict_iter(&dict);
    if (!r_runtime_dict_next(&iterator, &entry) ||
        (((const RRuntimeContainerTestValue *)entry.key)->value != 1U) ||
        (((const RRuntimeContainerTestValue *)entry.value)->value != 11U) ||
        !r_runtime_dict_next(&iterator, &entry) ||
        (((const RRuntimeContainerTestValue *)entry.key)->value != 2U) ||
        (((const RRuntimeContainerTestValue *)entry.value)->value != 20U) ||
        r_runtime_dict_next(&iterator, &entry)) {
        r_runtime_dict_destroy(&dict);
        return r_runtime_container_fail("dict insertion order");
    }
    if (!r_runtime_dict_remove(&dict, &first_key, &removed) || (removed.value != 11U) ||
        r_runtime_dict_contains(&dict, &first_key) || (dict.length != 1U)) {
        r_runtime_dict_destroy(&dict);
        return r_runtime_container_fail("dict remove");
    }
    r_runtime_dict_destroy(&dict);

    if (r_runtime_dict_initialize(&dict, &allocator, key_type, type, UINT64_C(0xabcdef)) !=
        R_RUNTIME_DICT_OK) {
        return r_runtime_container_fail("dict growth initialize");
    }
    for (number = 0U; number < 200U; ++number) {
        RRuntimeContainerTestValue key = {number};
        RRuntimeContainerTestValue value = {number + 1000U};
        if ((r_runtime_dict_insert(&dict, &key, &value, NULL, &did_replace) != R_RUNTIME_DICT_OK) ||
            did_replace) {
            r_runtime_dict_destroy(&dict);
            return r_runtime_container_fail("dict growth insertion");
        }
    }
    for (number = 0U; number < 200U; ++number) {
        RRuntimeContainerTestValue key = {number};
        const RRuntimeContainerTestValue *value = r_runtime_dict_get(&dict, &key);
        if ((value == NULL) || (value->value != (number + 1000U))) {
            r_runtime_dict_destroy(&dict);
            return r_runtime_container_fail("dict growth lookup");
        }
    }
    for (number = 0U; number < 200U; number += 2U) {
        RRuntimeContainerTestValue key = {number};
        if (!r_runtime_dict_remove(&dict, &key, &removed) || (removed.value != (number + 1000U))) {
            r_runtime_dict_destroy(&dict);
            return r_runtime_container_fail("dict tombstone removal");
        }
    }
    for (number = 200U; number < 300U; ++number) {
        RRuntimeContainerTestValue key = {number};
        RRuntimeContainerTestValue value = {number + 1000U};
        if (r_runtime_dict_insert(&dict, &key, &value, NULL, &did_replace) != R_RUNTIME_DICT_OK) {
            r_runtime_dict_destroy(&dict);
            return r_runtime_container_fail("dict tombstone reuse");
        }
    }
    iterator = r_runtime_dict_iter(&dict);
    iteration_count = 0U;
    while (r_runtime_dict_next(&iterator, &entry)) {
        const uint32_t expected = iteration_count < 100U ? (uint32_t)(iteration_count * 2U) + 1U
                                                         : (uint32_t)iteration_count + 100U;
        if (((const RRuntimeContainerTestValue *)entry.key)->value != expected) {
            r_runtime_dict_destroy(&dict);
            return r_runtime_container_fail("dict growth insertion order");
        }
        iteration_count += 1U;
    }
    if (iteration_count != 200U) {
        r_runtime_dict_destroy(&dict);
        return r_runtime_container_fail("dict growth iteration count");
    }
    r_runtime_dict_destroy(&dict);
    return 0;
}

static int r_runtime_arc_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        r_runtime_container_test_drop,
    };
    RRuntimeContainerTestValue value = {41U};
    RRuntimeContainerTestValue failed = {42U};
    RRuntimeContainerTestValue unwrapped = {0U};
    RRuntimeArc owner = {NULL};
    RRuntimeArc clone = {NULL};
    RRuntimeArc upgraded = {NULL};
    RRuntimeArc raw_owner = {NULL};
    RRuntimeWeakArc weak = {NULL};
    const void *raw;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_arc_create(&allocator, type, &failed, &owner) !=
         R_RUNTIME_ARC_ALLOCATION_FAILED) ||
        (owner.control != NULL) || (failed.value != 42U)) {
        return r_runtime_container_fail("arc failed create transaction");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    if ((r_runtime_arc_create(&allocator, type, &value, &owner) != R_RUNTIME_ARC_OK) ||
        (r_runtime_arc_clone(&owner, &clone) != R_RUNTIME_ARC_OK) ||
        (r_runtime_arc_downgrade(&owner, &weak) != R_RUNTIME_ARC_OK) ||
        (r_runtime_arc_strong_count(&owner) != 2U) || (r_runtime_arc_weak_count(&owner) != 1U) ||
        !r_runtime_arc_ptr_eq(&owner, &clone) || (r_runtime_arc_get_mut(&owner) != NULL)) {
        r_runtime_arc_release(&clone);
        r_runtime_arc_release(&owner);
        r_runtime_weak_arc_release(&weak);
        return r_runtime_container_fail("arc clone and weak counts");
    }
    r_runtime_arc_release(&clone);
    if ((r_runtime_weak_arc_upgrade(&weak, &upgraded) != R_RUNTIME_ARC_OK) ||
        (r_runtime_arc_strong_count(&owner) != 2U)) {
        r_runtime_arc_release(&upgraded);
        r_runtime_arc_release(&owner);
        r_runtime_weak_arc_release(&weak);
        return r_runtime_container_fail("arc weak upgrade");
    }
    r_runtime_arc_release(&upgraded);
    r_runtime_weak_arc_release(&weak);
    if ((r_runtime_arc_get_mut(&owner) == NULL) ||
        (((RRuntimeContainerTestValue *)r_runtime_arc_get_mut(&owner))->value != 41U)) {
        r_runtime_arc_release(&owner);
        return r_runtime_container_fail("arc unique mutable access");
    }
    raw = r_runtime_arc_into_raw(&owner);
    if ((raw == NULL) || (owner.control != NULL) ||
        (r_runtime_arc_from_raw(raw, &raw_owner) != R_RUNTIME_ARC_OK) ||
        (r_runtime_arc_try_unwrap(&raw_owner, &unwrapped) != R_RUNTIME_ARC_OK) ||
        (unwrapped.value != 41U) || (raw_owner.control != NULL)) {
        r_runtime_arc_release(&raw_owner);
        return r_runtime_container_fail("arc raw and unwrap");
    }
    value.value = 99U;
    r_runtime_container_drop_count = 0U;
    if (r_runtime_arc_create(&allocator, type, &value, &owner) != R_RUNTIME_ARC_OK) {
        return r_runtime_container_fail("arc drop setup");
    }
    r_runtime_arc_release(&owner);
    if ((r_runtime_container_drop_count != 1U) || (r_runtime_container_drop_log[0] != 99U)) {
        return r_runtime_container_fail("arc exactly once drop");
    }
    return 0;
}

static int r_runtime_rc_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        r_runtime_container_test_drop,
    };
    RRuntimeContainerTestValue value = {55U};
    RRuntimeContainerTestValue unwrapped = {0U};
    RRuntimeRc owner = {NULL};
    RRuntimeRc clone = {NULL};
    RRuntimeRc upgraded = {NULL};
    RRuntimeWeakRc weak = {NULL};

    r_runtime_allocator_initialize(&allocator);
    if ((r_runtime_rc_create(&allocator, type, &value, &owner) != R_RUNTIME_RC_OK) ||
        (r_runtime_rc_clone(&owner, &clone) != R_RUNTIME_RC_OK) ||
        (r_runtime_rc_downgrade(&owner, &weak) != R_RUNTIME_RC_OK) ||
        (r_runtime_rc_strong_count(&owner) != 2U) || (r_runtime_rc_weak_count(&owner) != 1U) ||
        !r_runtime_rc_ptr_eq(&owner, &clone) || (r_runtime_rc_get_mut(&owner) != NULL)) {
        r_runtime_rc_release(&clone);
        r_runtime_rc_release(&owner);
        r_runtime_weak_rc_release(&weak);
        return r_runtime_container_fail("rc clone and weak counts");
    }
    r_runtime_rc_release(&clone);
    if ((r_runtime_weak_rc_upgrade(&weak, &upgraded) != R_RUNTIME_RC_OK) ||
        (r_runtime_rc_strong_count(&owner) != 2U)) {
        r_runtime_rc_release(&upgraded);
        r_runtime_rc_release(&owner);
        r_runtime_weak_rc_release(&weak);
        return r_runtime_container_fail("rc weak upgrade");
    }
    r_runtime_rc_release(&upgraded);
    r_runtime_weak_rc_release(&weak);
    if ((r_runtime_rc_get_mut(&owner) == NULL) ||
        (r_runtime_rc_try_unwrap(&owner, &unwrapped) != R_RUNTIME_RC_OK) ||
        (unwrapped.value != 55U) || (owner.control != NULL)) {
        r_runtime_rc_release(&owner);
        return r_runtime_container_fail("rc unique unwrap");
    }
    value.value = 77U;
    r_runtime_container_drop_count = 0U;
    if (r_runtime_rc_create(&allocator, type, &value, &owner) != R_RUNTIME_RC_OK) {
        return r_runtime_container_fail("rc drop setup");
    }
    r_runtime_rc_release(&owner);
    if ((r_runtime_container_drop_count != 1U) || (r_runtime_container_drop_log[0] != 77U)) {
        return r_runtime_container_fail("rc exactly once drop");
    }
    return 0;
}

static int r_runtime_own_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        r_runtime_container_test_drop,
    };
    RRuntimeContainerTestValue value = {88U};
    RRuntimeContainerTestValue moved = {0U};
    RRuntimeOwn owner = {0};

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_own_create(&allocator, type, &value, &owner) !=
         R_RUNTIME_OWN_ALLOCATION_FAILED) ||
        (owner.allocation != NULL) || (value.value != 88U)) {
        return r_runtime_container_fail("own failed create transaction");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    if ((r_runtime_own_create(&allocator, type, &value, &owner) != R_RUNTIME_OWN_OK) ||
        (r_runtime_own_get_mut(&owner) == NULL) ||
        (((const RRuntimeContainerTestValue *)r_runtime_own_get(&owner))->value != 88U) ||
        (r_runtime_own_into_value(&owner, &moved) != R_RUNTIME_OWN_OK) || (moved.value != 88U) ||
        (owner.allocation != NULL)) {
        r_runtime_own_release(&owner);
        return r_runtime_container_fail("own into value");
    }
    value.value = 89U;
    r_runtime_container_drop_count = 0U;
    if (r_runtime_own_create(&allocator, type, &value, &owner) != R_RUNTIME_OWN_OK) {
        return r_runtime_container_fail("own drop setup");
    }
    r_runtime_own_release(&owner);
    if ((r_runtime_container_drop_count != 1U) || (r_runtime_container_drop_log[0] != 89U)) {
        return r_runtime_container_fail("own exactly once drop");
    }
    return 0;
}

static int r_runtime_own_initialize_test(void) {
    const RRuntimeContainerInitializeContext context = {
        64U,
        64U,
        UINT8_C(0x2a),
        UINT8_C(0x7c),
    };
    const RRuntimeTypeInfo type = {
        64U,
        64U,
        NULL,
        r_runtime_container_initialize_drop,
    };
    RRuntimeAllocator allocator;
    RRuntimeOwn owner = {0};
    const uint8_t *bytes;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_container_initialize_count = 0U;
    r_runtime_container_initialize_alignment_valid = 1;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_own_create_initialize(
             &allocator, type, r_runtime_container_initialize, &context, &owner) !=
         R_RUNTIME_OWN_ALLOCATION_FAILED) ||
        (owner.allocation != NULL) || (r_runtime_container_initialize_count != 0U) ||
        (r_runtime_allocator_attempt_count(&allocator) != 1U)) {
        return r_runtime_container_fail("own initializer failed allocation transaction");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    if ((r_runtime_own_create_initialize(
             &allocator, type, r_runtime_container_initialize, &context, &owner) !=
         R_RUNTIME_OWN_OK) ||
        (owner.allocation == NULL) || (r_runtime_container_initialize_count != 1U) ||
        !r_runtime_container_initialize_alignment_valid) {
        r_runtime_own_release(&owner);
        return r_runtime_container_fail("own initializer successful transaction");
    }
    bytes = r_runtime_own_get(&owner);
    if ((bytes == NULL) || (bytes[0] != context.first) || (bytes[1] != UINT8_C(0)) ||
        (bytes[context.size - 2U] != UINT8_C(0)) || (bytes[context.size - 1U] != context.last)) {
        r_runtime_own_release(&owner);
        return r_runtime_container_fail("own initializer payload");
    }
    r_runtime_container_initialize_drop_count = 0U;
    r_runtime_container_initialize_drop_checksum = UINT16_C(0);
    r_runtime_own_release(&owner);
    r_runtime_own_release(&owner);
    if ((r_runtime_container_initialize_drop_count != 1U) ||
        (r_runtime_container_initialize_drop_checksum !=
         (uint16_t)((uint16_t)context.first + (uint16_t)context.last))) {
        return r_runtime_container_fail("own initializer exactly once drop");
    }
    return 0;
}

static int r_runtime_own_adopt_test(void) {
    RRuntimeAllocator allocator;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeContainerTestValue),
        _Alignof(RRuntimeContainerTestValue),
        NULL,
        r_runtime_container_test_drop,
    };
    RRuntimeOwn owner = {0};
    RRuntimeContainerTestValue *allocation = NULL;
    void *released = NULL;
    uint64_t allocation_attempts;

    r_runtime_allocator_initialize(&allocator);
    if (r_runtime_allocator_allocate(&allocator, type.size, type.alignment, (void **)&allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return r_runtime_container_fail("own adopt allocation setup");
    }
    allocation->value = 90U;
    allocation_attempts = r_runtime_allocator_attempt_count(&allocator);
    r_runtime_container_drop_count = 0U;
    if ((r_runtime_own_adopt(type, allocation, &owner) != R_RUNTIME_OWN_OK) ||
        (r_runtime_own_get(&owner) != allocation) ||
        (r_runtime_allocator_attempt_count(&allocator) != allocation_attempts)) {
        if (owner.allocation != NULL) {
            r_runtime_own_release(&owner);
        } else {
            r_runtime_allocator_deallocate(allocation, type.alignment);
        }
        return r_runtime_container_fail("own adopt exact base without allocation");
    }
    if ((r_runtime_own_into_raw(&owner, &released) != R_RUNTIME_OWN_OK) ||
        (released != allocation) || (owner.allocation != NULL) ||
        (r_runtime_container_drop_count != 0U) ||
        (((RRuntimeContainerTestValue *)released)->value != 90U)) {
        if (owner.allocation != NULL) {
            r_runtime_own_release(&owner);
        } else if (released != NULL) {
            r_runtime_allocator_deallocate(released, type.alignment);
        }
        return r_runtime_container_fail("own release exact base without drop");
    }
    r_runtime_container_test_drop(released);
    r_runtime_allocator_deallocate(released, type.alignment);
    if ((r_runtime_container_drop_count != 1U) || (r_runtime_container_drop_log[0] != 90U)) {
        return r_runtime_container_fail("own released duty remains with caller");
    }
    return 0;
}

int main(void) {
    if ((r_runtime_allocator_test() != 0) || (r_runtime_container_zero_sentinel_test() != 0) ||
        (r_runtime_array_test() != 0) || (r_runtime_list_test() != 0) ||
        (r_runtime_dict_test() != 0) || (r_runtime_arc_test() != 0) || (r_runtime_rc_test() != 0) ||
        (r_runtime_own_test() != 0) || (r_runtime_own_initialize_test() != 0) ||
        (r_runtime_own_adopt_test() != 0)) {
        return 1;
    }
    (void)puts("runtime_container_ok");
    return 0;
}
