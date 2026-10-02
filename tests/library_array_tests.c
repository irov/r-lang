#include "r_std_array.h"

#include <stdint.h>
#include <stdio.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define R_TEST_TOKEN_COUNT 16U
#define R_TEST_DROP_ORDER_COUNT 32U

typedef struct RTestLedger {
    size_t move_count;
    size_t drop_count[R_TEST_TOKEN_COUNT];
    size_t drop_order[R_TEST_DROP_ORDER_COUNT];
    size_t drop_order_length;
    size_t invalid_drop_count;
} RTestLedger;

typedef struct RTestValue {
    size_t token;
    int32_t payload;
    RTestLedger *ledger;
    _Bool owns;
} RTestValue;

static void r_test_value_move(void *destination, void *source) {
    RTestValue *destination_value = destination;
    RTestValue *source_value = source;

    *destination_value = *source_value;
    source_value->ledger->move_count += 1U;
    source_value->owns = 0;
}

static void r_test_value_drop(void *value) {
    RTestValue *test_value = value;
    RTestLedger *ledger = test_value->ledger;

    if (!test_value->owns || (test_value->token >= R_TEST_TOKEN_COUNT) ||
        (ledger->drop_order_length >= R_TEST_DROP_ORDER_COUNT)) {
        ledger->invalid_drop_count += 1U;
        return;
    }
    test_value->owns = 0;
    ledger->drop_count[test_value->token] += 1U;
    ledger->drop_order[ledger->drop_order_length] = test_value->token;
    ledger->drop_order_length += 1U;
}

static RRuntimeTypeInfo r_test_value_type(void) {
    RRuntimeTypeInfo type = {
        sizeof(RTestValue),
        _Alignof(RTestValue),
        r_test_value_move,
        r_test_value_drop,
    };

    return type;
}

static RTestValue r_test_value(RTestLedger *ledger, size_t token, int32_t payload) {
    RTestValue value = {token, payload, ledger, 1};
    return value;
}

