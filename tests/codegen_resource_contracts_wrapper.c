#include "r_runtime_0_1.h"

#define main r_generated_main
int main(int argc, char *argv[]);
/* The C export of check_resources (@export_name). */
int checkResources(void);
#include R_TEST_PROGRAM_PRELUDE
#undef main

int main(int argc, char *argv[]) {
    const RRuntimeStartResult start = r_runtime_hosted_start(argc, argv);
    RRuntimeAllocator *allocator;
    uint64_t before;
    int status = 0;
    if (!start.started) {
        return start.process_status;
    }
    allocator = r_runtime_hosted_allocator();
    if (allocator == NULL) {
        return r_runtime_hosted_finish(10);
    }
    r_runtime_allocator_set_failure(allocator, 1U);
    before = r_runtime_allocator_attempt_count(allocator);
    for (unsigned index = 0U; index < 100U; ++index) {
        if (checkResources() != 0) {
            status = 11;
            break;
        }
    }
    if (r_runtime_allocator_attempt_count(allocator) != before) {
        status = 12;
    }
    r_runtime_allocator_set_failure(allocator, 0U);
    status = r_runtime_hosted_finish(status);
    return status == 0 ? r_generated_main(argc, argv) : status;
}
