#include "r_std_dict.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define R_TEST_DICT_MAX_INSTANCE 96U
#define R_TEST_DICT_MAX_EVENTS 192U

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(                                                                         \
                stderr, "dict test failure at %s:%d: %s\n", __FILE__, __LINE__, #expression);      \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef enum RTestDictEventKind {
    R_TEST_DICT_DROP_KEY = 1,
    R_TEST_DICT_DROP_VALUE = 2
} RTestDictEventKind;

typedef struct RTestDictEvent {
    RTestDictEventKind kind;
    uint32_t instance;
} RTestDictEvent;

typedef struct RTestDictLedger {
    size_t key_move_count;
    size_t value_move_count;
    size_t hash_count;
    size_t equal_count;
    size_t invalid_move_count;
    size_t invalid_drop_count;
    size_t event_count;
    size_t key_drop_count[R_TEST_DICT_MAX_INSTANCE];
    size_t value_drop_count[R_TEST_DICT_MAX_INSTANCE];
    RTestDictEvent events[R_TEST_DICT_MAX_EVENTS];
} RTestDictLedger;

typedef struct RTestDictKey {
    uint32_t logical;
    uint32_t instance;
    uint64_t forced_hash;
    RTestDictLedger *ledger;
    _Bool owns;
} RTestDictKey;

typedef struct RTestDictValue {
    uint32_t instance;
    int32_t payload;
    RTestDictLedger *ledger;
    _Bool owns;
} RTestDictValue;

static void
r_test_dict_record_event(RTestDictLedger *ledger, RTestDictEventKind kind, uint32_t instance) {
    if ((ledger == NULL) || (instance >= R_TEST_DICT_MAX_INSTANCE) ||
        (ledger->event_count >= R_TEST_DICT_MAX_EVENTS)) {
        if (ledger != NULL) {
            ledger->invalid_drop_count += 1U;
        }
        return;
    }
    ledger->events[ledger->event_count].kind = kind;
    ledger->events[ledger->event_count].instance = instance;
    ledger->event_count += 1U;
}

static void r_test_dict_key_move(void *destination, void *source) {
    RTestDictKey *target = destination;
    RTestDictKey *staged = source;

    *target = *staged;
    if (!staged->owns || (staged->ledger == NULL)) {
        if (staged->ledger != NULL) {
            staged->ledger->invalid_move_count += 1U;
        }
        return;
    }
    staged->ledger->key_move_count += 1U;
    staged->owns = 0;
}

static void r_test_dict_value_move(void *destination, void *source) {
    RTestDictValue *target = destination;
    RTestDictValue *staged = source;

    *target = *staged;
    if (!staged->owns || (staged->ledger == NULL)) {
        if (staged->ledger != NULL) {
            staged->ledger->invalid_move_count += 1U;
        }
        return;
    }
    staged->ledger->value_move_count += 1U;
    staged->owns = 0;
}

static void r_test_dict_key_drop(void *value) {
    RTestDictKey *key = value;

    if ((key->ledger == NULL) || !key->owns || (key->instance >= R_TEST_DICT_MAX_INSTANCE)) {
        if (key->ledger != NULL) {
            key->ledger->invalid_drop_count += 1U;
        }
        return;
    }
    key->owns = 0;
    key->ledger->key_drop_count[key->instance] += 1U;
    r_test_dict_record_event(key->ledger, R_TEST_DICT_DROP_KEY, key->instance);
}

static void r_test_dict_value_drop(void *value) {
    RTestDictValue *stored = value;

    if ((stored->ledger == NULL) || !stored->owns ||
        (stored->instance >= R_TEST_DICT_MAX_INSTANCE)) {
        if (stored->ledger != NULL) {
            stored->ledger->invalid_drop_count += 1U;
        }
        return;
    }
    stored->owns = 0;
    stored->ledger->value_drop_count[stored->instance] += 1U;
    r_test_dict_record_event(stored->ledger, R_TEST_DICT_DROP_VALUE, stored->instance);
}

static uint64_t r_test_dict_hash(const void *value) {
    const RTestDictKey *key = value;

    if (key->ledger != NULL) {
        key->ledger->hash_count += 1U;
    }
    return key->forced_hash;
}

