#include "r_std_log_native.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_core.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* The queue of the last listener. The runtime calls the sink on the thread that delivers a
   report, so the sink only copies the report under the mutex and never blocks otherwise. */
typedef struct RStdLogNativeQueue {
    RRuntimePanicReportData *reports;
    size_t capacity;
    size_t head;
    size_t count;
    uint64_t dropped;
    uint64_t generation;
    _Bool active;
} RStdLogNativeQueue;

static pthread_mutex_t r_std_log_native_mutex = PTHREAD_MUTEX_INITIALIZER;
static RStdLogNativeQueue r_std_log_native_queue;

static void r_std_log_native_lock(void) {
    if (pthread_mutex_lock(&r_std_log_native_mutex) != 0) {
        abort();
    }
}

static void r_std_log_native_unlock(void) {
    if (pthread_mutex_unlock(&r_std_log_native_mutex) != 0) {
        abort();
    }
}

static _Bool r_std_log_native_sink(const RRuntimePanicReportData *report) {
    _Bool taken = 0;

    r_std_log_native_lock();
    if (r_std_log_native_queue.active) {
        if (r_std_log_native_queue.count == r_std_log_native_queue.capacity) {
            r_std_log_native_queue.dropped += 1U;
        } else {
            const size_t slot = (r_std_log_native_queue.head + r_std_log_native_queue.count) %
                                r_std_log_native_queue.capacity;

            r_std_log_native_queue.reports[slot] = *report;
            r_std_log_native_queue.count += 1U;
            taken = 1;
        }
    }
    r_std_log_native_unlock();
    return taken;
}

static void r_std_log_native_release(RRuntimePanicReportData *reports) {
    if (reports != NULL) {
        r_runtime_allocator_deallocate(reports, _Alignof(RRuntimePanicReportData));
    }
}

uint64_t r_std_log_native_listen(size_t capacity) {
    const size_t held = capacity == 0U ? 1U : capacity;
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    void *storage = NULL;
    RRuntimePanicReportData *reports;
    RRuntimePanicReportData *old;
    uint64_t generation;

    /* The listener is made by R code of a running hosted program, which has an allocator. */
    if ((allocator == NULL) || (held > SIZE_MAX / sizeof(*reports)) ||
        (r_runtime_allocator_allocate(
             allocator, held * sizeof(*reports), _Alignof(RRuntimePanicReportData), &storage) !=
         R_RUNTIME_ALLOCATION_OK)) {
        return 0U;
    }
    reports = storage;
    r_std_log_native_lock();
    old = r_std_log_native_queue.reports;
    generation = r_std_log_native_queue.generation + 1U;
    r_std_log_native_queue = (RStdLogNativeQueue){reports, held, 0U, 0U, 0U, generation, 1};
    r_std_log_native_unlock();
    r_std_log_native_release(old);
    /* The sink stays installed; it takes nothing while no listener is active. */
    r_runtime_panic_set_sink(r_std_log_native_sink);
    return generation;
}

int32_t r_std_log_native_take(uint64_t generation,
                              uint8_t *category,
                              size_t *category_length,
                              uint8_t *text,
                              size_t *text_length,
                              uint32_t *module,
                              uint32_t *start,
                              uint32_t *end) {
    RRuntimePanicReportData report;
    const char *name;
    size_t name_length;

    r_std_log_native_lock();
    if (!r_std_log_native_queue.active || r_std_log_native_queue.generation != generation ||
        r_std_log_native_queue.count == 0U) {
        r_std_log_native_unlock();
        return 0;
    }
    report = r_std_log_native_queue.reports[r_std_log_native_queue.head];
    r_std_log_native_queue.head =
        (r_std_log_native_queue.head + 1U) % r_std_log_native_queue.capacity;
    r_std_log_native_queue.count -= 1U;
    r_std_log_native_unlock();
    name = r_runtime_panic_category_name(report.category);
    name_length = strlen(name);
    if (name_length > 32U) {
        name_length = 32U;
    }
    memcpy(category, name, name_length);
    *category_length = name_length;
    *text_length = report.text_length > 256U ? 256U : (size_t)report.text_length;
    memcpy(text, report.text, *text_length);
    *module = report.span.module;
    *start = report.span.start;
    *end = report.span.end;
    return 1;
}

uint64_t r_std_log_native_dropped(uint64_t generation) {
    uint64_t dropped = 0U;

    r_std_log_native_lock();
    if (r_std_log_native_queue.active && r_std_log_native_queue.generation == generation) {
        dropped = r_std_log_native_queue.dropped;
    }
    r_std_log_native_unlock();
    return dropped;
}

void r_std_log_native_stop(uint64_t generation) {
    RRuntimePanicReportData *old = NULL;
    size_t head = 0U;
    size_t count = 0U;
    size_t capacity = 0U;

    r_std_log_native_lock();
    if (r_std_log_native_queue.active && r_std_log_native_queue.generation == generation) {
        old = r_std_log_native_queue.reports;
        head = r_std_log_native_queue.head;
        count = r_std_log_native_queue.count;
        capacity = r_std_log_native_queue.capacity;
        r_std_log_native_queue.reports = NULL;
        r_std_log_native_queue.capacity = 0U;
        r_std_log_native_queue.count = 0U;
        r_std_log_native_queue.active = 0;
    }
    r_std_log_native_unlock();
    /* The reports nobody took are written as the lines they would have been. */
    for (size_t index = 0U; index < count; ++index) {
        r_runtime_panic_deliver(&old[(head + index) % capacity]);
    }
    r_std_log_native_release(old);
}
