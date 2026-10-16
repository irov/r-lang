module runtime.lists;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Allocator, AllocationStatus, List, ListNode, ListIterator, ListStatus};

// Doubly linked lists (r_runtime_list.h): each node is one allocation holding the links and,
// after them at the element's alignment, the element.
@header("r_runtime_list.h")
extern "C" {
}

protected raw void*? value_of(raw List*? items, raw ListNode*? node) {
    unsafe {
        return core::offset_bytes(node, items->value_offset as usize);
    }
}

protected raw const void*? value_of_const(raw const List*? items, raw const ListNode*? node) {
    unsafe {
        return core::offset_bytes(node, items->value_offset as usize);
    }
}

// The node of a value of the list: the C interface passes the value as `const void *` and the
// node is changed through it, as the C runtime does by its cast.
protected raw ListNode*? node_of(raw List* items, raw const void*? position) {
    unsafe {
        isize back = 0isize - ((items->value_offset as usize) as isize);
        raw const ListNode*? node = core::offset_bytes(position, back) as raw const ListNode*?;
        return core::cast_mutable(node);
    }
}

protected ListStatus create_node(raw List* items, raw void*? value, raw (raw ListNode*?)* result) {
    unsafe {
        usize offset = items->value_offset as usize;
        usize size = items->element.size as usize;
        if (size > runtime.types::size_maximum - offset) {
            return ListStatus::R_RUNTIME_LIST_SIZE_OVERFLOW;
        }
        raw void*? allocation = null;
        AllocationStatus status = runtime.heap::r_runtime_allocator_allocate(
            items->allocator, (offset + size) as c_size, items->node_alignment,
            &allocation as raw (raw void*?)*);
        switch (status) {
        case AllocationStatus::R_RUNTIME_ALLOCATION_OK:
            break;
        case AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED:
            return ListStatus::R_RUNTIME_LIST_ALLOCATION_FAILED;
        case AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
            return ListStatus::R_RUNTIME_LIST_SIZE_OVERFLOW;
        case AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
            return ListStatus::R_RUNTIME_LIST_UNSUPPORTED_ALIGNMENT;
        default:
            return ListStatus::R_RUNTIME_LIST_INVALID;
        }
        raw ListNode*? node = allocation as raw ListNode*?;
        node->previous = null;
        node->next = null;
        runtime.types::move_value(items->element, value_of(items, node), value);
        *result = node;
    }
    return ListStatus::R_RUNTIME_LIST_OK;
}

protected void link_before(raw List* items, raw ListNode*? position, raw ListNode*? node) {
    unsafe {
        node->next = position;
        if (position == null) {
            node->previous = items->last;
        } else {
            node->previous = position->previous;
        }
        raw ListNode*? previous = node->previous;
        if (previous != null) {
            previous->next = node;
        } else {
            items->first = node;
        }
        if (position != null) {
            position->previous = node;
        } else {
            items->last = node;
        }
        if (node->next == null) {
            items->last = node;
        }
        items->length = ((items->length as usize) + 1usize) as c_size;
    }
}

protected void unlink(raw List* items, raw ListNode*? node) {
    unsafe {
        raw ListNode*? previous = node->previous;
        raw ListNode*? next = node->next;
        if (previous != null) {
            previous->next = next;
        } else {
            items->first = next;
        }
        if (next != null) {
            next->previous = previous;
        } else {
            items->last = previous;
        }
        items->length = ((items->length as usize) - 1usize) as c_size;
    }
}

protected void remove_node(raw List* items, raw ListNode*? node, raw void*? result) {
    unlink(items, node);
    unsafe {
        runtime.types::move_value(items->element, result, value_of(items, node));
        runtime.heap::r_runtime_allocator_deallocate(node as raw void*?, items->node_alignment);
    }
}

protected void clear_valid(raw List* items) {
    unsafe {
        raw fn?(raw void*?) -> void drop_glue = items->element.drop_value;
        raw ListNode*? node = items->last;
        while (node != null) {
            raw ListNode*? previous = node->previous;
            if (drop_glue != null) {
                drop_glue(value_of(items, node));
            }
            runtime.heap::r_runtime_allocator_deallocate(node as raw void*?, items->node_alignment);
            node = previous;
        }
        items->first = null;
        items->last = null;
        items->length = 0usize as c_size;
    }
}

protected ListStatus push_linked(raw List* items, raw ListNode*? position, raw void*? value, raw (raw void*?)* stored_value) {
    unsafe {
        raw ListNode*? node = null;
        ListStatus status = create_node(items, value, &node as raw (raw ListNode*?)*);
        if (status != ListStatus::R_RUNTIME_LIST_OK) {
            return status;
        }
        link_before(items, position, node);
        *stored_value = value_of(items, node);
    }
    return ListStatus::R_RUNTIME_LIST_OK;
}

