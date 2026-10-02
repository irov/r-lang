#ifndef R_STD_LIST_H
#define R_STD_LIST_H

#include "r_runtime_list.h"
#include "r_std_alloc.h"

#include <stddef.h>

typedef RRuntimeList RStdList;
typedef RRuntimeListIterator RStdListIterator;

/*
 * Compiler wrapper contract:
 *
 * - T is represented by immutable RRuntimeTypeInfo metadata emitted for the concrete type;
 * - wrappers stage Move T inputs in caller-owned storage and materialize push_error(T) from the
 *   mapped reason plus that still-initialized T after an allocation error;
 * - every returned element pointer carries the originating list and node identity in its R borrow;
 * - option wrappers initialize caller T storage only when has_value is true;
 * - list Send derives from T Send, list Sync from T Sync, iterator Send+Sync from T Sync, and
 *   push_error(T) capabilities structurally from T;
 * - generated drop glue calls r_runtime_list_destroy exactly once for every live list owner.
 */

typedef enum RStdListCallStatus {
    R_STD_LIST_CALL_SUCCESS = 0,
    R_STD_LIST_CALL_ERROR = 1,
    R_STD_LIST_CALL_CONTRACT_VIOLATION = 2
} RStdListCallStatus;

typedef struct RStdListCreateResult {
    RStdListCallStatus status;
    RStdList value;
} RStdListCreateResult;

typedef struct RStdListInsertResult {
    RStdListCallStatus status;
    RStdAllocError reason;
    void *value;
} RStdListInsertResult;

typedef struct RStdListConstPointerOption {
    RStdListCallStatus status;
    _Bool has_value;
    const void *value;
} RStdListConstPointerOption;

typedef struct RStdListMutPointerOption {
    RStdListCallStatus status;
    _Bool has_value;
    void *value;
} RStdListMutPointerOption;

typedef struct RStdListValueOptionResult {
    RStdListCallStatus status;
    _Bool has_value;
} RStdListValueOptionResult;

typedef struct RStdListValueResult {
    RStdListCallStatus status;
} RStdListValueResult;

typedef struct RStdListIteratorResult {
    RStdListCallStatus status;
    RStdListIterator value;
} RStdListIteratorResult;

/*
 * Ownership: allocator and element metadata are shared call-bounded inputs retained by the list
 * descriptor. Success returns the sole empty list owner and performs no allocation.
 */
RStdListCreateResult r_std_list_create(RRuntimeAllocator *allocator, RRuntimeTypeInfo element);

/*
 * Ownership: target is exclusive. staged_value points to one initialized T. Success consumes it,
 * allocates exactly one node, and returns an exclusive element borrow. Failure preserves target
 * and staged_value so the wrapper can construct push_error(T).
 */
RStdListInsertResult r_std_list_push_front(RStdList *target, void *staged_value);
RStdListInsertResult r_std_list_push_back(RStdList *target, void *staged_value);

/*
 * Ownership: position is a shared node borrow with target's hidden origin. staged_value follows
 * the same transactional contract as push. Existing element addresses are preserved.
 */
RStdListInsertResult
r_std_list_insert_before(RStdList *target, const void *position, void *staged_value);
RStdListInsertResult
r_std_list_insert_after(RStdList *target, const void *position, void *staged_value);

/* Ownership: returned pointers are node borrows rooted in source and are never retained. */
RStdListConstPointerOption r_std_list_front(const RStdList *source);
RStdListConstPointerOption r_std_list_back(const RStdList *source);
RStdListMutPointerOption r_std_list_front_mut(RStdList *source);
RStdListMutPointerOption r_std_list_back_mut(RStdList *source);
RStdListConstPointerOption r_std_list_get(const RStdList *source, size_t index);
RStdListMutPointerOption r_std_list_get_mut(RStdList *source, size_t index);

/*
 * Ownership: element is the sole exclusive node borrow from target and is consumed. Success
 * unlinks exactly that node, move-initializes result_storage, and releases only its node storage.
 */
RStdListValueResult r_std_list_remove(RStdList *target, void *element, void *result_storage);

/*
 * Ownership: target is exclusive and result_storage is uninitialized aligned T storage.
 * has_value true initializes it by move; false leaves it untouched.
 */
RStdListValueOptionResult r_std_list_pop_front(RStdList *target, void *result_storage);
RStdListValueOptionResult r_std_list_pop_back(RStdList *target, void *result_storage);

/*
 * Ownership: target is exclusive. Live values are dropped back-to-front and every node allocation
 * is released exactly once.
 */
void r_std_list_clear(RStdList *target);

/*
 * Ownership: source is shared for the iterator lifetime. The iterator is Move-only and retains no
 * owner; the compiler keeps its hidden source borrow live until iterator drop/last use.
 */
RStdListIteratorResult r_std_list_iter(const RStdList *source);
RStdListConstPointerOption r_std_list_next(RStdListIterator *iterator);

#endif
