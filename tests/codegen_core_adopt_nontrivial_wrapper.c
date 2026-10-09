#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_own.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result);
void r_test_own_release(RRuntimeOwn *owner);

#define r_runtime_own_adopt r_test_own_adopt
#define r_runtime_own_release r_test_own_release
#define main r_generated_main
int main(int argc, char *argv[]);
/* The C exports of the fixture (@export_name). */
void *makeBox(int first, int second);
void dropBox(void *pointer, int early);
void *releaseBox(void *pointer);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release
#undef r_runtime_own_adopt

/* The log of one scenario: core::adopt of the Box, the release of its owner and the release of
 * each `own i32*` member by its value. Releases of empty owners do nothing and are not logged. */
enum {
    R_TEST_ADOPT = -1,
    R_TEST_BOX = -2,
    R_TEST_OTHER = -3
};

static int64_t events[8];
static size_t event_count;
static bool event_overflow;
static void *current_box;

static void r_test_record(int64_t event) {
    if (event_count < (sizeof(events) / sizeof(events[0]))) {
        events[event_count] = event;
        ++event_count;
    } else {
        event_overflow = true;
    }
}

static void r_test_reset(void *box) {
    current_box = box;
    event_count = 0U;
    event_overflow = false;
}

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result) {
    r_test_record(allocation == current_box ? R_TEST_ADOPT : R_TEST_OTHER);
    return r_runtime_own_adopt(type, allocation, result);
}

void r_test_own_release(RRuntimeOwn *owner) {
    if (owner->allocation == current_box) {
        r_test_record(R_TEST_BOX);
    } else if ((owner->allocation != NULL) && (owner->type.size == sizeof(int32_t))) {
        r_test_record(*(const int32_t *)owner->allocation);
    } else if (owner->allocation != NULL) {
        r_test_record(R_TEST_OTHER);
    }
    r_runtime_own_release(owner);
}

/* R-INIT-0010: the adopted owner drops the Box once, members in reverse declaration order. */
static bool r_test_dropped_box_once(void) {
    return !event_overflow && (event_count == 4U) && (events[0] == R_TEST_ADOPT) &&
           (events[1] == R_TEST_BOX) && (events[2] == INT64_C(22)) && (events[3] == INT64_C(11));
}

static int r_test_scenarios(RRuntimeAllocator *allocator) {
    void *box;
    void *returned;

    /* R-UNSAFE-0008: neither adopt nor the drop of the adopted owner allocates. */
    box = makeBox(11, 22);
    if (box == NULL) {
        return 1;
    }
    r_test_reset(box);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    dropBox(box, 0);
    if ((r_runtime_allocator_attempt_count(allocator) != UINT64_C(0)) ||
        !r_test_dropped_box_once()) {
        return 2;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));

    /* An early return out of the unsafe block drops the owner as well. */
    box = makeBox(11, 22);
    if (box == NULL) {
        return 3;
    }
    r_test_reset(box);
    dropBox(box, 1);
    if (!r_test_dropped_box_once()) {
        return 4;
    }

    /* core::release suppresses the drop and returns the same base pointer (R-FFI-0011), which
     * stays adoptable. */
    box = makeBox(11, 22);
    if (box == NULL) {
        return 5;
    }
    r_test_reset(box);
    returned = releaseBox(box);
    if ((returned != box) || event_overflow || (event_count != 1U) || (events[0] != R_TEST_ADOPT)) {
        return 6;
    }
    r_test_reset(returned);
    dropBox(returned, 0);
    if (!r_test_dropped_box_once()) {
        return 7;
    }
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
