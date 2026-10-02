#include "r_runtime_list.h"

#include <stddef.h>
#include <string.h>

static _Bool r_runtime_list_align_up(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1U;

    if (value > (SIZE_MAX - mask)) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static void *r_runtime_list_value(const RRuntimeList *list, RRuntimeListNode *node) {
    return (unsigned char *)node + list->value_offset;
}

static const void *r_runtime_list_value_const(const RRuntimeList *list,
                                              const RRuntimeListNode *node) {
    return (const unsigned char *)node + list->value_offset;
}

static void r_runtime_list_move(const RRuntimeList *list, void *destination, void *source) {
    if (list->element.move_initialize != NULL) {
        list->element.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, list->element.size);
    }
}

static RRuntimeListNode *r_runtime_list_node_from_value(const RRuntimeList *list,
                                                        const void *position) {
    return (RRuntimeListNode *)((unsigned char *)position - list->value_offset);
}

static RRuntimeListStatus
r_runtime_list_create_node(RRuntimeList *list, void *value, RRuntimeListNode **result) {
    RRuntimeAllocationStatus status;
    size_t allocation_size;
    void *allocation = NULL;

    if (list->element.size > (SIZE_MAX - list->value_offset)) {
        return R_RUNTIME_LIST_SIZE_OVERFLOW;
    }
    allocation_size = list->value_offset + list->element.size;
    status = r_runtime_allocator_allocate(
        list->allocator, allocation_size, list->node_alignment, &allocation);
    if (status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_LIST_ALLOCATION_FAILED;
    }
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_LIST_SIZE_OVERFLOW;
    }
    if (status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_LIST_UNSUPPORTED_ALIGNMENT;
    }
    if (status != R_RUNTIME_ALLOCATION_OK) {
        return R_RUNTIME_LIST_INVALID;
    }
    *result = allocation;
    (*result)->previous = NULL;
    (*result)->next = NULL;
    r_runtime_list_move(list, r_runtime_list_value(list, *result), value);
    return R_RUNTIME_LIST_OK;
}

static void
r_runtime_list_link_before(RRuntimeList *list, RRuntimeListNode *position, RRuntimeListNode *node) {
    node->next = position;
    node->previous = position == NULL ? list->last : position->previous;
    if (node->previous != NULL) {
        node->previous->next = node;
    } else {
        list->first = node;
    }
    if (position != NULL) {
        position->previous = node;
    } else {
        list->last = node;
    }
    if (node->next == NULL) {
        list->last = node;
    }
    list->length += 1U;
}

static void r_runtime_list_unlink(RRuntimeList *list, RRuntimeListNode *node) {
    if (node->previous != NULL) {
        node->previous->next = node->next;
    } else {
        list->first = node->next;
    }
    if (node->next != NULL) {
        node->next->previous = node->previous;
    } else {
        list->last = node->previous;
    }
    list->length -= 1U;
}

static void r_runtime_list_remove_node(RRuntimeList *list, RRuntimeListNode *node, void *result) {
    r_runtime_list_unlink(list, node);
    r_runtime_list_move(list, result, r_runtime_list_value(list, node));
    r_runtime_allocator_deallocate(node, list->node_alignment);
}

static void r_runtime_list_clear_valid(RRuntimeList *list) {
    RRuntimeListNode *node = list->last;

    while (node != NULL) {
        RRuntimeListNode *previous = node->previous;
        if (list->element.drop != NULL) {
            list->element.drop(r_runtime_list_value(list, node));
        }
        r_runtime_allocator_deallocate(node, list->node_alignment);
        node = previous;
    }
    list->first = NULL;
    list->last = NULL;
    list->length = 0U;
}

RRuntimeListStatus r_runtime_list_initialize(RRuntimeList *list,
                                             RRuntimeAllocator *allocator,
                                             RRuntimeTypeInfo element) {
    size_t value_offset = 0U;
    _Bool offset_valid;

    offset_valid =
        r_runtime_list_align_up(sizeof(RRuntimeListNode), element.alignment, &value_offset);
    (void)offset_valid;
    list->allocator = allocator;
    list->element = element;
    list->first = NULL;
    list->last = NULL;
    list->length = 0U;
    list->value_offset = value_offset;
    list->node_alignment = element.alignment > _Alignof(RRuntimeListNode)
                               ? element.alignment
                               : _Alignof(RRuntimeListNode);
    return R_RUNTIME_LIST_OK;
}

RRuntimeListStatus r_runtime_list_push_front(RRuntimeList *list, void *value, void **stored_value) {
    RRuntimeListNode *node;
    RRuntimeListStatus status;

    status = r_runtime_list_create_node(list, value, &node);
    if (status != R_RUNTIME_LIST_OK) {
        return status;
    }
    r_runtime_list_link_before(list, list->first, node);
    *stored_value = r_runtime_list_value(list, node);
    return R_RUNTIME_LIST_OK;
}

RRuntimeListStatus r_runtime_list_push_back(RRuntimeList *list, void *value, void **stored_value) {
    RRuntimeListNode *node;
    RRuntimeListStatus status;

    status = r_runtime_list_create_node(list, value, &node);
    if (status != R_RUNTIME_LIST_OK) {
        return status;
    }
    r_runtime_list_link_before(list, NULL, node);
    *stored_value = r_runtime_list_value(list, node);
    return R_RUNTIME_LIST_OK;
}

