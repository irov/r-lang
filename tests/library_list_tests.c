#include "r_std_list.h"

#include <stdint.h>
#include <stdio.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define R_TEST_LIST_TOKEN_COUNT 16U
#define R_TEST_LIST_DROP_ORDER_COUNT 32U

typedef struct RTestListLedger {
    size_t move_count;
    size_t drop_count[R_TEST_LIST_TOKEN_COUNT];
    size_t drop_order[R_TEST_LIST_DROP_ORDER_COUNT];
    size_t drop_order_length;
    size_t invalid_drop_count;
} RTestListLedger;

typedef struct RTestListValue {
    size_t token;
    int32_t payload;
    RTestListLedger *ledger;
    _Bool owns;
} RTestListValue;

static void r_test_list_value_move(void *destination, void *source) {
    RTestListValue *destination_value = destination;
    RTestListValue *source_value = source;

    *destination_value = *source_value;
    source_value->ledger->move_count += 1U;
    source_value->owns = 0;
}

static void r_test_list_value_drop(void *value) {
    RTestListValue *test_value = value;
    RTestListLedger *ledger = test_value->ledger;

    if (!test_value->owns || (test_value->token >= R_TEST_LIST_TOKEN_COUNT) ||
        (ledger->drop_order_length >= R_TEST_LIST_DROP_ORDER_COUNT)) {
        ledger->invalid_drop_count += 1U;
        return;
    }
    test_value->owns = 0;
    ledger->drop_count[test_value->token] += 1U;
    ledger->drop_order[ledger->drop_order_length] = test_value->token;
    ledger->drop_order_length += 1U;
}

static RRuntimeTypeInfo r_test_list_value_type(void) {
    RRuntimeTypeInfo type = {
        sizeof(RTestListValue),
        _Alignof(RTestListValue),
        r_test_list_value_move,
        r_test_list_value_drop,
    };

    return type;
}

static RTestListValue r_test_list_value(RTestListLedger *ledger, size_t token, int32_t payload) {
    RTestListValue value = {token, payload, ledger, 1};
    return value;
}

static int r_test_list_empty(void) {
    RRuntimeAllocator allocator;
    RTestListLedger ledger = {0};
    RStdListCreateResult created;
    RStdListConstPointerOption const_option;
    RStdListMutPointerOption mut_option;
    RStdListValueOptionResult value_option;
    RStdListIteratorResult iterator;
    RTestListValue untouched = {9U, 91, &ledger, 0};

    r_runtime_allocator_initialize(&allocator);
    created = r_std_list_create(&allocator, r_test_list_value_type());
    R_TEST_CHECK(created.status == R_STD_LIST_CALL_SUCCESS);
    R_TEST_CHECK(created.value.allocator == &allocator);
    R_TEST_CHECK(created.value.first == NULL && created.value.last == NULL);
    R_TEST_CHECK(created.value.length == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    const_option = r_std_list_front(&created.value);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value &&
                 const_option.value == NULL);
    const_option = r_std_list_back(&created.value);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value &&
                 const_option.value == NULL);
    const_option = r_std_list_get(&created.value, 0U);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value &&
                 const_option.value == NULL);
    mut_option = r_std_list_front_mut(&created.value);
    R_TEST_CHECK(mut_option.status == R_STD_LIST_CALL_SUCCESS && !mut_option.has_value &&
                 mut_option.value == NULL);
    mut_option = r_std_list_back_mut(&created.value);
    R_TEST_CHECK(mut_option.status == R_STD_LIST_CALL_SUCCESS && !mut_option.has_value &&
                 mut_option.value == NULL);
    mut_option = r_std_list_get_mut(&created.value, 0U);
    R_TEST_CHECK(mut_option.status == R_STD_LIST_CALL_SUCCESS && !mut_option.has_value &&
                 mut_option.value == NULL);

    value_option = r_std_list_pop_front(&created.value, &untouched);
    R_TEST_CHECK(value_option.status == R_STD_LIST_CALL_SUCCESS && !value_option.has_value);
    value_option = r_std_list_pop_back(&created.value, &untouched);
    R_TEST_CHECK(value_option.status == R_STD_LIST_CALL_SUCCESS && !value_option.has_value);
    R_TEST_CHECK(untouched.token == 9U && untouched.payload == 91 && !untouched.owns);

    iterator = r_std_list_iter(&created.value);
    R_TEST_CHECK(iterator.status == R_STD_LIST_CALL_SUCCESS);
    const_option = r_std_list_next(&iterator.value);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value);
    const_option = r_std_list_next(&iterator.value);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    r_std_list_clear(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 0U);
    r_runtime_list_destroy(&created.value);

    return 0;
}

