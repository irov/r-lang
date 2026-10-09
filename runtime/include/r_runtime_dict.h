#ifndef R_RUNTIME_DICT_H
#define R_RUNTIME_DICT_H

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t (*RRuntimeHashFn)(const void *value);
typedef _Bool (*RRuntimeEqualFn)(const void *left, const void *right);

typedef struct RRuntimeDictKeyInfo {
    RRuntimeTypeInfo type;
    RRuntimeHashFn hash;
    RRuntimeEqualFn equal;
} RRuntimeDictKeyInfo;

typedef enum RRuntimeDictStatus {
    R_RUNTIME_DICT_OK = 0,
    R_RUNTIME_DICT_INVALID,
    R_RUNTIME_DICT_ALLOCATION_FAILED,
    R_RUNTIME_DICT_SIZE_OVERFLOW,
    R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT
} RRuntimeDictStatus;

typedef struct RRuntimeDict {
    RRuntimeAllocator *allocator;
    RRuntimeDictKeyInfo key;
    RRuntimeTypeInfo value;
    void *slots;
    void *entries;
    size_t length;
    size_t tombstones;
    size_t capacity;
    size_t entry_capacity;
    size_t entries_offset;
    size_t first_order;
    size_t last_order;
    size_t key_offset;
    size_t value_offset;
    size_t slot_size;
    size_t slot_alignment;
    size_t entry_size;
    size_t entry_alignment;
    uint64_t seed;
} RRuntimeDict;

/*
 * Index and entry layout shared with generated code: the compiler emits per-key-type probe,
 * lookup and insert paths over this layout (compiler/llvm/dicts.c), so it is part of the
 * runtime contract and changes only together with the emitter.
 */
typedef enum RRuntimeDictSlotState {
    R_RUNTIME_DICT_SLOT_EMPTY = 0,
    R_RUNTIME_DICT_SLOT_LIVE,
    R_RUNTIME_DICT_SLOT_TOMBSTONE
} RRuntimeDictSlotState;

typedef struct RRuntimeDictIndexSlot {
    size_t entry_index;
    RRuntimeDictSlotState state;
} RRuntimeDictIndexSlot;

typedef struct RRuntimeDictEntryHeader {
    uint64_t hash;
} RRuntimeDictEntryHeader;

typedef struct RRuntimeDictIterator {
    const RRuntimeDict *dict;
    size_t next_order;
} RRuntimeDictIterator;

typedef struct RRuntimeDictEntryRef {
    const void *key;
    const void *value;
} RRuntimeDictEntryRef;

RRuntimeDictStatus r_runtime_dict_initialize(RRuntimeDict *dict,
                                             RRuntimeAllocator *allocator,
                                             RRuntimeDictKeyInfo key,
                                             RRuntimeTypeInfo value,
                                             uint64_t seed);
RRuntimeDictStatus r_runtime_dict_with_capacity(RRuntimeDict *dict,
                                                RRuntimeAllocator *allocator,
                                                RRuntimeDictKeyInfo key,
                                                RRuntimeTypeInfo value,
                                                uint64_t seed,
                                                size_t capacity);
RRuntimeDictStatus r_runtime_dict_reserve(RRuntimeDict *dict, size_t additional);
RRuntimeDictStatus r_runtime_dict_insert(
    RRuntimeDict *dict, void *key, void *value, void *replaced_value, _Bool *did_replace);
_Bool r_runtime_dict_contains(const RRuntimeDict *dict, const void *key);
const void *r_runtime_dict_get(const RRuntimeDict *dict, const void *key);
void *r_runtime_dict_get_mut(RRuntimeDict *dict, const void *key);
_Bool r_runtime_dict_remove(RRuntimeDict *dict, const void *key, void *removed_value);
void r_runtime_dict_clear(RRuntimeDict *dict);
void r_runtime_dict_destroy(RRuntimeDict *dict);

/*
 * Iterative-destruction protocol used by r_runtime_drop_iterative. Entries are destroyed from
 * the last insertion order down: the caller destroys value(index), then key(index) drops the
 * key. locate maps a value address back to its entry index. scratch exposes the value type
 * record whose size and alignment words the traversal may overwrite; finish releases the
 * storage and leaves the dict empty.
 * Ownership: the caller destroys every value before finish.
 */
size_t r_runtime_dict_destroy_count(const RRuntimeDict *dict);
void *r_runtime_dict_destroy_value(RRuntimeDict *dict, size_t index);
_Bool r_runtime_dict_destroy_locate(const RRuntimeDict *dict, const void *value, size_t *index);
void r_runtime_dict_destroy_key(RRuntimeDict *dict, size_t index);
RRuntimeTypeInfo *r_runtime_dict_destroy_scratch(RRuntimeDict *dict);
void r_runtime_dict_destroy_finish(RRuntimeDict *dict);
RRuntimeDictIterator r_runtime_dict_iter(const RRuntimeDict *dict);
_Bool r_runtime_dict_next(RRuntimeDictIterator *iterator, RRuntimeDictEntryRef *entry);

#ifdef __cplusplus
}
#endif

#endif