static _Bool r_test_dict_equal(const void *left, const void *right) {
    const RTestDictKey *stored = left;
    const RTestDictKey *probe = right;

    if (stored->ledger != NULL) {
        stored->ledger->equal_count += 1U;
    }
    return stored->logical == probe->logical;
}

static RRuntimeTypeInfo r_test_dict_value_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(RTestDictValue),
        _Alignof(RTestDictValue),
        r_test_dict_value_move,
        r_test_dict_value_drop,
    };
    return type;
}

static RStdDictKeyInfo r_test_dict_key_type(void) {
    const RStdDictKeyInfo type = {
        {
            sizeof(RTestDictKey),
            _Alignof(RTestDictKey),
            r_test_dict_key_move,
            r_test_dict_key_drop,
        },
        r_test_dict_hash,
        r_test_dict_equal,
    };
    return type;
}

static RTestDictKey
r_test_dict_owned_key(RTestDictLedger *ledger, uint32_t logical, uint32_t instance) {
    const RTestDictKey key = {
        logical,
        instance,
        UINT64_C(7),
        ledger,
        1,
    };
    return key;
}

static RTestDictKey r_test_dict_probe_key(RTestDictLedger *ledger, uint32_t logical) {
    const RTestDictKey key = {
        logical,
        0U,
        UINT64_C(7),
        ledger,
        0,
    };
    return key;
}

static RTestDictValue
r_test_dict_owned_value(RTestDictLedger *ledger, uint32_t instance, int32_t payload) {
    const RTestDictValue value = {
        instance,
        payload,
        ledger,
        1,
    };
    return value;
}

static int r_test_dict_expect_order(const RStdDict *dict,
                                    const uint32_t *logical,
                                    const uint32_t *key_instance,
                                    const int32_t *payload,
                                    size_t count) {
    RStdDictIteratorResult iterator = r_std_dict_iter(dict);
    size_t index;

    R_TEST_CHECK(iterator.status == R_STD_DICT_CALL_SUCCESS);
    for (index = 0U; index < count; ++index) {
        RStdDictEntryOption next = r_std_dict_next(&iterator.value);
        const RTestDictKey *key;
        const RTestDictValue *value;

        R_TEST_CHECK(next.status == R_STD_DICT_CALL_SUCCESS && next.has_value);
        R_TEST_CHECK(next.value.key != NULL && next.value.value != NULL);
        key = next.value.key;
        value = next.value.value;
        R_TEST_CHECK(key->logical == logical[index]);
        R_TEST_CHECK(key->instance == key_instance[index]);
        R_TEST_CHECK(value->payload == payload[index]);
    }
    {
        RStdDictEntryOption exhausted = r_std_dict_next(&iterator.value);
        R_TEST_CHECK(exhausted.status == R_STD_DICT_CALL_SUCCESS && !exhausted.has_value);
        R_TEST_CHECK(exhausted.value.key == NULL && exhausted.value.value == NULL);
    }
    {
        RStdDictEntryOption exhausted = r_std_dict_next(&iterator.value);
        R_TEST_CHECK(exhausted.status == R_STD_DICT_CALL_SUCCESS && !exhausted.has_value);
        R_TEST_CHECK(exhausted.value.key == NULL && exhausted.value.value == NULL);
    }
    return 0;
}

