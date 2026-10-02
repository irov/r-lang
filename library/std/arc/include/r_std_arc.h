#ifndef R_STD_ARC_H
#define R_STD_ARC_H

#include "r_runtime_arc.h"

#include <stddef.h>

typedef enum RStdArcCallStatus {
    R_STD_ARC_CALL_SUCCESS = 0,
    R_STD_ARC_CALL_COUNT_OVERFLOW = 1
} RStdArcCallStatus;

typedef struct RStdArcUpgradeResult {
    RStdArcCallStatus status;
    _Bool has_value;
    RRuntimeArc value;
} RStdArcUpgradeResult;

typedef enum RStdArcTryUnwrapKind {
    R_STD_ARC_TRY_UNWRAP_VALUE = 0,
    R_STD_ARC_TRY_UNWRAP_OWNER = 1
} RStdArcTryUnwrapKind;

typedef struct RStdArcTryUnwrapResult {
    RStdArcCallStatus status;
    RStdArcTryUnwrapKind kind;
    RRuntimeArc owner;
} RStdArcTryUnwrapResult;

/* Ownership: source is borrowed. Success creates one new strong-count duty. */
RStdArcCallStatus r_std_arc_clone(const RRuntimeArc *source, RRuntimeArc *result);

/* Ownership: source is borrowed. Success creates one new explicit weak-count duty. */
RStdArcCallStatus r_std_arc_clone_weak(const RRuntimeWeakArc *source, RRuntimeWeakArc *result);

/* Ownership: source is borrowed. Success creates one new explicit weak-count duty. */
RStdArcCallStatus r_std_arc_downgrade(const RRuntimeArc *source, RRuntimeWeakArc *result);

/* Ownership: source is borrowed. A present result owns one new strong-count duty. */
RStdArcUpgradeResult r_std_arc_upgrade(const RRuntimeWeakArc *source);

/* Ownership: owner is exclusively borrowed; the returned borrow has the same origin. */
void *r_std_arc_get_mut(RRuntimeArc *owner);

/*
 * Ownership: owner is consumed on both semantic outcomes. value_storage is uninitialized T
 * storage. A VALUE result initializes it; an OWNER result returns the unchanged owner duty.
 */
RStdArcTryUnwrapResult r_std_arc_try_unwrap(RRuntimeArc *owner, void *value_storage);

/* Ownership: source is borrowed. Neither operation changes a reference count. */
size_t r_std_arc_strong_count(const RRuntimeArc *source);
size_t r_std_arc_weak_count(const RRuntimeArc *source);

/* Ownership: both operands are borrowed. No count or pointee state changes. */
_Bool r_std_arc_ptr_eq(const RRuntimeArc *left, const RRuntimeArc *right);

/* Ownership: owner is consumed and its unchanged strong-count duty becomes one raw obligation. */
const void *r_std_arc_into_raw(RRuntimeArc *owner);

/*
 * Unsafe ownership: pointer consumes exactly one unmatched same-family raw obligation and
 * reconstructs its strong owner without changing the count.
 */
RStdArcCallStatus r_std_arc_from_raw(const void *pointer, RRuntimeArc *result);

static inline void r_std_arc_try_unwrap_result_destroy(RStdArcTryUnwrapResult *result) {
    if (result->kind == R_STD_ARC_TRY_UNWRAP_OWNER) {
        r_runtime_arc_release(&result->owner);
    }
    *result = (RStdArcTryUnwrapResult){0};
}

#endif