RRuntimeListStatus r_runtime_list_insert_before(RRuntimeList *list,
                                                const void *position,
                                                void *value,
                                                void **stored_value) {
    RRuntimeListNode *position_node;
    RRuntimeListNode *node;
    RRuntimeListStatus status;

    position_node = r_runtime_list_node_from_value(list, position);
    status = r_runtime_list_create_node(list, value, &node);
    if (status != R_RUNTIME_LIST_OK) {
        return status;
    }
    r_runtime_list_link_before(list, position_node, node);
    *stored_value = r_runtime_list_value(list, node);
    return R_RUNTIME_LIST_OK;
}

RRuntimeListStatus r_runtime_list_insert_after(RRuntimeList *list,
                                               const void *position,
                                               void *value,
                                               void **stored_value) {
    RRuntimeListNode *position_node;
    RRuntimeListNode *node;
    RRuntimeListStatus status;

    position_node = r_runtime_list_node_from_value(list, position);
    status = r_runtime_list_create_node(list, value, &node);
    if (status != R_RUNTIME_LIST_OK) {
        return status;
    }
    r_runtime_list_link_before(list, position_node->next, node);
    *stored_value = r_runtime_list_value(list, node);
    return R_RUNTIME_LIST_OK;
}

const void *r_runtime_list_front(const RRuntimeList *list) {
    if (list->first == NULL) {
        return NULL;
    }
    return r_runtime_list_value_const(list, list->first);
}

const void *r_runtime_list_back(const RRuntimeList *list) {
    if (list->last == NULL) {
        return NULL;
    }
    return r_runtime_list_value_const(list, list->last);
}

void *r_runtime_list_front_mut(RRuntimeList *list) {
    return (void *)r_runtime_list_front(list);
}

void *r_runtime_list_back_mut(RRuntimeList *list) {
    return (void *)r_runtime_list_back(list);
}

const void *r_runtime_list_get(const RRuntimeList *list, size_t index) {
    const RRuntimeListNode *node;
    size_t current;

    if (index >= list->length) {
        return NULL;
    }
    if (index <= (list->length / 2U)) {
        node = list->first;
        for (current = 0U; current < index; ++current) {
            node = node->next;
        }
    } else {
        node = list->last;
        for (current = list->length - 1U; current > index; --current) {
            node = node->previous;
        }
    }
    return r_runtime_list_value_const(list, node);
}

void *r_runtime_list_get_mut(RRuntimeList *list, size_t index) {
    return (void *)r_runtime_list_get(list, index);
}

_Bool r_runtime_list_remove(RRuntimeList *list, const void *position, void *result) {
    RRuntimeListNode *node;

    node = r_runtime_list_node_from_value(list, position);
    r_runtime_list_remove_node(list, node, result);
    return 1;
}

_Bool r_runtime_list_pop_front(RRuntimeList *list, void *result) {
    if (list->first == NULL) {
        return 0;
    }
    r_runtime_list_remove_node(list, list->first, result);
    return 1;
}

_Bool r_runtime_list_pop_back(RRuntimeList *list, void *result) {
    if (list->last == NULL) {
        return 0;
    }
    r_runtime_list_remove_node(list, list->last, result);
    return 1;
}

void r_runtime_list_clear(RRuntimeList *list) {
    if (list->allocator == NULL) {
        return;
    }
    r_runtime_list_clear_valid(list);
}

void r_runtime_list_destroy(RRuntimeList *list) {
    if (list->allocator == NULL) {
        return;
    }
    r_runtime_list_clear_valid(list);
}

RRuntimeListIterator r_runtime_list_iter(const RRuntimeList *list) {
    RRuntimeListIterator iterator;

    iterator.list = list;
    iterator.next = list->first;
    return iterator;
}

const void *r_runtime_list_next(RRuntimeListIterator *iterator) {
    const RRuntimeListNode *node;

    if (iterator->next == NULL) {
        return NULL;
    }
    node = iterator->next;
    iterator->next = node->next;
    return r_runtime_list_value_const(iterator->list, node);
}

void *r_runtime_list_destroy_last(RRuntimeList *list) {
    if ((list->allocator == NULL) || (list->last == NULL)) {
        return NULL;
    }
    return r_runtime_list_value(list, list->last);
}

_Bool r_runtime_list_destroy_is_last(const RRuntimeList *list, const void *value) {
    return (list->last != NULL) && (r_runtime_list_value_const(list, list->last) == value);
}

void *r_runtime_list_destroy_release_last(RRuntimeList *list) {
    RRuntimeListNode *node = list->last;
    RRuntimeListNode *previous = node->previous;

    r_runtime_allocator_deallocate(node, list->node_alignment);
    list->last = previous;
    if (previous == NULL) {
        list->first = NULL;
    } else {
        previous->next = NULL;
    }
    list->length -= 1U;
    return previous == NULL ? NULL : r_runtime_list_value(list, previous);
}

RRuntimeTypeInfo *r_runtime_list_destroy_scratch(RRuntimeList *list) {
    return &list->element;
}

void r_runtime_list_destroy_finish(RRuntimeList *list) {
    list->first = NULL;
    list->last = NULL;
    list->length = 0U;
}
