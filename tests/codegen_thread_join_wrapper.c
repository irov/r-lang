#include "r_std_thread.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static RStdThreadJoinResult r_test_thread_join(RStdThreadJoinHandle *handle);
static void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination);
static void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle);

#define r_library_internal_thread_handle_destroy r_test_thread_handle_destroy
#define r_library_internal_thread_join_result_move r_test_thread_join_result_move
#define r_std_thread_join r_test_thread_join
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_std_thread_join
#undef r_library_internal_thread_join_result_move
#undef r_library_internal_thread_handle_destroy

typedef enum RTestJoinMode {
    R_TEST_JOIN_INFALLIBLE,
    R_TEST_JOIN_CHECKED_SUCCESS,
    R_TEST_JOIN_CHECKED_ERROR
} RTestJoinMode;

typedef struct RTestCheckedCompletion {
    uint32_t tag;
    union {
        int32_t success;
        struct {
            int32_t code;
        } error;
    } payload;
} RTestCheckedCompletion;

_Static_assert(sizeof(RTestCheckedCompletion) == sizeof(r_d00000002),
               "runtime completion carrier shall store R directly");
_Static_assert(sizeof(r_d00000002) < sizeof(r_d00000007),
               "runtime completion and language join carriers shall remain distinct");

static max_align_t r_test_descriptor_storage;
static RTestJoinMode r_test_mode;
static size_t r_test_join_count;
static size_t r_test_move_count;

static RStdThreadJoinResult r_test_thread_join(RStdThreadJoinHandle *handle) {
    RStdThreadJoinResult result = {0};

    if ((handle == NULL) || (handle->descriptor == NULL)) {
        result.kind = R_STD_THREAD_JOIN_PANICKED;
        return result;
    }
    handle->descriptor = NULL;
    r_test_join_count += 1U;
    result.kind = R_STD_THREAD_JOIN_RETURNED;
    return result;
}

static void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination) {
    if ((result == NULL) || (destination == NULL) || (result->kind != R_STD_THREAD_JOIN_RETURNED)) {
        return;
    }
    r_test_move_count += 1U;
    if (r_test_mode == R_TEST_JOIN_INFALLIBLE) {
        const int32_t value = INT32_C(41);

        (void)memcpy(destination, &value, sizeof(value));
    } else {
        RTestCheckedCompletion completion = {0};

        if (r_test_mode == R_TEST_JOIN_CHECKED_SUCCESS) {
            completion.payload.success = INT32_C(42);
            completion.tag = UINT32_C(0);
        } else {
            completion.payload.error.code = INT32_C(73);
            completion.tag = UINT32_C(1);
        }
        (void)memcpy(destination, &completion, sizeof(completion));
    }
    result->kind = R_STD_THREAD_JOIN_COMPLETED;
}

static void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle) {
    if (handle != NULL) {
        handle->descriptor = NULL;
    }
}

static RStdThreadJoinHandle r_test_handle(void) {
    const RStdThreadJoinHandle handle = {
        .descriptor = (RStdThreadDescriptor *)(void *)&r_test_descriptor_storage,
    };

    return handle;
}

int main(int argc, char *argv[]) {
    r_d00000004 effect = {0};

    if (!r_runtime_stack_initialize_current_thread()) {
        return 70;
    }
    r_test_mode = R_TEST_JOIN_INFALLIBLE;
    r_f00000004(r_test_handle());
    if ((r_test_join_count != 1U) || (r_test_move_count != 1U)) {
        return 71;
    }

    r_test_mode = R_TEST_JOIN_CHECKED_SUCCESS;
    r_f00000003(&effect, r_test_handle());
    if ((effect.r_tag != UINT32_C(0)) || (r_test_join_count != 2U) || (r_test_move_count != 2U)) {
        return 72;
    }

    effect = (r_d00000004){0};
    r_test_mode = R_TEST_JOIN_CHECKED_ERROR;
    r_f00000003(&effect, r_test_handle());
    if ((effect.r_tag != UINT32_C(1)) ||
        (effect.r_payload.r_error_00000001.r_m00000001 != INT32_C(73)) ||
        (r_test_join_count != 3U) || (r_test_move_count != 3U)) {
        return 73;
    }
    return r_generated_main(argc, argv);
}