static int r_test_list_insertions_stability_and_iteration(void) {
    RRuntimeAllocator allocator;
    RTestListLedger ledger = {0};
    RStdListCreateResult created;
    RStdListInsertResult inserted;
    RTestListValue staged;
    RTestListValue *pointers[5] = {0};
    const size_t expected_tokens[] = {1U, 2U, 3U, 4U, 5U};
    RStdListConstPointerOption const_option;
    RStdListMutPointerOption mut_option;
    RStdListIteratorResult iterator;
    size_t index;
    uint64_t attempts;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_list_create(&allocator, r_test_list_value_type());
    R_TEST_CHECK(created.status == R_STD_LIST_CALL_SUCCESS);

    staged = r_test_list_value(&ledger, 3U, 30);
    inserted = r_std_list_push_back(&created.value, &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
    pointers[2] = inserted.value;

    staged = r_test_list_value(&ledger, 1U, 10);
    inserted = r_std_list_push_front(&created.value, &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
    pointers[0] = inserted.value;

    staged = r_test_list_value(&ledger, 2U, 20);
    inserted = r_std_list_insert_after(&created.value, pointers[0], &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
    pointers[1] = inserted.value;

    staged = r_test_list_value(&ledger, 5U, 50);
    inserted = r_std_list_push_back(&created.value, &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
    pointers[4] = inserted.value;

    staged = r_test_list_value(&ledger, 4U, 40);
    inserted = r_std_list_insert_before(&created.value, pointers[4], &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
    pointers[3] = inserted.value;

    R_TEST_CHECK(created.value.length == 5U);
    R_TEST_CHECK(ledger.move_count == 5U && ledger.drop_order_length == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(5));
    for (index = 0U; index < 5U; ++index) {
        size_t other;

        R_TEST_CHECK(pointers[index] != NULL);
        R_TEST_CHECK(pointers[index]->token == expected_tokens[index]);
        for (other = index + 1U; other < 5U; ++other) {
            R_TEST_CHECK(pointers[index] != pointers[other]);
        }
        const_option = r_std_list_get(&created.value, index);
        R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && const_option.has_value);
        R_TEST_CHECK(const_option.value == pointers[index]);
    }

    const_option = r_std_list_get(&created.value, 5U);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value &&
                 const_option.value == NULL);
    const_option = r_std_list_front(&created.value);
    R_TEST_CHECK(const_option.has_value && const_option.value == pointers[0]);
    const_option = r_std_list_back(&created.value);
    R_TEST_CHECK(const_option.has_value && const_option.value == pointers[4]);
    mut_option = r_std_list_front_mut(&created.value);
    R_TEST_CHECK(mut_option.has_value && mut_option.value == pointers[0]);
    ((RTestListValue *)mut_option.value)->payload = 11;
    mut_option = r_std_list_back_mut(&created.value);
    R_TEST_CHECK(mut_option.has_value && mut_option.value == pointers[4]);
    ((RTestListValue *)mut_option.value)->payload = 51;
    mut_option = r_std_list_get_mut(&created.value, 2U);
    R_TEST_CHECK(mut_option.has_value && mut_option.value == pointers[2]);
    ((RTestListValue *)mut_option.value)->payload = 31;

    attempts = r_runtime_allocator_attempt_count(&allocator);
    iterator = r_std_list_iter(&created.value);
    R_TEST_CHECK(iterator.status == R_STD_LIST_CALL_SUCCESS);
    for (index = 0U; index < 5U; ++index) {
        const_option = r_std_list_next(&iterator.value);
        R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && const_option.has_value);
        R_TEST_CHECK(const_option.value == pointers[index]);
    }
    const_option = r_std_list_next(&iterator.value);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value &&
                 const_option.value == NULL);
    const_option = r_std_list_next(&iterator.value);
    R_TEST_CHECK(const_option.status == R_STD_LIST_CALL_SUCCESS && !const_option.has_value &&
                 const_option.value == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == attempts);
    R_TEST_CHECK(ledger.move_count == 5U && ledger.drop_order_length == 0U);

    r_runtime_list_destroy(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 5U);
    R_TEST_CHECK(ledger.drop_order[0] == 5U);
    R_TEST_CHECK(ledger.drop_order[1] == 4U);
    R_TEST_CHECK(ledger.drop_order[2] == 3U);
    R_TEST_CHECK(ledger.drop_order[3] == 2U);
    R_TEST_CHECK(ledger.drop_order[4] == 1U);
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);
    return 0;
}

