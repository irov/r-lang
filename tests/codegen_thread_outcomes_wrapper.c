#include "r_runtime_own.h"
#include "r_std_thread.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

RStdThreadJoinResult test_join(RStdThreadJoinHandle *handle);
void test_report_destroy(RStdThreadPanicReport *report);
void test_owner_release(RRuntimeOwn *owner);

#define r_std_thread_join test_join
#define r_std_thread_panic_report_destroy test_report_destroy
#define r_runtime_own_release test_owner_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release
#undef r_std_thread_panic_report_destroy
#undef r_std_thread_join

static _Bool simulated;
static RRuntimeAllocator report_allocator;
static size_t joins;
static size_t reports;
static atomic_size_t owners;

RStdThreadJoinResult test_join(RStdThreadJoinHandle *handle) {
    RStdThreadJoinResult result = r_std_thread_join(handle);
    ++joins;
    if (simulated) {
        static const uint8_t message[] = "simulated worker panic";
        r_std_thread_join_result_destroy(&result);
        result = (RStdThreadJoinResult){.kind = R_STD_THREAD_JOIN_PANICKED};
        result.panic.category = R_RUNTIME_PANIC_EXPLICIT;
        if (r_runtime_string_from_utf8(
                &result.panic.text, &report_allocator, message, sizeof(message) - 1U, NULL) !=
            R_RUNTIME_STRING_OK)
            abort();
    }
    return result;
}

void test_report_destroy(RStdThreadPanicReport *report) {
    RStdStringView text = r_std_thread_panic_text(report);
    if (text.length != 0U) {
        if (text.length != strlen("simulated worker panic") ||
            memcmp(text.data, "simulated worker panic", text.length) != 0)
            abort();
        ++reports;
    }
    r_std_thread_panic_report_destroy(report);
}

void test_owner_release(RRuntimeOwn *owner) {
    if (owner->allocation != NULL)
        (void)atomic_fetch_add_explicit(&owners, 1U, memory_order_relaxed);
    r_runtime_own_release(owner);
}

int main(int argc, char *argv[]) {
    char name[] = "thread-outcomes";
    char mode[] = "panicked";
    char *normal_arguments[] = {name, NULL};
    char *panic_arguments[] = {name, mode, NULL};
    (void)argc;
    (void)argv;
    r_runtime_allocator_initialize(&report_allocator);
    int status = r_generated_main(1, normal_arguments);
    if (status != 0 || joins != 16U || reports != 0U || atomic_load(&owners) != 10U)
        return 40 + status;
    simulated = 1;
    status = r_generated_main(2, panic_arguments);
    if (status != 0 || joins != 32U || reports != 16U || atomic_load(&owners) != 20U)
        return 80 + status;
    return 0;
}
