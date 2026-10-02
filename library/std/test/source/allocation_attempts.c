#include "r_std_test.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

/* R-SLIB-TEST-0003: the attempts of the hosted allocator since the count last restarted. */
uint64_t r_std_test_allocation_attempts(void) {
    return r_runtime_allocator_attempt_count(r_runtime_hosted_allocator());
}
