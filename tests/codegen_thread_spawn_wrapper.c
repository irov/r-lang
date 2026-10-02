#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_std_thread.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void r_test_array_destroy(RRuntimeArray *array);
static void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination);

#define r_library_internal_thread_join_result_move r_test_thread_join_result_move
#define r_runtime_array_destroy r_test_array_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_array_destroy
#undef r_library_internal_thread_join_result_move

void r_library_internal_thread_testing_fail_create(int native_error_value);

static _Atomic unsigned int r_test_drop_11;
static _Atomic unsigned int r_test_drop_41;
static _Atomic unsigned int r_test_drop_42;
static _Atomic unsigned int r_test_drop_73;
static _Atomic unsigned int r_test_drop_91;
static _Atomic unsigned int r_test_join_41;
static _Atomic unsigned int r_test_join_42;
static _Atomic unsigned int r_test_join_error_73;

static void r_test_record_drop(uint8_t marker) {
    switch (marker) {
    case UINT8_C(11):
        (void)atomic_fetch_add_explicit(&r_test_drop_11, 1U, memory_order_relaxed);
        break;
    case UINT8_C(41):
        (void)atomic_fetch_add_explicit(&r_test_drop_41, 1U, memory_order_relaxed);
        break;
    case UINT8_C(42):
        (void)atomic_fetch_add_explicit(&r_test_drop_42, 1U, memory_order_relaxed);
        break;
    case UINT8_C(73):
        (void)atomic_fetch_add_explicit(&r_test_drop_73, 1U, memory_order_relaxed);
        break;
    case UINT8_C(91):
        (void)atomic_fetch_add_explicit(&r_test_drop_91, 1U, memory_order_relaxed);
        break;
    default:
        break;
    }
}

static void r_test_array_destroy(RRuntimeArray *array) {
    if ((array != NULL) && (array->data != NULL) && (array->length == 1U)) {
        r_test_record_drop(*(const uint8_t *)array->data);
    }
    r_runtime_array_destroy(array);
}

static void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination) {
    RStdThreadCompletionTypeInfo completion_type;

    if ((result == NULL) || (destination == NULL)) {
        return;
    }
    completion_type = result->completion_type;
    r_library_internal_thread_join_result_move(result, destination);
    if (completion_type.error_count == UINT32_C(0)) {
        int32_t value = INT32_C(0);

        (void)memcpy(&value, destination, sizeof(value));
        if (value == INT32_C(41)) {
            (void)atomic_fetch_add_explicit(&r_test_join_41, 1U, memory_order_relaxed);
        } else if (value == INT32_C(42)) {
            (void)atomic_fetch_add_explicit(&r_test_join_42, 1U, memory_order_relaxed);
        }
    } else {
        uint32_t tag = UINT32_C(0);
        int32_t code = INT32_C(0);
        const unsigned char *const bytes = destination;

        (void)memcpy(&tag, bytes + completion_type.tag_offset, sizeof(tag));
        if (tag == UINT32_C(1)) {
            (void)memcpy(&code, bytes + completion_type.payload_offset, sizeof(code));
            if (code == INT32_C(-73)) {
                (void)atomic_fetch_add_explicit(&r_test_join_error_73, 1U, memory_order_relaxed);
            }
        }
    }
}

static RRuntimeTypeInfo r_test_u8_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };

    return type;
}

static _Bool r_test_array(uint8_t marker, RRuntimeArray *array) {
    RRuntimeAllocator *const allocator = r_runtime_hosted_allocator();

    return (allocator != NULL) &&
           (r_runtime_array_with_capacity(array, allocator, r_test_u8_type(), 1U) ==
            R_RUNTIME_ARRAY_OK) &&
           (r_runtime_array_push(array, &marker) == R_RUNTIME_ARRAY_OK);
}

