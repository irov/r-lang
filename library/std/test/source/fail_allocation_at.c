#include "r_std_test.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

/* R-SLIB-TEST-0003: restarts the count of attempts; the attempt with this number fails, and zero
   makes none fail. */
void r_std_test_fail_allocation_at(uint64_t attempt) {
    r_runtime_allocator_set_failure(r_runtime_hosted_allocator(), attempt);
}
