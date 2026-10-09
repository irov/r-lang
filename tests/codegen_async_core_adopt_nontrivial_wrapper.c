#include "r_runtime_own.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result);
void r_test_own_release(RRuntimeOwn *owner);

#define r_runtime_own_adopt r_test_own_adopt
#define r_runtime_own_release r_test_own_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release
#undef r_runtime_own_adopt

/* The cancellation cleanup of a task runs on an executor thread, so the counters are atomic and
 * the members released inside one Box release are collected per thread. A Box owner carries the
 * drop glue of Box; an `own i32*` member has no drop. */
static atomic_uint box_adopts;
static atomic_uint adopted_box_drops;
static atomic_uint parameter_box_drops;
static atomic_uint unexpected_events;
static _Thread_local int32_t members[2];
static _Thread_local size_t member_count;
static _Thread_local bool in_box;

static void r_test_unexpected(void) {
    atomic_fetch_add_explicit(&unexpected_events, 1U, memory_order_relaxed);
}

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result) {
    if (type.drop != NULL) {
        atomic_fetch_add_explicit(&box_adopts, 1U, memory_order_relaxed);
    } else {
        r_test_unexpected();
    }
    return r_runtime_own_adopt(type, allocation, result);
}

/* R-INIT-0010: one release of a Box owner releases its members in reverse declaration order. The
 * Box adopted by hold_box holds 31 and 47, the Box parameter of keep_box 131 and 147. */
static void r_test_release_box(RRuntimeOwn *owner) {
    if (in_box) {
        r_test_unexpected();
        r_runtime_own_release(owner);
        return;
    }
    in_box = true;
    member_count = 0U;
    r_runtime_own_release(owner);
    in_box = false;
    if ((member_count == 2U) && (members[0] == INT32_C(47)) && (members[1] == INT32_C(31))) {
        atomic_fetch_add_explicit(&adopted_box_drops, 1U, memory_order_relaxed);
    } else if ((member_count == 2U) && (members[0] == INT32_C(147)) &&
               (members[1] == INT32_C(131))) {
        atomic_fetch_add_explicit(&parameter_box_drops, 1U, memory_order_relaxed);
    } else {
        r_test_unexpected();
    }
}

void r_test_own_release(RRuntimeOwn *owner) {
    if (owner->allocation == NULL) {
        r_runtime_own_release(owner);
        return;
    }
    if (owner->type.drop != NULL) {
        r_test_release_box(owner);
        return;
    }
    if (in_box && (member_count < 2U) && (owner->type.size == sizeof(int32_t))) {
        members[member_count] = *(const int32_t *)owner->allocation;
        ++member_count;
    } else {
        r_test_unexpected();
    }
    r_runtime_own_release(owner);
}

/* R-FUNC-0011: the frame of each cancelled task destroys its owner exactly once, the adopted local
 * of hold_box during its cancellation cleanup and the parameter of keep_box whether or not its
 * body ran; the parameter that hold_box moved out is not released again. The program returns
 * after the hosted drain, which waits for that cleanup. */
int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    if (status != 0) {
        return status;
    }
    return (atomic_load(&box_adopts) == 1U) && (atomic_load(&adopted_box_drops) == 1U) &&
                   (atomic_load(&parameter_box_drops) == 1U) &&
                   (atomic_load(&unexpected_events) == 0U)
               ? 0
               : 100;
}