static int r_test_list_transactional_failures(void) {
    RRuntimeAllocator allocator;
    RTestListLedger ledger = {0};
    RStdListCreateResult target;
    RStdListInsertResult inserted;
    RTestListValue first;
    RTestListValue staged;
    RTestListValue *first_pointer;
    RRuntimeTypeInfo unsupported_type = {
        sizeof(RTestListValue),
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        r_test_list_value_move,
        r_test_list_value_drop,
    };
    RRuntimeTypeInfo oversized_unsupported_type = {
        R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U,
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        r_test_list_value_move,
        r_test_list_value_drop,
    };
    RStdListCreateResult exceptional;

    r_runtime_allocator_initialize(&allocator);
    target = r_std_list_create(&allocator, r_test_list_value_type());
    R_TEST_CHECK(target.status == R_STD_LIST_CALL_SUCCESS);
    first = r_test_list_value(&ledger, 1U, 10);
    inserted = r_std_list_push_back(&target.value, &first);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !first.owns);
    first_pointer = inserted.value;

    staged = r_test_list_value(&ledger, 2U, 20);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    inserted = r_std_list_insert_after(&target.value, first_pointer, &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_ERROR);
    R_TEST_CHECK(inserted.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(staged.owns && staged.token == 2U && staged.payload == 20);
    R_TEST_CHECK(target.value.length == 1U);
    R_TEST_CHECK(r_runtime_list_front(&target.value) == first_pointer);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    r_test_list_value_drop(&staged);

    r_runtime_list_destroy(&target.value);
    R_TEST_CHECK(ledger.drop_count[1] == 1U);
    R_TEST_CHECK(ledger.drop_count[2] == 1U);
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);

    r_runtime_allocator_initialize(&allocator);
    exceptional = r_std_list_create(&allocator, unsupported_type);
    R_TEST_CHECK(exceptional.status == R_STD_LIST_CALL_SUCCESS);
    staged = r_test_list_value(&ledger, 3U, 30);
    inserted = r_std_list_push_front(&exceptional.value, &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_ERROR);
    R_TEST_CHECK(inserted.reason == R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT);
    R_TEST_CHECK(staged.owns && exceptional.value.length == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_test_list_value_drop(&staged);
    r_runtime_list_destroy(&exceptional.value);

    exceptional = r_std_list_create(&allocator, oversized_unsupported_type);
    R_TEST_CHECK(exceptional.status == R_STD_LIST_CALL_SUCCESS);
    staged = r_test_list_value(&ledger, 4U, 40);
    inserted = r_std_list_push_back(&exceptional.value, &staged);
    R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_ERROR);
    R_TEST_CHECK(inserted.reason == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(staged.owns && exceptional.value.length == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_test_list_value_drop(&staged);
    r_runtime_list_destroy(&exceptional.value);
    return 0;
}

static int r_test_list_remove_pop_and_reverse_clear(void) {
    RRuntimeAllocator allocator;
    RTestListLedger ledger = {0};
    RStdListCreateResult created;
    RStdListInsertResult inserted;
    RStdListValueResult removed_result;
    RStdListValueOptionResult pop_result;
    RTestListValue staged;
    RTestListValue removed = {0};
    RTestListValue front = {0};
    RTestListValue back = {0};
    RTestListValue *pointers[5] = {0};
    const size_t expected_remaining[] = {1U, 2U, 4U, 5U};
    const size_t expected_drop_order[] = {3U, 1U, 5U, 4U, 2U};
    size_t token;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_list_create(&allocator, r_test_list_value_type());
    R_TEST_CHECK(created.status == R_STD_LIST_CALL_SUCCESS);
    for (token = 1U; token <= 5U; ++token) {
        staged = r_test_list_value(&ledger, token, (int32_t)(token * 10U));
        inserted = r_std_list_push_back(&created.value, &staged);
        R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
        pointers[token - 1U] = inserted.value;
    }
    R_TEST_CHECK(ledger.move_count == 5U);

    removed_result = r_std_list_remove(&created.value, pointers[2], &removed);
    R_TEST_CHECK(removed_result.status == R_STD_LIST_CALL_SUCCESS);
    R_TEST_CHECK(removed.owns && removed.token == 3U && removed.payload == 30);
    R_TEST_CHECK(created.value.length == 4U);
    R_TEST_CHECK(ledger.move_count == 6U);
    for (index = 0U; index < 4U; ++index) {
        RStdListConstPointerOption item = r_std_list_get(&created.value, index);
        RTestListValue *expected_pointer = index < 2U ? pointers[index] : pointers[index + 1U];

        R_TEST_CHECK(item.status == R_STD_LIST_CALL_SUCCESS && item.has_value);
        R_TEST_CHECK(item.value == expected_pointer);
        R_TEST_CHECK(((const RTestListValue *)item.value)->token == expected_remaining[index]);
    }
    r_test_list_value_drop(&removed);

    pop_result = r_std_list_pop_front(&created.value, &front);
    R_TEST_CHECK(pop_result.status == R_STD_LIST_CALL_SUCCESS && pop_result.has_value);
    R_TEST_CHECK(front.owns && front.token == 1U);
    pop_result = r_std_list_pop_back(&created.value, &back);
    R_TEST_CHECK(pop_result.status == R_STD_LIST_CALL_SUCCESS && pop_result.has_value);
    R_TEST_CHECK(back.owns && back.token == 5U);
    R_TEST_CHECK(created.value.length == 2U && ledger.move_count == 8U);
    r_test_list_value_drop(&front);
    r_test_list_value_drop(&back);

    r_std_list_clear(&created.value);
    R_TEST_CHECK(created.value.first == NULL && created.value.last == NULL);
    R_TEST_CHECK(created.value.length == 0U);
    R_TEST_CHECK(ledger.drop_order_length == 5U);
    for (index = 0U; index < 5U; ++index) {
        R_TEST_CHECK(ledger.drop_order[index] == expected_drop_order[index]);
    }
    for (token = 1U; token <= 5U; ++token) {
        R_TEST_CHECK(ledger.drop_count[token] == 1U);
    }
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);
    r_runtime_list_destroy(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 5U);
    return 0;
}

