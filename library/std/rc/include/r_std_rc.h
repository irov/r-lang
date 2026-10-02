#ifndef R_STD_RC_H
#define R_STD_RC_H

#include "r_runtime_rc.h"

#include <stddef.h>

typedef enum RStdRcCallStatus {
    R_STD_RC_CALL_SUCCESS = 0,
    R_STD_RC_CALL_COUNT_OVERFLOW = 1
} RStdRcCallStatus;

typedef struct RStdRcUpgradeResult {
    RStdRcCallStatus status;
    _Bool has_value;
    RRuntimeRc value;
} RStdRcUpgradeResult;

typedef enum RStdRcTryUnwrapKind {
    R_STD_RC_TRY_UNWRAP_VALUE = 0,
    R_STD_RC_TRY_UNWRAP_OWNER = 1
} RStdRcTryUnwrapKind;

typedef struct RStdRcTryUnwrapResult {
    RStdRcCallStatus status;
    RStdRcTryUnwrapKind kind;
    RRuntimeRc owner;
} RStdRcTryUnwrapResult;

/* Ownership: source is borrowed. Success creates one new strong-count duty. */
RStdRcCallStatus r_std_rc_clone(const RRuntimeRc *source, RRuntimeRc *result);

/* Ownership: source is borrowed. Success creates one new explicit weak-count duty. */
RStdRcCallStatus r_std_rc_clone_weak(const RRuntimeWeakRc *source, RRuntimeWeakRc *result);

/* Ownership: source is borrowed. Success creates one new explicit weak-count duty. */
RStdRcCallStatus r_std_rc_downgrade(const RRuntimeRc *source, RRuntimeWeakRc *result);

/* Ownership: source is borrowed. A present result owns one new strong-count duty. */
RStdRcUpgradeResult r_std_rc_upgrade(const RRuntimeWeakRc *source);

/* Ownership: owner is exclusively borrowed; the returned borrow has the same origin. */
void *r_std_rc_get_mut(RRuntimeRc *owner);

/*
 * Ownership: owner is consumed on both semantic outcomes. value_storage is uninitialized T
 * storage. A VALUE result initializes it; an OWNER result returns the unchanged owner duty.
 */
RStdRcTryUnwrapResult r_std_rc_try_unwrap(RRuntimeRc *owner, void *value_storage);

/* Ownership: source is borrowed. Neither operation changes a reference count. */
size_t r_std_rc_strong_count(const RRuntimeRc *source);
size_t r_std_rc_weak_count(const RRuntimeRc *source);

/* Ownership: both operands are borrowed. No count or pointee state changes. */
_Bool r_std_rc_ptr_eq(const RRuntimeRc *left, const RRuntimeRc *right);

/* Ownership: owner is consumed and its unchanged strong-count duty becomes one raw obligation. */
const void *r_std_rc_into_raw(RRuntimeRc *owner);

/*
 * Unsafe ownership: pointer consumes exactly one unmatched same-family raw obligation and
 * reconstructs its strong owner without changing the count.
 */
RStdRcCallStatus r_std_rc_from_raw(const void *pointer, RRuntimeRc *result);

static inline void r_std_rc_try_unwrap_result_destroy(RStdRcTryUnwrapResult *result) {
    if (result->kind == R_STD_RC_TRY_UNWRAP_OWNER) {
        r_runtime_rc_release(&result->owner);
    }
    *result = (RStdRcTryUnwrapResult){0};
}

#endif
