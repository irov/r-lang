#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_own.h"
#include "r_runtime_task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

RRuntimeOwnStatus r_test_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result);
void r_test_own_release(RRuntimeOwn *owner);
void r_test_task_destroy(RRuntimeTask **task);

#define r_runtime_own_adopt r_test_own_adopt
#define r_runtime_own_release r_test_own_release
#define r_runtime_task_destroy r_test_task_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
/* The C exports of the fixture (@export_name). */
void *makeBox(int base, int selected);
void adoptBox(void *pointer);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_task_destroy
#undef r_runtime_own_release
#undef r_runtime_own_adopt

/* The log of one scenario: core::adopt of the Box, the release of its owner, the destruction of its
 * task and the release of each `own i32*` or `own u32*` member by its value. Releasing an empty
 * owner or destroying an empty task does nothing and is not logged. adopt_box adopts with the type
 * glue of Box, which the move scenario calls directly. */
enum {
    R_TEST_ADOPT = -1,
    R_TEST_BOX = -2,
    R_TEST_TASK = -3,
    R_TEST_OTHER = -4
};

static int64_t events[16];
static size_t event_count;
static bool event_overflow;
static void *current_box;
static RRuntimeTypeInfo box_type;

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
    if ((current_box != NULL) && (allocation == current_box)) {
        box_type = type;
        r_test_record(R_TEST_ADOPT);
    } else {
        r_test_record(R_TEST_OTHER);
    }
    return r_runtime_own_adopt(type, allocation, result);
}

void r_test_own_release(RRuntimeOwn *owner) {
    if ((current_box != NULL) && (owner->allocation == current_box)) {
        r_test_record(R_TEST_BOX);
    } else if ((owner->allocation != NULL) && (owner->type.size == sizeof(int32_t))) {
        /* The members hold small positive values, so i32 and u32 read alike. */
        r_test_record(*(const int32_t *)owner->allocation);
    } else if (owner->allocation != NULL) {
        r_test_record(R_TEST_OTHER);
    }
    r_runtime_own_release(owner);
}

void r_test_task_destroy(RRuntimeTask **task) {
    if (*task != NULL) {
        r_test_record(R_TEST_TASK);
    }
    r_runtime_task_destroy(task);
}

/* R-INIT-0010: fields in reverse declaration order (pending, unsigned_outcome or outcome,
 * optional, then fixed from its last element down); only the selected option is released. */
static bool r_test_dropped_box(int64_t base) {
    return !event_overflow && (event_count == 7U) && (events[0] == R_TEST_ADOPT) &&
           (events[1] == R_TEST_BOX) && (events[2] == R_TEST_TASK) && (events[3] == base + 40) &&
           (events[4] == base + 30) && (events[5] == base + 20) && (events[6] == base + 10);
}

static int r_test_scenarios(RRuntimeAllocator *allocator) {
    void *box;
    void *moved = NULL;

    /* R-UNSAFE-0008: neither adopt nor the drop glue of the adopted Box allocates. */
    box = makeBox(0, 0);
    if (box == NULL) {
        return 1;
    }
    r_test_reset(box);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    adoptBox(box);
    if ((r_runtime_allocator_attempt_count(allocator) != UINT64_C(0)) || !r_test_dropped_box(0)) {
        return 2;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));

    box = makeBox(100, 1);
    if (box == NULL) {
        return 3;
    }
    r_test_reset(box);
    adoptBox(box);
    if (!r_test_dropped_box(100) || (box_type.move_initialize == NULL) || (box_type.drop == NULL)) {
        return 4;
    }

    /* The move glue leaves the source empty, so its drop releases nothing; the destination is a
     * whole Box that R adopts and drops once. */
    box = makeBox(200, 0);
    if ((box == NULL) ||
        (r_runtime_allocator_allocate(allocator, box_type.size, box_type.alignment, &moved) !=
         R_RUNTIME_ALLOCATION_OK)) {
        return 5;
    }
    r_test_reset(NULL);
    box_type.move_initialize(moved, box);
    box_type.drop(box);
    r_runtime_allocator_deallocate(box, box_type.alignment);
    if (event_overflow || (event_count != 0U)) {
        return 6;
    }
    r_test_reset(moved);
    adoptBox(moved);
    if (!r_test_dropped_box(200)) {
        return 7;
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