@safety("R-RUNTIME-LIST-INITIALIZE", "items is writable; the element alignment is a power of two")
extern "C" ListStatus r_runtime_list_initialize(raw List* items, raw Allocator*? allocator, TypeInfo element) {
    // The value follows the two links at the element's alignment; the node takes the larger of
    // the two alignments.
    usize mask = (element.alignment as usize) - 1usize;
    usize links = sizeof(ListNode);
    usize value_offset = 0usize;
    if (links <= runtime.types::size_maximum - mask) {
        value_offset = (links + mask) & (runtime.types::size_maximum ^ mask);
    }
    usize node_alignment = alignof(ListNode);
    if ((element.alignment as usize) > node_alignment) {
        node_alignment = element.alignment as usize;
    }
    unsafe {
        *items = List {.allocator = allocator, .element = element, .first = null, .last = null,
                       .length = 0usize as c_size, .value_offset = value_offset as c_size,
                       .node_alignment = node_alignment as c_size};
    }
    return ListStatus::R_RUNTIME_LIST_OK;
}

@safety("R-RUNTIME-LIST-PUSH-FRONT", "items is initialized; value moves into the list; stored_value is writable")
extern "C" ListStatus r_runtime_list_push_front(raw List* items, raw void*? value, raw (raw void*?)* stored_value) {
    unsafe {
        return push_linked(items, items->first, value, stored_value);
    }
}

@safety("R-RUNTIME-LIST-PUSH-BACK", "items is initialized; value moves into the list; stored_value is writable")
extern "C" ListStatus r_runtime_list_push_back(raw List* items, raw void*? value, raw (raw void*?)* stored_value) {
    return push_linked(items, null, value, stored_value);
}

@safety("R-RUNTIME-LIST-INSERT-BEFORE", "position is a value of the list; value moves into it; stored_value is writable")
extern "C" ListStatus r_runtime_list_insert_before(raw List* items, raw const void*? position, raw void*? value, raw (raw void*?)* stored_value) {
    return push_linked(items, node_of(items, position), value, stored_value);
}

@safety("R-RUNTIME-LIST-INSERT-AFTER", "position is a value of the list; value moves into it; stored_value is writable")
extern "C" ListStatus r_runtime_list_insert_after(raw List* items, raw const void*? position, raw void*? value, raw (raw void*?)* stored_value) {
    unsafe {
        raw ListNode*? position_node = node_of(items, position);
        return push_linked(items, position_node->next, value, stored_value);
    }
}

@safety("R-RUNTIME-LIST-FRONT", "items is initialized")
extern "C" raw const void*? r_runtime_list_front(raw const List* items) {
    unsafe {
        if (items->first == null) {
            return null;
        }
        return value_of_const(items, items->first as raw const ListNode*?);
    }
}

@safety("R-RUNTIME-LIST-BACK", "items is initialized")
extern "C" raw const void*? r_runtime_list_back(raw const List* items) {
    unsafe {
        if (items->last == null) {
            return null;
        }
        return value_of_const(items, items->last as raw const ListNode*?);
    }
}

@safety("R-RUNTIME-LIST-FRONT-MUT", "items is initialized")
extern "C" raw void*? r_runtime_list_front_mut(raw List* items) {
    unsafe {
        if (items->first == null) {
            return null;
        }
        return value_of(items, items->first);
    }
}

@safety("R-RUNTIME-LIST-BACK-MUT", "items is initialized")
extern "C" raw void*? r_runtime_list_back_mut(raw List* items) {
    unsafe {
        if (items->last == null) {
            return null;
        }
        return value_of(items, items->last);
    }
}

protected raw const ListNode*? node_at(raw const List* items, usize index) {
    unsafe {
        usize length = items->length as usize;
        if (index <= length / 2usize) {
            raw const ListNode*? node = items->first as raw const ListNode*?;
            for (usize current = 0usize; current < index; current += 1usize) {
                node = node->next as raw const ListNode*?;
            }
            return node;
        }
        raw const ListNode*? node = items->last as raw const ListNode*?;
        usize current = length - 1usize;
        while (current > index) {
            node = node->previous as raw const ListNode*?;
            current -= 1usize;
        }
        return node;
    }
}

@safety("R-RUNTIME-LIST-GET", "items is initialized")
extern "C" raw const void*? r_runtime_list_get(raw const List* items, c_size index) {
    unsafe {
        if ((index as usize) >= (items->length as usize)) {
            return null;
        }
        return value_of_const(items, node_at(items, index as usize));
    }
}