static int r_test_list_generated_drop_reverse_order(void) {
    RRuntimeAllocator allocator;
    RTestListLedger ledger = {0};
    RStdListCreateResult created;
    RStdListInsertResult inserted;
    RTestListValue staged;
    size_t token;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_list_create(&allocator, r_test_list_value_type());
    R_TEST_CHECK(created.status == R_STD_LIST_CALL_SUCCESS);
    for (token = 6U; token <= 8U; ++token) {
        staged = r_test_list_value(&ledger, token, (int32_t)token);
        inserted = r_std_list_push_back(&created.value, &staged);
        R_TEST_CHECK(inserted.status == R_STD_LIST_CALL_SUCCESS && !staged.owns);
    }
    r_runtime_list_destroy(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 3U);
    R_TEST_CHECK(ledger.drop_order[0] == 8U);
    R_TEST_CHECK(ledger.drop_order[1] == 7U);
    R_TEST_CHECK(ledger.drop_order[2] == 6U);
    R_TEST_CHECK(ledger.drop_count[6] == 1U);
    R_TEST_CHECK(ledger.drop_count[7] == 1U);
    R_TEST_CHECK(ledger.drop_count[8] == 1U);
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);
    return 0;
}

int main(void) {
    if (r_test_list_empty() != 0) {
        return 1;
    }
    if (r_test_list_insertions_stability_and_iteration() != 0) {
        return 1;
    }
    if (r_test_list_transactional_failures() != 0) {
        return 1;
    }
    if (r_test_list_remove_pop_and_reverse_clear() != 0) {
        return 1;
    }
    if (r_test_list_generated_drop_reverse_order() != 0) {
        return 1;
    }
    return 0;
}
