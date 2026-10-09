#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_runtime_rc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result);
void r_test_own_release(RRuntimeOwn *owner);
void r_test_array_destroy(RRuntimeArray *array);
void r_test_list_destroy(RRuntimeList *list);
void r_test_dict_destroy(RRuntimeDict *dict);
void r_test_arc_release(RRuntimeArc *owner);
void r_test_rc_release(RRuntimeRc *owner);
void r_test_weak_arc_release(RRuntimeWeakArc *owner);
void r_test_weak_rc_release(RRuntimeWeakRc *owner);

#define r_runtime_arc_release r_test_arc_release
#define r_runtime_array_destroy r_test_array_destroy
#define r_runtime_dict_destroy r_test_dict_destroy
#define r_runtime_list_destroy r_test_list_destroy
#define r_runtime_own_adopt r_test_own_adopt
#define r_runtime_own_release r_test_own_release
#define r_runtime_rc_release r_test_rc_release
#define r_runtime_weak_arc_release r_test_weak_arc_release
#define r_runtime_weak_rc_release r_test_weak_rc_release
#define main r_generated_main
int main(int argc, char *argv[]);
/* The C exports of the fixture (@export_name). */
void *makeValues(void);
void dropValues(void *pointer);
void *releaseValues(void *pointer);
int checkValues(void *pointer);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_weak_rc_release
#undef r_runtime_weak_arc_release
#undef r_runtime_rc_release
#undef r_runtime_own_release
#undef r_runtime_own_adopt
#undef r_runtime_list_destroy
#undef r_runtime_dict_destroy
#undef r_runtime_array_destroy
#undef r_runtime_arc_release

/* The log of one scenario: core::adopt of the values, the release of their owner and the
 * destruction of each member with what it held at that moment (the i32 element of the array and
 * of the list, the key and value of the dict entry, the value and strong count of arc and rc).
 * Destroying an empty member (one that was moved out) does nothing and is not logged. adopt
 * receives the type glue of runtime_values, which the move scenario calls directly. */
typedef enum RTestKind {
    R_TEST_ADOPT,
    R_TEST_OWNER,
    R_TEST_ARRAY,
    R_TEST_LIST,
    R_TEST_DICT,
    R_TEST_ARC,
    R_TEST_RC,
    R_TEST_WEAK_ARC,
    R_TEST_WEAK_RC,
    R_TEST_OTHER
} RTestKind;

typedef struct RTestEvent {
    RTestKind kind;
    int64_t first;
    int64_t second;
} RTestEvent;

static RTestEvent events[16];
static size_t event_count;
static bool event_overflow;
static void *current_values;
static RRuntimeTypeInfo values_type;

static void r_test_record(RTestKind kind, int64_t first, int64_t second) {
    if (event_count < (sizeof(events) / sizeof(events[0]))) {
        events[event_count] = (RTestEvent){kind, first, second};
        ++event_count;
    } else {
        event_overflow = true;
    }
}

static void r_test_reset(void *values) {
    current_values = values;
    event_count = 0U;
    event_overflow = false;
}

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result) {
    if ((current_values != NULL) && (allocation == current_values)) {
        values_type = type;
        r_test_record(R_TEST_ADOPT, 0, 0);
    } else {
        r_test_record(R_TEST_OTHER, 0, 0);
    }
    return r_runtime_own_adopt(type, allocation, result);
}

void r_test_own_release(RRuntimeOwn *owner) {
    if ((current_values != NULL) && (owner->allocation == current_values)) {
        r_test_record(R_TEST_OWNER, 0, 0);
    } else if (owner->allocation != NULL) {
        r_test_record(R_TEST_OTHER, 0, 0);
    }
    r_runtime_own_release(owner);
}

void r_test_array_destroy(RRuntimeArray *array) {
    if (array->length == 1U) {
        r_test_record(R_TEST_ARRAY, *(const int32_t *)r_runtime_array_get(array, 0U), 1);
    } else if ((array->length != 0U) || (array->data != NULL)) {
        r_test_record(R_TEST_OTHER, 0, 0);
    }
    r_runtime_array_destroy(array);
}

void r_test_list_destroy(RRuntimeList *list) {
    if (list->length == 1U) {
        r_test_record(R_TEST_LIST, *(const int32_t *)r_runtime_list_front(list), 1);
    } else if ((list->length != 0U) || (list->first != NULL)) {
        r_test_record(R_TEST_OTHER, 0, 0);
    }
    r_runtime_list_destroy(list);
}

void r_test_dict_destroy(RRuntimeDict *dict) {
    RRuntimeDictIterator iterator = r_runtime_dict_iter(dict);
    RRuntimeDictEntryRef entry;

    if ((dict->length == 1U) && r_runtime_dict_next(&iterator, &entry)) {
        r_test_record(R_TEST_DICT, *(const int32_t *)entry.key, *(const int32_t *)entry.value);
    } else if ((dict->length != 0U) || (dict->entries != NULL)) {
        r_test_record(R_TEST_OTHER, 0, 0);
    }
    r_runtime_dict_destroy(dict);
}

