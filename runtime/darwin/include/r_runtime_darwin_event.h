#ifndef R_RUNTIME_DARWIN_EVENT_H
#define R_RUNTIME_DARWIN_EVENT_H

#include <stdint.h>

/*
 * Returns one process-wide Darwin runtime event sequence. A smaller nonzero value identifies an
 * earlier linearized terminal candidate. Exhausting the 64-bit sequence is a runtime invariant
 * failure rather than permitting ambiguous wraparound ordering.
 */
uint64_t r_runtime_darwin_event_sequence_next(void);

#if defined(R_RUNTIME_DARWIN_EVENT_TESTING)
/* The caller shall ensure that no sequence operation is active while resetting test state. */
void r_runtime_darwin_event_testing_set_sequence(uint64_t sequence);
#endif

#endif
