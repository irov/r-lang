#ifndef R_RUNTIME_LIST_H
#define R_RUNTIME_LIST_H

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RRuntimeListStatus {
    R_RUNTIME_LIST_OK = 0,
    R_RUNTIME_LIST_INVALID,
    R_RUNTIME_LIST_ALLOCATION_FAILED,
    R_RUNTIME_LIST_SIZE_OVERFLOW,
    R_RUNTIME_LIST_UNSUPPORTED_ALIGNMENT
} RRuntimeListStatus;

typedef struct RRuntimeListNode RRuntimeListNode;

/*
 * Node layout shared with generated C: the compiler emits per-element-type insert, observe and
 * pop helpers over this layout, so it is part of the runtime contract and changes only together
 * with the emitter (r_c17_emit_list_helpers). Values live at list->value_offset after the node.
 */
struct RRuntimeListNode {
    RRuntimeListNode *previous;
    RRuntimeListNode *next;
};

typedef struct RRuntimeList {
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo element;
    RRuntimeListNode *first;
    RRuntimeListNode *last;
    size_t length;
    size_t value_offset;
    size_t node_alignment;
} RRuntimeList;

typedef struct RRuntimeListIterator {
    const RRuntimeList *list;
    const RRuntimeListNode *next;
} RRuntimeListIterator;

RRuntimeListStatus r_runtime_list_initialize(RRuntimeList *list,
                                             RRuntimeAllocator *allocator,
                                             RRuntimeTypeInfo element);
RRuntimeListStatus r_runtime_list_push_front(RRuntimeList *list, void *value, void **stored_value);
RRuntimeListStatus r_runtime_list_push_back(RRuntimeList *list, void *value, void **stored_value);
RRuntimeListStatus r_runtime_list_insert_before(RRuntimeList *list,
                                                const void *position,
                                                void *value,
                                                void **stored_value);
RRuntimeListStatus r_runtime_list_insert_after(RRuntimeList *list,
                                               const void *position,
                                               void *value,
                                               void **stored_value);
const void *r_runtime_list_front(const RRuntimeList *list);
const void *r_runtime_list_back(const RRuntimeList *list);
void *r_runtime_list_front_mut(RRuntimeList *list);
void *r_runtime_list_back_mut(RRuntimeList *list);
const void *r_runtime_list_get(const RRuntimeList *list, size_t index);
void *r_runtime_list_get_mut(RRuntimeList *list, size_t index);
_Bool r_runtime_list_remove(RRuntimeList *list, const void *position, void *result);
_Bool r_runtime_list_pop_front(RRuntimeList *list, void *result);
_Bool r_runtime_list_pop_back(RRuntimeList *list, void *result);
void r_runtime_list_clear(RRuntimeList *list);
void r_runtime_list_destroy(RRuntimeList *list);

/*
 * Iterative-destruction protocol used by r_runtime_drop_iterative. last yields the value of the
 * last node (NULL for an empty list); once the caller has destroyed it, release_last frees that
 * node and yields the value of the new last node. is_last tells whether value is the value of
 * the current last node. scratch exposes the element type record whose size and alignment
 * words the traversal may overwrite; finish leaves the list empty.
 * Ownership: the caller destroys each value before release_last.
 */
void *r_runtime_list_destroy_last(RRuntimeList *list);
_Bool r_runtime_list_destroy_is_last(const RRuntimeList *list, const void *value);
void *r_runtime_list_destroy_release_last(RRuntimeList *list);
RRuntimeTypeInfo *r_runtime_list_destroy_scratch(RRuntimeList *list);
void r_runtime_list_destroy_finish(RRuntimeList *list);
RRuntimeListIterator r_runtime_list_iter(const RRuntimeList *list);
const void *r_runtime_list_next(RRuntimeListIterator *iterator);

#ifdef __cplusplus
}
#endif

#endif