static int r_test_dict_empty(void) {
    RRuntimeAllocator allocator;
    RTestDictLedger ledger = {0};
    RStdDictCreateResult created;
    RStdDictAllocValueResult reserved;
    RStdDictAllocResult reserve_result;
    RStdDictBoolResult contains;
    RStdDictConstPointerOption const_option;
    RStdDictMutPointerOption mut_option;
    RStdDictValueOptionResult removed;
    RStdDictIteratorResult iterator;
    RStdDictEntryOption next;
    RTestDictKey probe = r_test_dict_probe_key(&ledger, 1U);
    RTestDictValue untouched = {91U, 919, &ledger, 0};

    r_runtime_allocator_initialize(&allocator);
    created = r_std_dict_create(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(0x1234));
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.slots == NULL && created.value.length == 0U);
    R_TEST_CHECK(created.value.tombstones == 0U && created.value.capacity == 0U);
    R_TEST_CHECK(created.value.first_order == SIZE_MAX && created.value.last_order == SIZE_MAX);
    R_TEST_CHECK(created.value.seed == UINT64_C(0x1234));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    reserve_result = r_std_dict_reserve(&created.value, 0U);
    R_TEST_CHECK(reserve_result.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.slots == NULL && created.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    contains = r_std_dict_contains(&created.value, &probe);
    R_TEST_CHECK(contains.status == R_STD_DICT_CALL_SUCCESS && !contains.value);
    const_option = r_std_dict_get(&created.value, &probe);
    R_TEST_CHECK(const_option.status == R_STD_DICT_CALL_SUCCESS && !const_option.has_value);
    R_TEST_CHECK(const_option.value == NULL);
    mut_option = r_std_dict_get_mut(&created.value, &probe);
    R_TEST_CHECK(mut_option.status == R_STD_DICT_CALL_SUCCESS && !mut_option.has_value);
    R_TEST_CHECK(mut_option.value == NULL);
    removed = r_std_dict_remove(&created.value, &probe, &untouched);
    R_TEST_CHECK(removed.status == R_STD_DICT_CALL_SUCCESS && !removed.has_value);
    R_TEST_CHECK(untouched.instance == 91U && untouched.payload == 919 && !untouched.owns);
    iterator = r_std_dict_iter(&created.value);
    R_TEST_CHECK(iterator.status == R_STD_DICT_CALL_SUCCESS);
    next = r_std_dict_next(&iterator.value);
    R_TEST_CHECK(next.status == R_STD_DICT_CALL_SUCCESS && !next.has_value);
    next = r_std_dict_next(&iterator.value);
    R_TEST_CHECK(next.status == R_STD_DICT_CALL_SUCCESS && !next.has_value);
    r_std_dict_clear(&created.value);
    r_runtime_dict_destroy(&created.value);
    R_TEST_CHECK(ledger.invalid_drop_count == 0U);

    reserved = r_std_dict_with_capacity(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(0x5678), 0U);
    R_TEST_CHECK(reserved.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(reserved.value.slots == NULL && reserved.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_dict_destroy(&reserved.value);

    return 0;
}

static int r_test_dict_collision_tombstone_replace_and_order(void) {
    RRuntimeAllocator allocator;
    RTestDictLedger ledger = {0};
    RStdDictAllocValueResult created;
    RStdDictInsertResult inserted;
    RStdDictConstPointerOption found;
    RStdDictMutPointerOption found_mut;
    RStdDictBoolResult contains;
    RStdDictValueOptionResult removed_result;
    RTestDictKey keys[3];
    RTestDictValue values[3];
    RTestDictValue untouched = {90U, 900, &ledger, 0};
    RTestDictValue removed = {0};
    RTestDictValue replaced = {0};
    RTestDictKey probe;
    const uint32_t initial_logical[] = {1U, 2U, 3U};
    const uint32_t initial_instances[] = {10U, 20U, 30U};
    const int32_t initial_payloads[] = {100, 200, 300};
    const uint32_t reordered_logical[] = {1U, 3U, 2U};
    const uint32_t reordered_instances[] = {10U, 30U, 21U};
    const int32_t reordered_payloads[] = {101, 310, 210};
    size_t index;
    void *slots;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_dict_with_capacity(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(0xc0111de), 4U);
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.capacity == 8U && created.value.slots != NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    for (index = 0U; index < 3U; ++index) {
        keys[index] =
            r_test_dict_owned_key(&ledger, (uint32_t)(index + 1U), (uint32_t)((index + 1U) * 10U));
        values[index] = r_test_dict_owned_value(
            &ledger, (uint32_t)((index + 1U) * 10U), (int32_t)((index + 1U) * 100U));
        inserted = r_std_dict_insert(&created.value, &keys[index], &values[index], &untouched);
        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && !inserted.did_replace);
        R_TEST_CHECK(!keys[index].owns && !values[index].owns);
        R_TEST_CHECK(untouched.instance == 90U && untouched.payload == 900 && !untouched.owns);
    }
    R_TEST_CHECK(created.value.length == 3U && created.value.tombstones == 0U);
    R_TEST_CHECK(created.value.entries != NULL && created.value.entries != created.value.slots);
    R_TEST_CHECK(created.value.entries ==
                 (unsigned char *)created.value.slots + created.value.entries_offset);
    R_TEST_CHECK(created.value.entry_capacity == 5U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    R_TEST_CHECK(ledger.equal_count > 0U);
    R_TEST_CHECK(r_test_dict_expect_order(
                     &created.value, initial_logical, initial_instances, initial_payloads, 3U) ==
                 0);

    probe = r_test_dict_probe_key(&ledger, 1U);
    contains = r_std_dict_contains(&created.value, &probe);
    R_TEST_CHECK(contains.status == R_STD_DICT_CALL_SUCCESS && contains.value);
    found_mut = r_std_dict_get_mut(&created.value, &probe);
    R_TEST_CHECK(found_mut.status == R_STD_DICT_CALL_SUCCESS && found_mut.has_value);
    ((RTestDictValue *)found_mut.value)->payload = 101;
    probe = r_test_dict_probe_key(&ledger, 3U);
    found = r_std_dict_get(&created.value, &probe);
    R_TEST_CHECK(found.status == R_STD_DICT_CALL_SUCCESS && found.has_value);
    R_TEST_CHECK(((const RTestDictValue *)found.value)->payload == 300);

    {
        RStdDictIteratorResult iterator = r_std_dict_iter(&created.value);
        RStdDictEntryOption next;

        R_TEST_CHECK(iterator.status == R_STD_DICT_CALL_SUCCESS);
        for (index = 0U; index < 3U; ++index) {
            next = r_std_dict_next(&iterator.value);
            R_TEST_CHECK(next.has_value);
            R_TEST_CHECK(next.value.key == (unsigned char *)created.value.entries +
                                               (index * created.value.entry_size) +
                                               created.value.key_offset);
            R_TEST_CHECK(next.value.value == (unsigned char *)created.value.entries +
                                                 (index * created.value.entry_size) +
                                                 created.value.value_offset);
        }
    }

    probe = r_test_dict_probe_key(&ledger, 2U);
    removed_result = r_std_dict_remove(&created.value, &probe, &removed);
    R_TEST_CHECK(removed_result.status == R_STD_DICT_CALL_SUCCESS && removed_result.has_value);
    R_TEST_CHECK(removed.owns && removed.instance == 20U && removed.payload == 200);
    R_TEST_CHECK(ledger.key_drop_count[20U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[20U] == 0U);
    R_TEST_CHECK(created.value.length == 2U && created.value.tombstones == 1U);
    probe = r_test_dict_probe_key(&ledger, 3U);
    found = r_std_dict_get(&created.value, &probe);
    R_TEST_CHECK(found.has_value && ((const RTestDictValue *)found.value)->payload == 300);
    R_TEST_CHECK(found.value == (unsigned char *)created.value.entries + created.value.entry_size +
                                    created.value.value_offset);

    {
        RTestDictKey reinserted_key = r_test_dict_owned_key(&ledger, 2U, 21U);
        RTestDictValue reinserted_value = r_test_dict_owned_value(&ledger, 21U, 210);

        inserted =
            r_std_dict_insert(&created.value, &reinserted_key, &reinserted_value, &untouched);
        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && !inserted.did_replace);
        R_TEST_CHECK(!reinserted_key.owns && !reinserted_value.owns);
    }
    R_TEST_CHECK(created.value.length == 3U && created.value.tombstones == 0U);

    slots = created.value.slots;
    capacity = created.value.capacity;
    {
        RTestDictKey replacement_key = r_test_dict_owned_key(&ledger, 3U, 31U);
        RTestDictValue replacement_value = r_test_dict_owned_value(&ledger, 31U, 310);

        r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
        inserted =
            r_std_dict_insert(&created.value, &replacement_key, &replacement_value, &replaced);
        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && inserted.did_replace);
        R_TEST_CHECK(!replacement_key.owns && !replacement_value.owns);
        R_TEST_CHECK(replaced.owns && replaced.instance == 30U && replaced.payload == 300);
        R_TEST_CHECK(ledger.key_drop_count[31U] == 1U);
        R_TEST_CHECK(created.value.slots == slots && created.value.capacity == capacity);
        R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    }
    R_TEST_CHECK(
        r_test_dict_expect_order(
            &created.value, reordered_logical, reordered_instances, reordered_payloads, 3U) == 0);
    probe = r_test_dict_probe_key(&ledger, 99U);
    contains = r_std_dict_contains(&created.value, &probe);
    R_TEST_CHECK(contains.status == R_STD_DICT_CALL_SUCCESS && !contains.value);

    r_test_dict_value_drop(&removed);
    r_test_dict_value_drop(&replaced);
    ledger.event_count = 0U;
    slots = created.value.slots;
    capacity = created.value.capacity;
    r_std_dict_clear(&created.value);
    R_TEST_CHECK(created.value.slots == slots && created.value.capacity == capacity);
    R_TEST_CHECK(created.value.length == 0U && created.value.tombstones == 0U);
    R_TEST_CHECK(created.value.first_order == SIZE_MAX && created.value.last_order == SIZE_MAX);
    R_TEST_CHECK(ledger.event_count == 6U);
    R_TEST_CHECK(ledger.events[0].kind == R_TEST_DICT_DROP_VALUE &&
                 ledger.events[0].instance == 21U);
    R_TEST_CHECK(ledger.events[1].kind == R_TEST_DICT_DROP_KEY && ledger.events[1].instance == 21U);
    R_TEST_CHECK(ledger.events[2].kind == R_TEST_DICT_DROP_VALUE &&
                 ledger.events[2].instance == 31U);
    R_TEST_CHECK(ledger.events[3].kind == R_TEST_DICT_DROP_KEY && ledger.events[3].instance == 30U);
    R_TEST_CHECK(ledger.events[4].kind == R_TEST_DICT_DROP_VALUE &&
                 ledger.events[4].instance == 10U);
    R_TEST_CHECK(ledger.events[5].kind == R_TEST_DICT_DROP_KEY && ledger.events[5].instance == 10U);
    r_runtime_dict_destroy(&created.value);
    R_TEST_CHECK(ledger.event_count == 6U);
    R_TEST_CHECK(ledger.invalid_move_count == 0U && ledger.invalid_drop_count == 0U);
    R_TEST_CHECK(ledger.key_drop_count[10U] == 1U);
    R_TEST_CHECK(ledger.key_drop_count[20U] == 1U);
    R_TEST_CHECK(ledger.key_drop_count[21U] == 1U);
    R_TEST_CHECK(ledger.key_drop_count[30U] == 1U);
    R_TEST_CHECK(ledger.key_drop_count[31U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[10U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[20U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[21U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[30U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[31U] == 1U);
    R_TEST_CHECK(ledger.hash_count > 0U && ledger.equal_count > 0U);
    return 0;
}

static int r_test_dict_transactional_growth_failures(void) {
    RRuntimeAllocator allocator;
    RTestDictLedger ledger = {0};
    RStdDictCreateResult empty;
    RStdDictAllocValueResult created;
    RStdDictInsertResult inserted;
    RStdDictAllocResult reserved;
    RTestDictKey staged_key;
    RTestDictValue staged_value;
    RTestDictValue untouched = {90U, 900, &ledger, 0};
    const void *old_slots;
    const void *old_values[5];
    size_t old_capacity;
    size_t old_length;
    size_t old_first;
    size_t old_last;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    empty = r_std_dict_create(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(0xa110c));
    R_TEST_CHECK(empty.status == R_STD_DICT_CALL_SUCCESS);
    staged_key = r_test_dict_owned_key(&ledger, 1U, 1U);
    staged_value = r_test_dict_owned_value(&ledger, 1U, 10);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    inserted = r_std_dict_insert(&empty.value, &staged_key, &staged_value, &untouched);
    R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(inserted.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(!inserted.did_replace);
    R_TEST_CHECK(staged_key.owns && staged_value.owns);
    R_TEST_CHECK(empty.value.slots == NULL && empty.value.capacity == 0U);
    R_TEST_CHECK(empty.value.length == 0U && empty.value.tombstones == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    r_test_dict_key_drop(&staged_key);
    r_test_dict_value_drop(&staged_value);
    r_runtime_dict_destroy(&empty.value);

    r_runtime_allocator_initialize(&allocator);
    created = r_std_dict_with_capacity(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(0xa110c), 5U);
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.capacity == 8U);
    for (index = 0U; index < 5U; ++index) {
        RTestDictKey key =
            r_test_dict_owned_key(&ledger, (uint32_t)(index + 1U), (uint32_t)(index + 10U));
        RTestDictValue value =
            r_test_dict_owned_value(&ledger, (uint32_t)(index + 10U), (int32_t)(index + 100U));

        inserted = r_std_dict_insert(&created.value, &key, &value, &untouched);
        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && !inserted.did_replace);
    }
    old_slots = created.value.slots;
    old_capacity = created.value.capacity;
    old_length = created.value.length;
    old_first = created.value.first_order;
    old_last = created.value.last_order;
    for (index = 0U; index < 5U; ++index) {
        RTestDictKey probe = r_test_dict_probe_key(&ledger, (uint32_t)(index + 1U));
        RStdDictConstPointerOption found = r_std_dict_get(&created.value, &probe);

        R_TEST_CHECK(found.status == R_STD_DICT_CALL_SUCCESS && found.has_value);
        old_values[index] = found.value;
    }

    staged_key = r_test_dict_owned_key(&ledger, 6U, 16U);
    staged_value = r_test_dict_owned_value(&ledger, 16U, 106);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    inserted = r_std_dict_insert(&created.value, &staged_key, &staged_value, &untouched);
    R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(inserted.reason == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(staged_key.owns && staged_value.owns);
    R_TEST_CHECK(created.value.slots == old_slots && created.value.capacity == old_capacity);
    R_TEST_CHECK(created.value.length == old_length && created.value.tombstones == 0U);
    R_TEST_CHECK(created.value.first_order == old_first && created.value.last_order == old_last);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    for (index = 0U; index < 5U; ++index) {
        RTestDictKey probe = r_test_dict_probe_key(&ledger, (uint32_t)(index + 1U));
        RStdDictConstPointerOption found = r_std_dict_get(&created.value, &probe);

        R_TEST_CHECK(found.has_value && found.value == old_values[index]);
    }
    r_test_dict_key_drop(&staged_key);
    r_test_dict_value_drop(&staged_value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    reserved = r_std_dict_reserve(&created.value, 1U);
    R_TEST_CHECK(reserved.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(reserved.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(created.value.slots == old_slots && created.value.capacity == old_capacity);
    R_TEST_CHECK(created.value.length == old_length && created.value.first_order == old_first);
    R_TEST_CHECK(created.value.last_order == old_last);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    reserved = r_std_dict_reserve(&created.value, SIZE_MAX);
    R_TEST_CHECK(reserved.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(reserved.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(created.value.slots == old_slots && created.value.capacity == old_capacity);
    R_TEST_CHECK(created.value.length == old_length && created.value.first_order == old_first);
    R_TEST_CHECK(created.value.last_order == old_last);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    staged_key = r_test_dict_owned_key(&ledger, 6U, 17U);
    staged_value = r_test_dict_owned_value(&ledger, 17U, 106);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    inserted = r_std_dict_insert(&created.value, &staged_key, &staged_value, &untouched);
    R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && !inserted.did_replace);
    R_TEST_CHECK(!staged_key.owns && !staged_value.owns);
    R_TEST_CHECK(created.value.capacity == 16U && created.value.slots != old_slots);
    R_TEST_CHECK(created.value.length == 6U && created.value.tombstones == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    R_TEST_CHECK(ledger.key_move_count == 11U && ledger.value_move_count == 11U);
    r_runtime_dict_destroy(&created.value);
    for (index = 10U; index <= 14U; ++index) {
        R_TEST_CHECK(ledger.key_drop_count[index] == 1U);
        R_TEST_CHECK(ledger.value_drop_count[index] == 1U);
    }
    R_TEST_CHECK(ledger.key_drop_count[16U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[16U] == 1U);
    R_TEST_CHECK(ledger.key_drop_count[17U] == 1U);
    R_TEST_CHECK(ledger.value_drop_count[17U] == 1U);
    R_TEST_CHECK(ledger.invalid_move_count == 0U && ledger.invalid_drop_count == 0U);
    return 0;
}

static int r_test_dict_tombstone_reserve_compaction(void) {
    RRuntimeAllocator allocator;
    RTestDictLedger ledger = {0};
    RStdDictAllocValueResult created;
    RTestDictValue untouched = {90U, 900, &ledger, 0};
    RTestDictValue removed[2] = {{0}};
    RStdDictValueOptionResult remove_result;
    RStdDictAllocResult reserve_result;
    void *old_slots;
    void *old_entries;
    size_t old_first;
    size_t old_last;
    size_t index;
    const uint32_t compacted_logical[] = {1U, 3U, 5U};
    const uint32_t compacted_instances[] = {50U, 52U, 54U};
    const int32_t compacted_payloads[] = {500, 502, 504};
    const uint32_t final_logical[] = {1U, 3U, 5U, 6U, 7U};
    const uint32_t final_instances[] = {50U, 52U, 54U, 55U, 56U};
    const int32_t final_payloads[] = {500, 502, 504, 505, 506};

    r_runtime_allocator_initialize(&allocator);
    created = r_std_dict_with_capacity(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(0x70ab), 5U);
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.capacity == 8U && created.value.entry_capacity == 5U);
    for (index = 0U; index < 5U; ++index) {
        RTestDictKey key =
            r_test_dict_owned_key(&ledger, (uint32_t)(index + 1U), (uint32_t)(index + 50U));
        RTestDictValue value =
            r_test_dict_owned_value(&ledger, (uint32_t)(index + 50U), (int32_t)(index + 500U));
        RStdDictInsertResult inserted = r_std_dict_insert(&created.value, &key, &value, &untouched);

        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && !inserted.did_replace);
    }
    for (index = 0U; index < 2U; ++index) {
        RTestDictKey probe =
            r_test_dict_probe_key(&ledger, index == 0U ? UINT32_C(2) : UINT32_C(4));

        remove_result = r_std_dict_remove(&created.value, &probe, &removed[index]);
        R_TEST_CHECK(remove_result.status == R_STD_DICT_CALL_SUCCESS && remove_result.has_value);
        R_TEST_CHECK(removed[index].owns);
    }
    R_TEST_CHECK(created.value.length == 3U && created.value.tombstones == 2U);
    R_TEST_CHECK(
        r_test_dict_expect_order(
            &created.value, compacted_logical, compacted_instances, compacted_payloads, 3U) == 0);
    old_slots = created.value.slots;
    old_entries = created.value.entries;
    old_first = created.value.first_order;
    old_last = created.value.last_order;

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    reserve_result = r_std_dict_reserve(&created.value, 2U);
    R_TEST_CHECK(reserve_result.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(reserve_result.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(created.value.slots == old_slots && created.value.entries == old_entries);
    R_TEST_CHECK(created.value.capacity == 8U && created.value.length == 3U);
    R_TEST_CHECK(created.value.tombstones == 2U);
    R_TEST_CHECK(created.value.first_order == old_first && created.value.last_order == old_last);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    R_TEST_CHECK(
        r_test_dict_expect_order(
            &created.value, compacted_logical, compacted_instances, compacted_payloads, 3U) == 0);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    reserve_result = r_std_dict_reserve(&created.value, 2U);
    R_TEST_CHECK(reserve_result.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.capacity == 8U && created.value.slots != old_slots);
    R_TEST_CHECK(created.value.entries != old_entries && created.value.tombstones == 0U);
    R_TEST_CHECK(created.value.length == 3U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    R_TEST_CHECK(
        r_test_dict_expect_order(
            &created.value, compacted_logical, compacted_instances, compacted_payloads, 3U) == 0);
    for (index = 0U; index < 2U; ++index) {
        RTestDictKey key =
            r_test_dict_owned_key(&ledger, (uint32_t)(index + 6U), (uint32_t)(index + 55U));
        RTestDictValue value =
            r_test_dict_owned_value(&ledger, (uint32_t)(index + 55U), (int32_t)(index + 505U));
        RStdDictInsertResult inserted = r_std_dict_insert(&created.value, &key, &value, &untouched);

        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS && !inserted.did_replace);
        R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    }
    R_TEST_CHECK(r_test_dict_expect_order(
                     &created.value, final_logical, final_instances, final_payloads, 5U) == 0);
    r_test_dict_value_drop(&removed[0]);
    r_test_dict_value_drop(&removed[1]);
    r_runtime_dict_destroy(&created.value);
    for (index = 50U; index <= 56U; ++index) {
        R_TEST_CHECK(ledger.key_drop_count[index] == 1U);
        R_TEST_CHECK(ledger.value_drop_count[index] == 1U);
    }
    R_TEST_CHECK(ledger.invalid_move_count == 0U && ledger.invalid_drop_count == 0U);
    return 0;
}

static int r_test_dict_allocation_error_precedence(void) {
    RRuntimeAllocator allocator;
    RStdDictKeyInfo unsupported_key = r_test_dict_key_type();
    RStdDictKeyInfo oversized_key = r_test_dict_key_type();
    RStdDictAllocValueResult created;

    unsupported_key.type.alignment = R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U;
    oversized_key.type.size = R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U;
    oversized_key.type.alignment = R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_dict_with_capacity(
        &allocator, unsupported_key, r_test_dict_value_type(), UINT64_C(1), 0U);
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_SUCCESS);
    R_TEST_CHECK(created.value.slots == NULL && created.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_dict_destroy(&created.value);

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    created = r_std_dict_with_capacity(
        &allocator, unsupported_key, r_test_dict_value_type(), UINT64_C(1), 1U);
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(created.error == R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    created = r_std_dict_with_capacity(
        &allocator, oversized_key, r_test_dict_value_type(), UINT64_C(1), 1U);
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_ERROR);
    R_TEST_CHECK(created.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    return 0;
}

static int r_test_dict_destroy_reverse_order(void) {
    RRuntimeAllocator allocator;
    RTestDictLedger ledger = {0};
    RStdDictCreateResult created;
    RTestDictValue untouched = {90U, 900, &ledger, 0};
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_dict_create(
        &allocator, r_test_dict_key_type(), r_test_dict_value_type(), UINT64_C(9));
    R_TEST_CHECK(created.status == R_STD_DICT_CALL_SUCCESS);
    for (index = 0U; index < 3U; ++index) {
        RTestDictKey key =
            r_test_dict_owned_key(&ledger, (uint32_t)(index + 1U), (uint32_t)(index + 40U));
        RTestDictValue value =
            r_test_dict_owned_value(&ledger, (uint32_t)(index + 40U), (int32_t)(index + 400U));
        RStdDictInsertResult inserted = r_std_dict_insert(&created.value, &key, &value, &untouched);

        R_TEST_CHECK(inserted.status == R_STD_DICT_CALL_SUCCESS);
    }
    ledger.event_count = 0U;
    r_runtime_dict_destroy(&created.value);
    R_TEST_CHECK(ledger.event_count == 6U);
    for (index = 0U; index < 3U; ++index) {
        const uint32_t expected = (uint32_t)(42U - index);

        R_TEST_CHECK(ledger.events[index * 2U].kind == R_TEST_DICT_DROP_VALUE);
        R_TEST_CHECK(ledger.events[index * 2U].instance == expected);
        R_TEST_CHECK(ledger.events[(index * 2U) + 1U].kind == R_TEST_DICT_DROP_KEY);
        R_TEST_CHECK(ledger.events[(index * 2U) + 1U].instance == expected);
        R_TEST_CHECK(ledger.key_drop_count[expected] == 1U);
        R_TEST_CHECK(ledger.value_drop_count[expected] == 1U);
    }
    R_TEST_CHECK(ledger.invalid_move_count == 0U && ledger.invalid_drop_count == 0U);
    return 0;
}

int main(void) {
    if (r_test_dict_empty() != 0) {
        return 1;
    }
    if (r_test_dict_collision_tombstone_replace_and_order() != 0) {
        return 1;
    }
    if (r_test_dict_transactional_growth_failures() != 0) {
        return 1;
    }
    if (r_test_dict_tombstone_reserve_compaction() != 0) {
        return 1;
    }
    if (r_test_dict_allocation_error_precedence() != 0) {
        return 1;
    }
    if (r_test_dict_destroy_reverse_order() != 0) {
        return 1;
    }
    return 0;
}