void r_test_arc_release(RRuntimeArc *owner) {
    if (owner->control != NULL) {
        r_test_record(R_TEST_ARC,
                      *(const int32_t *)r_runtime_arc_get(owner),
                      (int64_t)r_runtime_arc_strong_count(owner));
    }
    r_runtime_arc_release(owner);
}

void r_test_rc_release(RRuntimeRc *owner) {
    if (owner->control != NULL) {
        r_test_record(R_TEST_RC,
                      *(const int32_t *)r_runtime_rc_get(owner),
                      (int64_t)r_runtime_rc_strong_count(owner));
    }
    r_runtime_rc_release(owner);
}

void r_test_weak_arc_release(RRuntimeWeakArc *owner) {
    if (owner->control != NULL) {
        r_test_record(R_TEST_WEAK_ARC, 0, 0);
    }
    r_runtime_weak_arc_release(owner);
}

void r_test_weak_rc_release(RRuntimeWeakRc *owner) {
    if (owner->control != NULL) {
        r_test_record(R_TEST_WEAK_RC, 0, 0);
    }
    r_runtime_weak_rc_release(owner);
}

/* R-INIT-0010: one drop of the adopted owner destroys every member once, in reverse declaration
 * order: the weak handles first, then rc and arc with their last strong reference, the dict, the
 * list and the array, each still holding its element. */
static bool r_test_dropped_values(void) {
    static const RTestEvent expected[] = {
        {R_TEST_ADOPT, 0, 0},
        {R_TEST_OWNER, 0, 0},
        {R_TEST_WEAK_RC, 0, 0},
        {R_TEST_WEAK_ARC, 0, 0},
        {R_TEST_RC, 50, 1},
        {R_TEST_ARC, 40, 1},
        {R_TEST_DICT, 30, 31},
        {R_TEST_LIST, 20, 1},
        {R_TEST_ARRAY, 10, 1},
    };
    size_t index;

    if (event_overflow || (event_count != (sizeof(expected) / sizeof(expected[0])))) {
        return false;
    }
    for (index = 0U; index < event_count; ++index) {
        if ((events[index].kind != expected[index].kind) ||
            (events[index].first != expected[index].first) ||
            (events[index].second != expected[index].second)) {
            return false;
        }
    }
    return true;
}

static int r_test_scenarios(RRuntimeAllocator *allocator) {
    void *values;
    void *returned;
    void *moved = NULL;

    /* R-UNSAFE-0008: neither adopt nor the drop of the adopted owner allocates. */
    values = makeValues();
    if ((values == NULL) || (checkValues(values) != 0)) {
        return 1;
    }
    r_test_reset(values);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    dropValues(values);
    if ((r_runtime_allocator_attempt_count(allocator) != UINT64_C(0)) || !r_test_dropped_values()) {
        return 2;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));

    /* core::release suppresses the drop and returns the same base pointer (R-FFI-0011). */
    values = makeValues();
    if (values == NULL) {
        return 3;
    }
    r_test_reset(values);
    returned = releaseValues(values);
    if ((returned != values) || event_overflow || (event_count != 1U) ||
        (events[0].kind != R_TEST_ADOPT)) {
        return 4;
    }
    r_test_reset(returned);
    dropValues(returned);
    if (!r_test_dropped_values() || (values_type.move_initialize == NULL) ||
        (values_type.drop == NULL)) {
        return 5;
    }

    /* The move glue leaves the source empty, so its drop destroys nothing; the destination holds
     * every member, the atomic value included, and drops like the original. */
    values = makeValues();
    if ((values == NULL) ||
        (r_runtime_allocator_allocate(allocator, values_type.size, values_type.alignment, &moved) !=
         R_RUNTIME_ALLOCATION_OK)) {
        return 6;
    }
    r_test_reset(NULL);
    values_type.move_initialize(moved, values);
    values_type.drop(values);
    r_runtime_allocator_deallocate(values, values_type.alignment);
    if (event_overflow || (event_count != 0U) || (checkValues(moved) != 0)) {
        return 7;
    }
    r_test_reset(moved);
    dropValues(moved);
    if (!r_test_dropped_values()) {
        return 8;
    }
    r_test_reset(NULL);
    return 0;
}

int main(int argc, char *argv[]) {
    const RRuntimeStartResult start = r_runtime_hosted_start(argc, argv);
    RRuntimeAllocator *allocator;
    int status;

    if (!start.started) {
        return start.process_status;
    }
    allocator = r_runtime_hosted_allocator();
    status = allocator == NULL ? 10 : r_test_scenarios(allocator);
    if (allocator != NULL) {
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    }
    status = r_runtime_hosted_finish(status);
    return status == 0 ? r_generated_main(argc, argv) : status;
}
