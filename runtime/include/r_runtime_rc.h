#ifndef R_RUNTIME_RC_H
#define R_RUNTIME_RC_H

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RRuntimeRcControl RRuntimeRcControl;

typedef struct RRuntimeRc {
    RRuntimeRcControl *control;
} RRuntimeRc;

typedef struct RRuntimeWeakRc {
    RRuntimeRcControl *control;
} RRuntimeWeakRc;

typedef enum RRuntimeRcStatus {
    R_RUNTIME_RC_OK = 0,
    R_RUNTIME_RC_INVALID,
    R_RUNTIME_RC_ALLOCATION_FAILED,
    R_RUNTIME_RC_SIZE_OVERFLOW,
    R_RUNTIME_RC_UNSUPPORTED_ALIGNMENT,
    R_RUNTIME_RC_COUNT_OVERFLOW,
    R_RUNTIME_RC_EXPIRED,
    R_RUNTIME_RC_NOT_UNIQUE
} RRuntimeRcStatus;

RRuntimeRcStatus r_runtime_rc_create(RRuntimeAllocator *allocator,
                                     RRuntimeTypeInfo type,
                                     void *value,
                                     RRuntimeRc *result);
RRuntimeRcStatus r_runtime_rc_clone(const RRuntimeRc *source, RRuntimeRc *result);
RRuntimeRcStatus r_runtime_rc_downgrade(const RRuntimeRc *source, RRuntimeWeakRc *result);
RRuntimeRcStatus r_runtime_weak_rc_clone(const RRuntimeWeakRc *source, RRuntimeWeakRc *result);
RRuntimeRcStatus r_runtime_weak_rc_upgrade(const RRuntimeWeakRc *source, RRuntimeRc *result);
const void *r_runtime_rc_get(const RRuntimeRc *owner);
void *r_runtime_rc_get_mut(RRuntimeRc *owner);
size_t r_runtime_rc_strong_count(const RRuntimeRc *owner);
size_t r_runtime_rc_weak_count(const RRuntimeRc *owner);
_Bool r_runtime_rc_ptr_eq(const RRuntimeRc *left, const RRuntimeRc *right);
RRuntimeRcStatus r_runtime_rc_try_unwrap(RRuntimeRc *owner, void *result);
const void *r_runtime_rc_into_raw(RRuntimeRc *owner);
RRuntimeRcStatus r_runtime_rc_from_raw(const void *pointer, RRuntimeRc *result);
void r_runtime_rc_release(RRuntimeRc *owner);
void r_runtime_weak_rc_release(RRuntimeWeakRc *owner);

/*
 * Iterative-destruction protocol used by r_runtime_drop_iterative. begin releases the strong
 * reference held by owner: when it was the last one, the value storage is returned and the
 * control block stays attached to owner until finish; otherwise owner is cleared and NULL is
 * returned. value returns the pointee of a still-attached owner (NULL once cleared). scratch
 * exposes the control block's type record, whose size and alignment words the traversal may
 * overwrite between begin and finish. finish releases the implicit weak reference and clears
 * owner. Ownership: owner is consumed by begin; the value storage is destroyed by the caller.
 */
void *r_runtime_rc_destroy_begin(RRuntimeRc *owner);
void *r_runtime_rc_destroy_value(RRuntimeRc *owner);
RRuntimeTypeInfo *r_runtime_rc_destroy_scratch(RRuntimeRc *owner);
void r_runtime_rc_destroy_finish(RRuntimeRc *owner);

#ifdef __cplusplus
}
#endif

#endif
