#include "r_runtime_0_1.h"

#include <stddef.h>
#include <stdint.h>

extern void r_audit_nonnull_callback(void (*callback)(void));

void r_test_hosted_drain(void);
void r_test_hosted_drain(void) {
    r_audit_nonnull_callback(NULL);
    r_runtime_hosted_drain();
}

#define r_runtime_hosted_drain r_test_hosted_drain
#include R_TEST_PROGRAM_PRELUDE
#undef r_runtime_hosted_drain
