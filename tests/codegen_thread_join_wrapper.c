#include "r_std_thread.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * R-LIB-0010: the program joins real threads through each join form: an infallible handle, a
 * checked handle whose entry returns and one whose entry throws, and a scoped handle joined in a
 * @scoped async frame. The wrapper observes every join and every move of a joined value out of
 * its result, and checks that a checked completion reaches the program in the canonical carrier
 * of RStdThreadCompletionTypeInfo.
 */
RStdThreadJoinResult r_test_thread_join(RStdThreadJoinHandle *handle);
void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination);
void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle);

#define r_library_internal_thread_handle_destroy r_test_thread_handle_destroy
#define r_library_internal_thread_join_result_move r_test_thread_join_result_move
#define r_std_thread_join r_test_thread_join
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_thread_join
#undef r_library_internal_thread_join_result_move
#undef r_library_internal_thread_handle_destroy

/* The carrier of i32 throws worker_error: a uint32_t tag, then the union of the value and the
   single i32 field of the error. */
typedef struct RTestCheckedCompletion {
    uint32_t tag;
    union {
        int32_t success;
        struct {
            int32_t code;
        } error;
    } payload;
} RTestCheckedCompletion;

static size_t r_test_join_count;
static size_t r_test_move_count;
static size_t r_test_plain_41;
static size_t r_test_checked_42;
static size_t r_test_checked_error_73;
static size_t r_test_live_handle_destroys;
static _Bool r_test_valid = 1;

RStdThreadJoinResult r_test_thread_join(RStdThreadJoinHandle *handle) {
    RStdThreadJoinResult result;

    if ((handle == NULL) || (handle->descriptor == NULL)) {
        r_test_valid = 0;
    }
    result = r_std_thread_join(handle);
    /* Join consumes the handle (R-LIB-0010); the program never joins the same handle twice. */
    if ((handle->descriptor != NULL) || (result.kind != R_STD_THREAD_JOIN_RETURNED)) {
        r_test_valid = 0;
    }
    r_test_join_count += 1U;
    return result;
}

/* Replaces the size assertion of the carrier: the object hands the library the layout above for
   every checked completion, and the infallible i32 completion is stored directly. */
static _Bool r_test_checked_layout(const RStdThreadCompletionTypeInfo *completion_type) {
    return (completion_type->storage_type.size == sizeof(RTestCheckedCompletion)) &&
           (completion_type->storage_type.alignment == _Alignof(RTestCheckedCompletion)) &&
           (completion_type->tag_offset == offsetof(RTestCheckedCompletion, tag)) &&
           (completion_type->payload_offset == offsetof(RTestCheckedCompletion, payload)) &&
           (completion_type->error_count == UINT32_C(1));
}

void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination) {
    RStdThreadCompletionTypeInfo completion_type;

    if ((result == NULL) || (destination == NULL) || (result->kind != R_STD_THREAD_JOIN_RETURNED)) {
        r_test_valid = 0;
        r_library_internal_thread_join_result_move(result, destination);
        return;
    }
    completion_type = result->completion_type;
    r_library_internal_thread_join_result_move(result, destination);
    r_test_move_count += 1U;
    if (result->kind != R_STD_THREAD_JOIN_COMPLETED) {
        r_test_valid = 0;
    }
    if (completion_type.error_count == UINT32_C(0)) {
        int32_t value = INT32_C(0);

        if (completion_type.storage_type.size != sizeof(value)) {
            r_test_valid = 0;
            return;
        }
        (void)memcpy(&value, destination, sizeof(value));
        if (value == INT32_C(41)) {
            r_test_plain_41 += 1U;
        }
    } else {
        RTestCheckedCompletion completion;

        if (!r_test_checked_layout(&completion_type)) {
            r_test_valid = 0;
            return;
        }
        (void)memcpy(&completion, destination, sizeof(completion));
        if ((completion.tag == UINT32_C(0)) && (completion.payload.success == INT32_C(42))) {
            r_test_checked_42 += 1U;
        } else if ((completion.tag == UINT32_C(1)) &&
                   (completion.payload.error.code == INT32_C(73))) {
            r_test_checked_error_73 += 1U;
        }
    }
}

/* Every handle of the program is consumed by join, so none reaches its destructor live. */
void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle) {
    if ((handle != NULL) && (handle->descriptor != NULL)) {
        r_test_live_handle_destroys += 1U;
    }
    r_library_internal_thread_handle_destroy(handle);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    if (status != 0) {
        return status;
    }
    if (!r_test_valid || (r_test_live_handle_destroys != 0U)) {
        return 71;
    }
    if ((r_test_join_count != 4U) || (r_test_move_count != 4U)) {
        return 72;
    }
    if ((r_test_plain_41 != 2U) || (r_test_checked_42 != 1U) || (r_test_checked_error_73 != 1U)) {
        return 73;
    }
    return 0;
}