static int r_test_empty_and_zero_capacity(void) {
    RRuntimeAllocator allocator;
    RTestLedger ledger = {0};
    RStdArrayCreateResult created;
    RStdArrayAllocValueResult with_capacity;
    RStdArrayConstSliceResult const_slice;
    RStdArrayMutSliceResult mut_slice;
    RStdArrayConstPointerOption const_pointer;
    RStdArrayMutPointerOption mut_pointer;
    RStdArrayValueOptionResult option;
    RTestValue untouched = {9U, 91, &ledger, 0};

    r_runtime_allocator_initialize(&allocator);
    created = r_std_array_create(&allocator, r_test_value_type());
    R_TEST_CHECK(created.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(created.value.allocator == &allocator);
    R_TEST_CHECK(created.value.data == NULL);
    R_TEST_CHECK(created.value.length == 0U);
    R_TEST_CHECK(r_std_array_capacity(&created.value) == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    const_slice = r_std_array_as_slice(&created.value);
    R_TEST_CHECK(const_slice.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(const_slice.data == NULL && const_slice.length == 0U);
    mut_slice = r_std_array_as_slice_mut(&created.value);
    R_TEST_CHECK(mut_slice.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(mut_slice.data == NULL && mut_slice.length == 0U);
    const_pointer = r_std_array_get(&created.value, 0U);
    R_TEST_CHECK(const_pointer.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(!const_pointer.has_value && const_pointer.value == NULL);
    mut_pointer = r_std_array_get_mut(&created.value, 0U);
    R_TEST_CHECK(mut_pointer.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(!mut_pointer.has_value && mut_pointer.value == NULL);

    option = r_std_array_pop(&created.value, &untouched);
    R_TEST_CHECK(option.status == R_STD_ARRAY_CALL_SUCCESS && !option.has_value);
    R_TEST_CHECK(untouched.token == 9U && untouched.payload == 91 && !untouched.owns);
    option = r_std_array_remove(&created.value, 0U, &untouched);
    R_TEST_CHECK(option.status == R_STD_ARRAY_CALL_SUCCESS && !option.has_value);
    R_TEST_CHECK(untouched.token == 9U && untouched.payload == 91 && !untouched.owns);
    R_TEST_CHECK(created.value.length == 0U && created.value.capacity == 0U);
    r_std_array_clear(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 0U);
    r_runtime_array_destroy(&created.value);

    with_capacity = r_std_array_with_capacity(&allocator, r_test_value_type(), 0U);
    R_TEST_CHECK(with_capacity.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(with_capacity.value.data == NULL);
    R_TEST_CHECK(with_capacity.value.length == 0U && with_capacity.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_array_destroy(&with_capacity.value);

    return 0;
}

static int r_test_capacity_get_slices_and_clear(void) {
    RRuntimeAllocator allocator;
    RTestLedger ledger = {0};
    RStdArrayAllocValueResult created;
    RTestValue first;
    RTestValue second;
    RStdArrayPushResult pushed;
    RStdArrayConstPointerOption const_pointer;
    RStdArrayMutPointerOption mut_pointer;
    RStdArrayConstSliceResult const_slice;
    RStdArrayMutSliceResult mut_slice;
    RStdArrayValueOptionResult removed;
    RStdArrayAllocResult reserved;
    RTestValue untouched = {9U, 99, &ledger, 0};
    const RTestValue *const_values;
    RTestValue *mut_values;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_array_with_capacity(&allocator, r_test_value_type(), 2U);
    R_TEST_CHECK(created.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(created.value.length == 0U && created.value.capacity >= 2U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    allocation = created.value.data;
    capacity = created.value.capacity;

    first = r_test_value(&ledger, 1U, 10);
    pushed = r_std_array_push(&created.value, &first);
    R_TEST_CHECK(pushed.status == R_STD_ARRAY_CALL_SUCCESS && !first.owns);
    second = r_test_value(&ledger, 2U, 20);
    pushed = r_std_array_push(&created.value, &second);
    R_TEST_CHECK(pushed.status == R_STD_ARRAY_CALL_SUCCESS && !second.owns);
    R_TEST_CHECK(created.value.data == allocation);
    R_TEST_CHECK(created.value.capacity == capacity);
    R_TEST_CHECK(created.value.length == 2U);
    R_TEST_CHECK(ledger.move_count == 2U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    reserved = r_std_array_reserve(&created.value, 5U);
    R_TEST_CHECK(reserved.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(created.value.length == 2U);
    R_TEST_CHECK(r_std_array_capacity(&created.value) >= 7U);
    R_TEST_CHECK(ledger.move_count == 4U && ledger.drop_order_length == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(2));
    allocation = created.value.data;
    capacity = created.value.capacity;

    const_pointer = r_std_array_get(&created.value, 0U);
    R_TEST_CHECK(const_pointer.status == R_STD_ARRAY_CALL_SUCCESS && const_pointer.has_value);
    R_TEST_CHECK(((const RTestValue *)const_pointer.value)->token == 1U);
    const_pointer = r_std_array_get(&created.value, 2U);
    R_TEST_CHECK(const_pointer.status == R_STD_ARRAY_CALL_SUCCESS && !const_pointer.has_value &&
                 const_pointer.value == NULL);

    mut_pointer = r_std_array_get_mut(&created.value, 1U);
    R_TEST_CHECK(mut_pointer.status == R_STD_ARRAY_CALL_SUCCESS && mut_pointer.has_value);
    ((RTestValue *)mut_pointer.value)->payload = 21;
    mut_pointer = r_std_array_get_mut(&created.value, 2U);
    R_TEST_CHECK(mut_pointer.status == R_STD_ARRAY_CALL_SUCCESS && !mut_pointer.has_value &&
                 mut_pointer.value == NULL);

    const_slice = r_std_array_as_slice(&created.value);
    R_TEST_CHECK(const_slice.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(const_slice.data == allocation && const_slice.length == 2U);
    const_values = const_slice.data;
    R_TEST_CHECK(const_values[0].token == 1U && const_values[0].payload == 10);
    R_TEST_CHECK(const_values[1].token == 2U && const_values[1].payload == 21);
    mut_slice = r_std_array_as_slice_mut(&created.value);
    R_TEST_CHECK(mut_slice.status == R_STD_ARRAY_CALL_SUCCESS);
    R_TEST_CHECK(mut_slice.data == allocation && mut_slice.length == 2U);
    mut_values = mut_slice.data;
    mut_values[0].payload = 11;
    R_TEST_CHECK(((const RTestValue *)r_std_array_get(&created.value, 0U).value)->payload == 11);

    removed = r_std_array_remove(&created.value, 2U, &untouched);
    R_TEST_CHECK(removed.status == R_STD_ARRAY_CALL_SUCCESS && !removed.has_value);
    R_TEST_CHECK(created.value.data == allocation);
    R_TEST_CHECK(created.value.length == 2U && created.value.capacity == capacity);
    R_TEST_CHECK(untouched.token == 9U && untouched.payload == 99 && !untouched.owns);
    R_TEST_CHECK(ledger.move_count == 4U && ledger.drop_order_length == 0U);

    r_std_array_clear(&created.value);
    R_TEST_CHECK(created.value.data == allocation);
    R_TEST_CHECK(created.value.length == 0U && created.value.capacity == capacity);
    R_TEST_CHECK(ledger.drop_order_length == 2U);
    R_TEST_CHECK(ledger.drop_order[0] == 2U && ledger.drop_order[1] == 1U);
    R_TEST_CHECK(ledger.drop_count[1] == 1U && ledger.drop_count[2] == 1U);
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);
    r_runtime_array_destroy(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 2U);
    return 0;
}

static int r_test_transactional_allocation_errors(void) {
    RRuntimeAllocator allocator;
    RTestLedger ledger = {0};
    RStdArrayAllocValueResult created;
    RStdArrayAllocValueResult failed_create;
    RStdArrayPushResult pushed;
    RStdArrayAllocResult reserved;
    RTestValue first;
    RTestValue second;
    RRuntimeTypeInfo unsupported_type = {
        sizeof(RTestValue),
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        r_test_value_move,
        r_test_value_drop,
    };
    RRuntimeTypeInfo oversized_unsupported_type = {
        R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U,
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        r_test_value_move,
        r_test_value_drop,
    };
    size_t too_many;
    void *allocation;
    size_t capacity;
    const RTestValue *stored;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_array_with_capacity(&allocator, r_test_value_type(), 1U);
    R_TEST_CHECK(created.status == R_STD_ARRAY_CALL_SUCCESS);
    first = r_test_value(&ledger, 1U, 10);
    pushed = r_std_array_push(&created.value, &first);
    R_TEST_CHECK(pushed.status == R_STD_ARRAY_CALL_SUCCESS && !first.owns);
    allocation = created.value.data;
    capacity = created.value.capacity;
    stored = r_runtime_array_get(&created.value, 0U);
    R_TEST_CHECK(stored != NULL && stored->token == 1U && stored->payload == 10);

    second = r_test_value(&ledger, 2U, 20);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    pushed = r_std_array_push(&created.value, &second);
    R_TEST_CHECK(pushed.status == R_STD_ARRAY_CALL_ERROR);
    R_TEST_CHECK(pushed.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(second.owns && second.token == 2U && second.payload == 20);
    R_TEST_CHECK(created.value.data == allocation);
    R_TEST_CHECK(created.value.length == 1U && created.value.capacity == capacity);
    stored = r_runtime_array_get(&created.value, 0U);
    R_TEST_CHECK(stored != NULL && stored->token == 1U && stored->payload == 10);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    reserved = r_std_array_reserve(&created.value, SIZE_MAX);
    R_TEST_CHECK(reserved.status == R_STD_ARRAY_CALL_ERROR);
    R_TEST_CHECK(reserved.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(created.value.data == allocation);
    R_TEST_CHECK(created.value.length == 1U && created.value.capacity == capacity);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_test_value_drop(&second);
    r_runtime_array_destroy(&created.value);
    R_TEST_CHECK(ledger.drop_count[1] == 1U && ledger.drop_count[2] == 1U);
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);

    r_runtime_allocator_initialize(&allocator);
    too_many = (R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE / sizeof(RTestValue)) + 1U;
    failed_create = r_std_array_with_capacity(&allocator, r_test_value_type(), too_many);
    R_TEST_CHECK(failed_create.status == R_STD_ARRAY_CALL_ERROR);
    R_TEST_CHECK(failed_create.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(failed_create.value.data == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_array_destroy(&failed_create.value);

    failed_create = r_std_array_with_capacity(&allocator, unsupported_type, 1U);
    R_TEST_CHECK(failed_create.status == R_STD_ARRAY_CALL_ERROR);
    R_TEST_CHECK(failed_create.error == R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT);
    R_TEST_CHECK(failed_create.value.data == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_array_destroy(&failed_create.value);

    failed_create = r_std_array_with_capacity(&allocator, oversized_unsupported_type, 1U);
    R_TEST_CHECK(failed_create.status == R_STD_ARRAY_CALL_ERROR);
    R_TEST_CHECK(failed_create.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(failed_create.value.data == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_array_destroy(&failed_create.value);
    return 0;
}

static int r_test_remove_pop_growth_and_drop_exactly_once(void) {
    RRuntimeAllocator allocator;
    RTestLedger ledger = {0};
    RStdArrayCreateResult created;
    RStdArrayPushResult pushed;
    RStdArrayValueOptionResult option;
    RTestValue staged;
    RTestValue removed = {0};
    RTestValue popped = {0};
    RTestValue untouched = {9U, 99, &ledger, 0};
    const size_t expected_tokens[] = {1U, 3U, 4U};
    const size_t expected_drop_order[] = {2U, 5U, 4U, 3U, 1U};
    size_t token;
    size_t index;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_array_create(&allocator, r_test_value_type());
    R_TEST_CHECK(created.status == R_STD_ARRAY_CALL_SUCCESS);
    for (token = 1U; token <= 5U; ++token) {
        staged = r_test_value(&ledger, token, (int32_t)(token * 10U));
        pushed = r_std_array_push(&created.value, &staged);
        R_TEST_CHECK(pushed.status == R_STD_ARRAY_CALL_SUCCESS);
        R_TEST_CHECK(!staged.owns);
    }
    R_TEST_CHECK(created.value.length == 5U);
    R_TEST_CHECK(created.value.capacity >= 5U);
    R_TEST_CHECK(ledger.move_count == 9U);

    option = r_std_array_remove(&created.value, 5U, &untouched);
    R_TEST_CHECK(option.status == R_STD_ARRAY_CALL_SUCCESS && !option.has_value);
    R_TEST_CHECK(untouched.token == 9U && untouched.payload == 99 && !untouched.owns);
    R_TEST_CHECK(created.value.length == 5U && ledger.move_count == 9U);

    option = r_std_array_remove(&created.value, 1U, &removed);
    R_TEST_CHECK(option.status == R_STD_ARRAY_CALL_SUCCESS && option.has_value);
    R_TEST_CHECK(removed.owns && removed.token == 2U && removed.payload == 20);
    R_TEST_CHECK(created.value.length == 4U);
    R_TEST_CHECK(ledger.move_count == 13U);
    r_test_value_drop(&removed);

    option = r_std_array_pop(&created.value, &popped);
    R_TEST_CHECK(option.status == R_STD_ARRAY_CALL_SUCCESS && option.has_value);
    R_TEST_CHECK(popped.owns && popped.token == 5U && popped.payload == 50);
    R_TEST_CHECK(created.value.length == 3U);
    R_TEST_CHECK(ledger.move_count == 14U);
    r_test_value_drop(&popped);

    for (index = 0U; index < 3U; ++index) {
        RStdArrayConstPointerOption item = r_std_array_get(&created.value, index);

        R_TEST_CHECK(item.status == R_STD_ARRAY_CALL_SUCCESS && item.has_value);
        R_TEST_CHECK(((const RTestValue *)item.value)->token == expected_tokens[index]);
    }
    allocation = created.value.data;
    capacity = created.value.capacity;
    r_std_array_clear(&created.value);
    R_TEST_CHECK(created.value.data == allocation);
    R_TEST_CHECK(created.value.capacity == capacity && created.value.length == 0U);
    R_TEST_CHECK(ledger.drop_order_length == 5U);
    for (index = 0U; index < 5U; ++index) {
        R_TEST_CHECK(ledger.drop_order[index] == expected_drop_order[index]);
    }
    for (token = 1U; token <= 5U; ++token) {
        R_TEST_CHECK(ledger.drop_count[token] == 1U);
    }
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);

    option = r_std_array_pop(&created.value, &untouched);
    R_TEST_CHECK(option.status == R_STD_ARRAY_CALL_SUCCESS && !option.has_value);
    R_TEST_CHECK(untouched.token == 9U && untouched.payload == 99 && !untouched.owns);
    r_runtime_array_destroy(&created.value);
    R_TEST_CHECK(ledger.drop_order_length == 5U);
    return 0;
}

static int r_test_generated_drop_glue_reverse_order(void) {
    RRuntimeAllocator allocator;
    RTestLedger ledger = {0};
    RStdArrayCreateResult created;
    RStdArrayPushResult pushed;
    RTestValue staged;
    size_t token;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_array_create(&allocator, r_test_value_type());
    R_TEST_CHECK(created.status == R_STD_ARRAY_CALL_SUCCESS);
    for (token = 6U; token <= 8U; ++token) {
        staged = r_test_value(&ledger, token, (int32_t)token);
        pushed = r_std_array_push(&created.value, &staged);
        R_TEST_CHECK(pushed.status == R_STD_ARRAY_CALL_SUCCESS && !staged.owns);
    }
    r_runtime_array_destroy(&created.value);
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
    if (r_test_empty_and_zero_capacity() != 0) {
        return 1;
    }
    if (r_test_capacity_get_slices_and_clear() != 0) {
        return 1;
    }
    if (r_test_transactional_allocation_errors() != 0) {
        return 1;
    }
    if (r_test_remove_pop_growth_and_drop_exactly_once() != 0) {
        return 1;
    }
    if (r_test_generated_drop_glue_reverse_order() != 0) {
        return 1;
    }
    return 0;
}