static _Bool r_test_wait_for(_Atomic unsigned int *counter) {
    unsigned int attempt;

    for (attempt = 0U; attempt < 1000U; ++attempt) {
        if (atomic_load_explicit(counter, memory_order_acquire) == 1U) {
            return 1;
        }
        r_std_thread_sleep_nanoseconds(UINT64_C(1000000));
    }
    return 0;
}

static void r_test_effect_drop(r_d00000004 *effect) {
    if ((effect != NULL) && (effect->r_tag == UINT32_C(1))) {
        r_type_drop_a00000001_gate(&effect->r_payload.r_error_00000001);
        effect->r_tag = UINT32_C(0);
    }
}

static int r_test_thread_spawn(void) {
    RRuntimeArray payload = {0};
    r_d00000004 effect = {0};

    if (!r_test_array(UINT8_C(11), &payload)) {
        return 71;
    }
    r_library_internal_thread_testing_fail_create(EAGAIN);
    r_f00000008(payload);
    if (atomic_load_explicit(&r_test_drop_11, memory_order_relaxed) != 1U) {
        return 72;
    }

    payload = (RRuntimeArray){0};
    if (!r_test_array(UINT8_C(41), &payload)) {
        return 73;
    }
    r_f00000003(&effect, payload);
    if ((effect.r_tag != UINT32_C(0)) ||
        (atomic_load_explicit(&r_test_join_41, memory_order_relaxed) != 1U) ||
        (atomic_load_explicit(&r_test_drop_41, memory_order_relaxed) != 1U)) {
        return 74;
    }

    effect = (r_d00000004){0};
    payload = (RRuntimeArray){0};
    if (!r_test_array(UINT8_C(73), &payload)) {
        return 75;
    }
    r_f00000006(&effect, payload);
    if ((effect.r_tag != UINT32_C(1)) ||
        (effect.r_payload.r_error_00000001.r_m00000001 != INT32_C(-73)) ||
        (atomic_load_explicit(&r_test_join_error_73, memory_order_relaxed) != 1U)) {
        r_test_effect_drop(&effect);
        return 76;
    }
    r_test_effect_drop(&effect);
    if (atomic_load_explicit(&r_test_drop_73, memory_order_relaxed) != 1U) {
        return 77;
    }

    effect = (r_d00000004){0};
    payload = (RRuntimeArray){0};
    if (!r_test_array(UINT8_C(42), &payload)) {
        return 78;
    }
    r_f00000007(&effect, payload);
    if ((effect.r_tag != UINT32_C(0)) ||
        (atomic_load_explicit(&r_test_join_42, memory_order_relaxed) != 1U) ||
        (atomic_load_explicit(&r_test_drop_42, memory_order_relaxed) != 1U)) {
        return 79;
    }

    effect = (r_d00000004){0};
    payload = (RRuntimeArray){0};
    if (!r_test_array(UINT8_C(91), &payload)) {
        return 80;
    }
    r_f00000005((r_d00000005 *)&effect, payload);
    if ((effect.r_tag != UINT32_C(0)) || !r_test_wait_for(&r_test_drop_91)) {
        return 81;
    }
    r_std_thread_sleep_nanoseconds(UINT64_C(10000000));
    if ((atomic_load_explicit(&r_test_drop_11, memory_order_relaxed) != 1U) ||
        (atomic_load_explicit(&r_test_drop_41, memory_order_relaxed) != 1U) ||
        (atomic_load_explicit(&r_test_drop_42, memory_order_relaxed) != 1U) ||
        (atomic_load_explicit(&r_test_drop_73, memory_order_relaxed) != 1U) ||
        (atomic_load_explicit(&r_test_drop_91, memory_order_relaxed) != 1U)) {
        return 82;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const RRuntimeStartResult started = r_runtime_hosted_start(argc, argv);
    int result;

    if (!r_runtime_stack_initialize_current_thread()) {
        return 70;
    }
    if (!started.started) {
        return started.process_status;
    }
    result = r_test_thread_spawn();
    return r_runtime_hosted_finish((int32_t)result);
}
