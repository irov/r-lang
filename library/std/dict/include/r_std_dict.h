#ifndef R_STD_DICT_H
#define R_STD_DICT_H

#include "r_runtime_dict.h"
#include "r_std_alloc.h"

#include <stddef.h>
#include <stdint.h>

typedef RRuntimeDict RStdDict;
typedef RRuntimeDictKeyInfo RStdDictKeyInfo;
typedef RRuntimeDictIterator RStdDictIterator;
typedef RRuntimeDictEntryRef RStdDictEntryRef;

/*
 * Compiler wrapper contract:
 *
 * - K and V are represented by immutable runtime metadata emitted for their concrete types;
 * - the K metadata contains the exact compiler-provided or associated hash/equality contract;
 * - seed is a hidden per-dictionary value selected by the hosted runtime and remains fixed for
 *   the complete owner lifetime; it is not an R source-level argument;
 * - wrappers stage Move K and V inputs in caller-owned storage before calling insert;
 * - an allocation error leaves both staged inputs initialized, allowing the wrapper to construct
 *   insert_error(K,V)::allocation_failed without copying or dropping them;
 * - option wrappers initialize caller-owned V storage only when has_value is true;
 * - returned value pointers and entry references carry the originating dictionary borrow;
 * - keys are exposed only through const pointers and never through mutable access;
 * - dict Send/Sync derive structurally from K and V; iterator and entry_ref Send+Sync require K
 *   and V to be Sync; insert_error(K,V) derives capabilities from K and V;
 * - generated drop glue calls r_runtime_dict_destroy exactly once for every live dictionary.
 */

typedef enum RStdDictCallStatus {
    R_STD_DICT_CALL_SUCCESS = 0,
    R_STD_DICT_CALL_ERROR = 1,
    R_STD_DICT_CALL_CONTRACT_VIOLATION = 2
} RStdDictCallStatus;

typedef struct RStdDictCreateResult {
    RStdDictCallStatus status;
    RStdDict value;
} RStdDictCreateResult;

typedef struct RStdDictAllocValueResult {
    RStdDictCallStatus status;
    RStdAllocError error;
    RStdDict value;
} RStdDictAllocValueResult;

typedef struct RStdDictAllocResult {
    RStdDictCallStatus status;
    RStdAllocError error;
} RStdDictAllocResult;

typedef struct RStdDictInsertResult {
    RStdDictCallStatus status;
    RStdAllocError reason;
    _Bool did_replace;
} RStdDictInsertResult;

typedef struct RStdDictBoolResult {
    RStdDictCallStatus status;
    _Bool value;
} RStdDictBoolResult;

typedef struct RStdDictConstPointerOption {
    RStdDictCallStatus status;
    _Bool has_value;
    const void *value;
} RStdDictConstPointerOption;

typedef struct RStdDictMutPointerOption {
    RStdDictCallStatus status;
    _Bool has_value;
    void *value;
} RStdDictMutPointerOption;

typedef struct RStdDictValueOptionResult {
    RStdDictCallStatus status;
    _Bool has_value;
} RStdDictValueOptionResult;

typedef struct RStdDictIteratorResult {
    RStdDictCallStatus status;
    RStdDictIterator value;
} RStdDictIteratorResult;

typedef struct RStdDictEntryOption {
    RStdDictCallStatus status;
    _Bool has_value;
    RStdDictEntryRef value;
} RStdDictEntryOption;

/*
 * Ownership: allocator and metadata are shared inputs retained by the descriptor. Success returns
 * the sole empty owner without allocating. seed is hidden compiler/runtime input.
 */
RStdDictCreateResult r_std_dict_create(RRuntimeAllocator *allocator,
                                       RStdDictKeyInfo key,
                                       RRuntimeTypeInfo value,
                                       uint64_t seed);

/*
 * Ownership: success returns the sole empty owner. Failure exposes no partial allocation.
 * capacity zero performs no allocation, including for metadata whose alignment would be
 * unsupported only by a future non-empty allocation.
 */
RStdDictAllocValueResult r_std_dict_with_capacity(RRuntimeAllocator *allocator,
                                                  RStdDictKeyInfo key,
                                                  RRuntimeTypeInfo value,
                                                  uint64_t seed,
                                                  size_t capacity);

/*
 * Ownership: target is exclusive. Failure preserves its descriptor, allocation, live entries,
 * insertion order and tombstones exactly.
 */
RStdDictAllocResult r_std_dict_reserve(RStdDict *target, size_t additional);

/*
 * Ownership: staged_key and staged_value each point to one initialized caller-owned object.
 * Success consumes both. A replacement move-initializes replaced_storage before consuming the
 * incoming key and sets did_replace; insertion leaves replaced_storage untouched. Failure leaves
 * target and both staged inputs unchanged so the wrapper can materialize insert_error(K,V).
 */
RStdDictInsertResult
r_std_dict_insert(RStdDict *target, void *staged_key, void *staged_value, void *replaced_storage);

/* Ownership: source and key are shared call-bounded borrows and are never retained. */
RStdDictBoolResult r_std_dict_contains(const RStdDict *source, const void *key);
RStdDictConstPointerOption r_std_dict_get(const RStdDict *source, const void *key);
RStdDictMutPointerOption r_std_dict_get_mut(RStdDict *source, const void *key);

/*
 * Ownership: target is exclusive, key is shared, and result_storage is uninitialized aligned V
 * storage. Presence move-initializes it, drops the stored K, and removes the entry; absence leaves
 * it untouched.
 */
RStdDictValueOptionResult
r_std_dict_remove(RStdDict *target, const void *key, void *result_storage);

/*
 * Ownership: target is exclusive. Live entries are dropped in reverse insertion order, value
 * before key. The sparse table allocation and capacity are retained.
 */
void r_std_dict_clear(RStdDict *target);

/*
 * Ownership: source remains shared for the Move-only iterator lifetime. next returns Copy entry
 * references rooted in that source and permanently reports none after exhaustion.
 */
RStdDictIteratorResult r_std_dict_iter(const RStdDict *source);
RStdDictEntryOption r_std_dict_next(RStdDictIterator *iterator);

#endif