@safety("R-RUNTIME-LIST-GET-MUT", "items is initialized")
extern "C" raw void*? r_runtime_list_get_mut(raw List* items, c_size index) {
    unsafe {
        if ((index as usize) >= (items->length as usize)) {
            return null;
        }
        return core::cast_mutable(value_of_const(items as raw const List*, node_at(items as raw const List*, index as usize)));
    }
}

@safety("R-RUNTIME-LIST-REMOVE", "position is a value of the list; result addresses storage for one element")
extern "C" c_bool r_runtime_list_remove(raw List* items, raw const void*? position, raw void*? result) {
    remove_node(items, node_of(items, position), result);
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-LIST-POP-FRONT", "items is initialized; result addresses storage for one element")
extern "C" c_bool r_runtime_list_pop_front(raw List* items, raw void*? result) {
    unsafe {
        if (items->first == null) {
            return 0i32 as c_bool;
        }
        remove_node(items, items->first, result);
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-LIST-POP-BACK", "items is initialized; result addresses storage for one element")
extern "C" c_bool r_runtime_list_pop_back(raw List* items, raw void*? result) {
    unsafe {
        if (items->last == null) {
            return 0i32 as c_bool;
        }
        remove_node(items, items->last, result);
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-LIST-CLEAR", "items is initialized or zeroed")
extern "C" void r_runtime_list_clear(raw List* items) {
    unsafe {
        if (items->allocator == null) {
            return;
        }
    }
    clear_valid(items);
}

@safety("R-RUNTIME-LIST-DESTROY", "items is initialized or zeroed")
extern "C" void r_runtime_list_destroy(raw List* items) {
    unsafe {
        if (items->allocator == null) {
            return;
        }
    }
    clear_valid(items);
}

@safety("R-RUNTIME-LIST-ITER", "items is initialized and outlives the iterator")
extern "C" ListIterator r_runtime_list_iter(raw const List* items) {
    unsafe {
        return ListIterator {.list_value = items, .next = items->first as raw const ListNode*?};
    }
}

@safety("R-RUNTIME-LIST-NEXT", "iterator came from r_runtime_list_iter of a live list")
extern "C" raw const void*? r_runtime_list_next(raw ListIterator* iterator) {
    unsafe {
        raw const ListNode*? node = iterator->next;
        if (node == null) {
            return null;
        }
        iterator->next = node->next as raw const ListNode*?;
        return value_of_const(iterator->list_value, node);
    }
}

@safety("R-RUNTIME-LIST-DESTROY-LAST", "items is initialized or zeroed")
extern "C" raw void*? r_runtime_list_destroy_last(raw List*? items) {
    unsafe {
        if ((items->allocator == null) || (items->last == null)) {
            return null;
        }
        return value_of(items, items->last);
    }
}

@safety("R-RUNTIME-LIST-DESTROY-IS-LAST", "items is being destroyed")
extern "C" c_bool r_runtime_list_destroy_is_last(raw const List*? items, raw const void*? value) {
    unsafe {
        if (items->last == null) {
            return 0i32 as c_bool;
        }
        raw const void*? last_value = value_of_const(items, items->last as raw const ListNode*?);
        if (last_value == value) {
            return 1i32 as c_bool;
        }
    }
    return 0i32 as c_bool;
}

@safety("R-RUNTIME-LIST-DESTROY-RELEASE-LAST", "items is being destroyed and has a last node whose value is destroyed")
extern "C" raw void*? r_runtime_list_destroy_release_last(raw List*? items) {
    unsafe {
        raw ListNode*? node = items->last;
        raw ListNode*? previous = node->previous;
        runtime.heap::r_runtime_allocator_deallocate(node as raw void*?, items->node_alignment);
        items->last = previous;
        items->length = ((items->length as usize) - 1usize) as c_size;
        if (previous == null) {
            items->first = null;
            return null;
        }
        previous->next = null;
        return value_of(items, previous);
    }
}

@safety("R-RUNTIME-LIST-DESTROY-SCRATCH", "items is being destroyed")
extern "C" raw TypeInfo* r_runtime_list_destroy_scratch(raw List*? items) {
    unsafe {
        return &items->element as raw TypeInfo*;
    }
}

@safety("R-RUNTIME-LIST-DESTROY-FINISH", "every node of the list is released")
extern "C" void r_runtime_list_destroy_finish(raw List*? items) {
    unsafe {
        items->first = null;
        items->last = null;
        items->length = 0usize as c_size;
    }
}
