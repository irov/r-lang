module runtime.drops;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Own, Arc, Rc, Array, List, Dict, DropMember, DropMemberKind, DropNode};
import runtime.arcs;
import runtime.rcs;
import runtime.arrays;
import runtime.lists;
import runtime.dicts;

// Iterative destruction of self-nesting aggregates (r_runtime_drop.h, Core R-FUNC-0004): every
// pointee and element is entered in a loop, and an entered owner slot, control block or container
// header keeps where its owner resumes in the size and alignment words of its type record, which
// the traversal takes from the descriptor instead once it has entered the storage.
@header("r_runtime_drop.h")
extern "C" {
}

protected bool is_slot(DropMemberKind kind) {
    return (kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_OWN) ||
           (kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_RC) ||
           (kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_ARC);
}

protected bool is_sequence(DropMemberKind kind) {
    return (kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_ARRAY) ||
           (kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_LIST) ||
           (kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_DICT);
}

protected raw void*? own_value(raw void*? slot) {
    unsafe {
        raw Own*? owner = slot as raw Own*?;
        return owner->allocation;
    }
}

// Releases the slot's reference and returns the pointee when this traversal must destroy it.
protected raw void*? slot_begin(DropMemberKind kind, raw void*? slot) {
    unsafe {
        switch (kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_OWN:
            return own_value(slot);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_RC:
            return runtime.rcs::r_runtime_rc_destroy_begin(slot as raw Rc*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARC:
            return runtime.arcs::r_runtime_arc_destroy_begin(slot as raw Arc*?);
        default:
            return null;
        }
    }
}

// The pointee of a slot that is still attached, or null once the slot has been finished.
protected raw void*? slot_value(DropMemberKind kind, raw void*? slot) {
    unsafe {
        switch (kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_OWN:
            return own_value(slot);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_RC:
            return runtime.rcs::r_runtime_rc_destroy_value(slot as raw Rc*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARC:
            return runtime.arcs::r_runtime_arc_destroy_value(slot as raw Arc*?);
        default:
            return null;
        }
    }
}

// Releases the pointee storage of an entered slot after the pointee has been destroyed.
protected void slot_finish(DropMemberKind kind, raw void*? slot) {
    unsafe {
        switch (kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_OWN:
            raw Own*? owner = slot as raw Own*?;
            raw void*? allocation = owner->allocation;
            c_size alignment = owner->allocation_alignment;
            *owner = Own {.allocation = null,
                          .type = TypeInfo {.size = 0usize as c_size, .alignment = 0usize as c_size,
                                            .move_initialize = null, .drop_value = null},
                          .allocation_alignment = 0usize as c_size};
            runtime.heap::r_runtime_allocator_deallocate(allocation, alignment);
            break;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_RC:
            runtime.rcs::r_runtime_rc_destroy_finish(slot as raw Rc*?);
            break;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARC:
            runtime.arcs::r_runtime_arc_destroy_finish(slot as raw Arc*?);
            break;
        default:
            break;
        }
    }
}

protected usize stride_of(raw const DropMember* member) {
    unsafe {
        raw const DropNode*? child = member->child;
        return child->size as usize;
    }
}

// The element destroyed first (the last one), or null when the sequence is empty.
protected raw void*? sequence_first(raw const DropMember* member) {
    unsafe {
        raw void*? address = member->address;
        switch (member->kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARRAY:
            usize count = runtime.arrays::r_runtime_array_destroy_count(address as raw const Array*?) as usize;
            if (count == 0usize) {
                return null;
            }
            return runtime.arrays::r_runtime_array_destroy_element(
                address as raw Array*?, (count - 1usize) as c_size, stride_of(member) as c_size);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_LIST:
            return runtime.lists::r_runtime_list_destroy_last(address as raw List*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_DICT:
            usize entries = runtime.dicts::r_runtime_dict_destroy_count(address as raw const Dict*?) as usize;
            if (entries == 0usize) {
                return null;
            }
            return runtime.dicts::r_runtime_dict_destroy_value(address as raw Dict*?, (entries - 1usize) as c_size);
        default:
            return null;
        }
    }
}

// When `current` is the element of the sequence under destruction, releases what the sequence
// holds for it (a list node, a dict key) and yields the next element, null when none remains.
protected bool sequence_advance(raw const DropMember* member, raw void*? current, raw (raw void*?)* next) {
    unsafe {
        raw void*? address = member->address;
        c_size index = 0usize as c_size;
        switch (member->kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARRAY:
            c_size stride = stride_of(member) as c_size;
            if (runtime.arrays::r_runtime_array_destroy_locate(address as raw const Array*?, current as raw const void*?, stride, &index as raw c_size*) == 0i32 as c_bool) {
                return false;
            }
            if ((index as usize) == 0usize) {
                *next = null;
            } else {
                *next = runtime.arrays::r_runtime_array_destroy_element(address as raw Array*?, ((index as usize) - 1usize) as c_size, stride);
            }
            return true;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_LIST:
            if (runtime.lists::r_runtime_list_destroy_is_last(address as raw const List*?, current as raw const void*?) == 0i32 as c_bool) {
                return false;
            }
            *next = runtime.lists::r_runtime_list_destroy_release_last(address as raw List*?);
            return true;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_DICT:
            if (runtime.dicts::r_runtime_dict_destroy_locate(address as raw const Dict*?, current as raw const void*?, &index as raw c_size*) == 0i32 as c_bool) {
                return false;
            }
            runtime.dicts::r_runtime_dict_destroy_key(address as raw Dict*?, index);
            if ((index as usize) == 0usize) {
                *next = null;
            } else {
                *next = runtime.dicts::r_runtime_dict_destroy_value(address as raw Dict*?, ((index as usize) - 1usize) as c_size);
            }
            return true;
        default:
            return false;
        }
    }
}

protected raw TypeInfo*? scratch_of(DropMemberKind kind, raw void*? storage) {
    unsafe {
        switch (kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_OWN:
            raw Own*? owner = storage as raw Own*?;
            return &owner->type as raw TypeInfo*;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_RC:
            return runtime.rcs::r_runtime_rc_destroy_scratch(storage as raw Rc*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARC:
            return runtime.arcs::r_runtime_arc_destroy_scratch(storage as raw Arc*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARRAY:
            return runtime.arrays::r_runtime_array_destroy_scratch(storage as raw Array*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_LIST:
            return runtime.lists::r_runtime_list_destroy_scratch(storage as raw List*?);
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_DICT:
            return runtime.dicts::r_runtime_dict_destroy_scratch(storage as raw Dict*?);
        default:
            return null;
        }
    }
}

// Releases the container storage after every element has been destroyed.
protected void sequence_finish(raw const DropMember* member) {
    unsafe {
        raw void*? address = member->address;
        switch (member->kind) {
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_ARRAY:
            raw const DropNode*? child = member->child;
            runtime.arrays::r_runtime_array_destroy_finish(address as raw Array*?, child->alignment);
            break;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_LIST:
            runtime.lists::r_runtime_list_destroy_finish(address as raw List*?);
            break;
        case DropMemberKind::R_RUNTIME_DROP_MEMBER_DICT:
            runtime.dicts::r_runtime_dict_destroy_finish(address as raw Dict*?);
            break;
        default:
            break;
        }
    }
}

// The two words of an entered storage's type record hold its owner's storage and descriptor:
// the pointers are stored in that memory itself, so no address becomes an integer.
protected void store_resume(raw TypeInfo*? scratch, raw void*? parent, raw const DropNode*? parent_node) {
    unsafe {
        raw (raw void*?)*? first = (scratch as raw void*?) as raw (raw void*?)*?;
        *first = parent;
        raw (raw const DropNode*?)*? second = core::offset_bytes(scratch, sizeof(c_size)) as raw (raw const DropNode*?)*?;
        *second = parent_node;
    }
}

protected raw void*? resume_storage(raw TypeInfo*? scratch) {
    unsafe {
        raw (raw void*?)*? first = (scratch as raw void*?) as raw (raw void*?)*?;
        return *first;
    }
}

protected raw const DropNode*? resume_node(raw TypeInfo*? scratch) {
    unsafe {
        raw (raw const DropNode*?)*? second = core::offset_bytes(scratch, sizeof(c_size)) as raw (raw const DropNode*?)*?;
        return *second;
    }
}

// Member `index` of the storage at `storage`, described by `node`, or false once all are given.
protected bool describe(raw const DropNode*? node, raw void*? storage, usize index, raw DropMember* member) {
    unsafe {
        raw fn?(raw void*?, c_size, raw DropMember*?) -> c_bool cursor = node->cursor;
        return cursor(storage, index as c_size, member) != 0i32 as c_bool;
    }
}

@safety("R-RUNTIME-DROP-ITERATIVE", "root is storage described by node; every pointee and element it reaches is owned by it")
extern "C" void r_runtime_drop_iterative(raw void*? root, raw const DropNode*? node) {
    raw void*? current = root;
    raw const DropNode*? current_node = node;
    raw void*? parent = null;
    raw const DropNode*? parent_node = null;
    usize index = 0usize;
    DropMember member = DropMember {.kind = DropMemberKind::R_RUNTIME_DROP_MEMBER_NONE,
                                    .address = null, .drop_value = null, .child = null};
    unsafe {
        raw DropMember* member_address = &member as raw DropMember*;
        while (true) {
            if (describe(current_node, current, index, member_address) == true) {
                index += 1usize;
                if (member.kind == DropMemberKind::R_RUNTIME_DROP_MEMBER_LEAF) {
                    raw fn?(raw void*?) -> void drop_glue = member.drop_value;
                    drop_glue(member.address);
                    continue;
                }
                raw void*? child = null;
                if (is_slot(member.kind) == true) {
                    child = slot_begin(member.kind, member.address);
                } else {
                    if (is_sequence(member.kind) == true) {
                        child = sequence_first(member_address as raw const DropMember*);
                        if (child == null) {
                            sequence_finish(member_address as raw const DropMember*);
                        }
                    }
                }
                if (child == null) {
                    continue;
                }
                // Enter the pointee or element; the storage remembers where its owner resumes.
                store_resume(scratch_of(member.kind, member.address), parent, parent_node);
                parent = current;
                parent_node = current_node;
                current = child;
                current_node = member.child;
                index = 0usize;
                continue;
            }
            if (parent == null) {
                return;
            }
            // The pointee or element is destroyed: find its owner in the parent and move on.
            index = 0usize;
            while (true) {
                raw void*? next = null;
                if (describe(parent_node, parent, index, member_address) == false) {
                    return;
                }
                index += 1usize;
                bool owner_found = false;
                if (is_slot(member.kind) == true) {
                    owner_found = slot_value(member.kind, member.address) == current;
                } else {
                    if (is_sequence(member.kind) == true) {
                        owner_found = sequence_advance(member_address as raw const DropMember*, current, &next as raw (raw void*?)*);
                    }
                }
                if (owner_found == false) {
                    continue;
                }
                if (next != null) {
                    current = next;
                    current_node = member.child;
                    index = 0usize;
                    break;
                }
                raw TypeInfo*? scratch = scratch_of(member.kind, member.address);
                raw void*? grandparent = resume_storage(scratch);
                raw const DropNode*? grandparent_node = resume_node(scratch);
                if (is_slot(member.kind) == true) {
                    slot_finish(member.kind, member.address);
                } else {
                    sequence_finish(member_address as raw const DropMember*);
                }
                current = parent;
                current_node = parent_node;
                parent = grandparent;
                parent_node = grandparent_node;
                break;
            }
        }
    }
}
