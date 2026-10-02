#ifndef R_STD_ALLOC_NATIVE_H
#define R_STD_ALLOC_NATIVE_H

/* The native provider of std.alloc::usage (Library R-SLIB-ALLOC-0004): the counters of the
   budget that the runtime installed for the calling thread (Core R-STMT-0020). */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the calling thread runs under a budget, 0 otherwise. With a budget, the outputs receive
   the bytes and tasks charged to it, its limits (UINT64_MAX for none) and the bytes that it and
   every enclosing budget still admit (UINT64_MAX when none of them limits bytes). */
int32_t r_std_alloc_native_usage(uint64_t *bytes,
                                 uint64_t *byte_limit,
                                 uint64_t *tasks,
                                 uint64_t *task_limit,
                                 uint64_t *bytes_available);

#ifdef __cplusplus
}
#endif

#endif
