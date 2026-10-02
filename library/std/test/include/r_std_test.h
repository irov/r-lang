#ifndef R_STD_TEST_H
#define R_STD_TEST_H

#include <stdint.h>

/*
 * R-SLIB-TEST-0003: allocation failures for tests. Both operations act on the hosted allocator of
 * the program, allocate nothing and cannot fail. The count of attempts restarts at every call of
 * r_std_test_fail_allocation_at.
 */
uint64_t r_std_test_allocation_attempts(void);
void r_std_test_fail_allocation_at(uint64_t attempt);

#endif
