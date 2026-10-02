#ifndef R_RUNTIME_ARC_H
#define R_RUNTIME_ARC_H

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RRuntimeArcControl RRuntimeArcControl;

typedef struct RRuntimeArc {
    RRuntimeArcControl *control;
} RRuntimeArc;

typedef struct RRuntimeWeakArc {
    RRuntimeArcControl *control;
} RRuntimeWeakArc;

typedef enum RRuntimeArcStatus {
    R_RUNTIME_ARC_OK = 0,
    R_RUNTIME_ARC_INVALID,
    R_RUNTIME_ARC_ALLOCATION_FAILED,
    R_RUNTIME_ARC_SIZE_OVERFLOW,
    R_RUNTIME_ARC_UNSUPPORTED_ALIGNMENT,
    R_RUNTIME_ARC_COUNT_OVERFLOW,
    R_RUNTIME_ARC_EXPIRED,
    R_RUNTIME_ARC_NOT_UNIQUE
} RRuntimeArcStatus;

RRuntimeArcStatus r_runtime_arc_create(RRuntimeAllocator *allocator,
                                       RRuntimeTypeInfo type,
                                       void *value,
                                       RRuntimeArc *result);
RRuntimeArcStatus r_runtime_arc_clone(const RRuntimeArc *source, RRuntimeArc *result);
RRuntimeArcStatus r_runtime_arc_downgrade(const RRuntimeArc *source, RRuntimeWeakArc *result);
RRuntimeArcStatus r_runtime_weak_arc_clone(const RRuntimeWeakArc *source, RRuntimeWeakArc *result);
RRuntimeArcStatus r_runtime_weak_arc_upgrade(const RRuntimeWeakArc *source, RRuntimeArc *result);
const void *r_runtime_arc_get(const RRuntimeArc *owner);
void *r_runtime_arc_get_mut(RRuntimeArc *owner);
size_t r_runtime_arc_strong_count(const RRuntimeArc *owner);
size_t r_runtime_arc_weak_count(const RRuntimeArc *owner);
_Bool r_runtime_arc_ptr_eq(const RRuntimeArc *left, const RRuntimeArc *right);
RRuntimeArcStatus r_runtime_arc_try_unwrap(RRuntimeArc *owner, void *result);
const void *r_runtime_arc_into_raw(RRuntimeArc *owner);
RRuntimeArcStatus r_runtime_arc_from_raw(const void *pointer, RRuntimeArc *result);
void r_runtime_arc_release(RRuntimeArc *owner);
void r_runtime_weak_arc_release(RRuntimeWeakArc *owner);

/*
 * Iterative-destruction protocol used by r_runtime_drop_iterative; the same contract as the rc
 * form. begin performs the release-ordered strong decrement and the acquire fence of the last
 * release, so the caller observes every write made through the other strong handles.
 * Ownership: owner is consumed by begin; the value storage is destroyed by the caller.
 */
void *r_runtime_arc_destroy_begin(RRuntimeArc *owner);
void *r_runtime_arc_destroy_value(RRuntimeArc *owner);
RRuntimeTypeInfo *r_runtime_arc_destroy_scratch(RRuntimeArc *owner);
void r_runtime_arc_destroy_finish(RRuntimeArc *owner);

#ifdef __cplusplus
}
#endif

#endif
