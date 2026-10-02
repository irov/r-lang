#include "r_runtime_drop.h"

#include "r_runtime_allocator.h"
#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_runtime_rc.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The traversal stores two pointers in the size and alignment words of an entered owner slot's
 * type record (RRuntimeOwn), control block (rc, arc) or container header (array, list, dict).
 * Both words are static properties of the pointee or element type that the traversal takes from
 * the descriptor instead once the storage has been entered.
 */
_Static_assert(sizeof(size_t) >= sizeof(uintptr_t),
               "owner slot scratch words must hold the traversal pointers");

static _Bool r_runtime_drop_member_is_slot(RRuntimeDropMemberKind kind) {
    return (kind == R_RUNTIME_DROP_MEMBER_OWN) || (kind == R_RUNTIME_DROP_MEMBER_RC) ||
           (kind == R_RUNTIME_DROP_MEMBER_ARC);
}

static _Bool r_runtime_drop_member_is_sequence(RRuntimeDropMemberKind kind) {
    return (kind == R_RUNTIME_DROP_MEMBER_ARRAY) || (kind == R_RUNTIME_DROP_MEMBER_LIST) ||
           (kind == R_RUNTIME_DROP_MEMBER_DICT);
}

/* Releases the slot's reference and returns the pointee when this traversal must destroy it. */
static void *r_runtime_drop_slot_begin(RRuntimeDropMemberKind kind, void *slot) {
    switch (kind) {
    case R_RUNTIME_DROP_MEMBER_OWN:
        return ((RRuntimeOwn *)slot)->allocation;
    case R_RUNTIME_DROP_MEMBER_RC:
        return r_runtime_rc_destroy_begin(slot);
    case R_RUNTIME_DROP_MEMBER_ARC:
        return r_runtime_arc_destroy_begin(slot);
    default:
        return NULL;
    }
}

/* The pointee of a slot that is still attached, or NULL once the slot has been finished. */
static void *r_runtime_drop_slot_value(RRuntimeDropMemberKind kind, void *slot) {
    switch (kind) {
    case R_RUNTIME_DROP_MEMBER_OWN:
        return ((RRuntimeOwn *)slot)->allocation;
    case R_RUNTIME_DROP_MEMBER_RC:
        return r_runtime_rc_destroy_value(slot);
    case R_RUNTIME_DROP_MEMBER_ARC:
        return r_runtime_arc_destroy_value(slot);
    default:
        return NULL;
    }
}

/* Releases the pointee storage of an entered slot after the pointee has been destroyed. */
static void r_runtime_drop_slot_finish(RRuntimeDropMemberKind kind, void *slot) {
    switch (kind) {
    case R_RUNTIME_DROP_MEMBER_OWN: {
        RRuntimeOwn *owner = slot;
        void *allocation = owner->allocation;
        size_t alignment = owner->allocation_alignment;

        *owner = (RRuntimeOwn){0};
        r_runtime_allocator_deallocate(allocation, alignment);
        break;
    }
    case R_RUNTIME_DROP_MEMBER_RC:
        r_runtime_rc_destroy_finish(slot);
        break;
    case R_RUNTIME_DROP_MEMBER_ARC:
        r_runtime_arc_destroy_finish(slot);
        break;
    default:
        break;
    }
}

/* The element destroyed first (the last one), or NULL when the sequence is empty. */
static void *r_runtime_drop_sequence_first(const RRuntimeDropMember *member) {
    size_t count;

    switch (member->kind) {
    case R_RUNTIME_DROP_MEMBER_ARRAY:
        count = r_runtime_array_destroy_count(member->address);
        return count == 0U ? NULL
                           : r_runtime_array_destroy_element(
                                 member->address, count - 1U, member->child->size);
    case R_RUNTIME_DROP_MEMBER_LIST:
        return r_runtime_list_destroy_last(member->address);
    case R_RUNTIME_DROP_MEMBER_DICT:
        count = r_runtime_dict_destroy_count(member->address);
        return count == 0U ? NULL : r_runtime_dict_destroy_value(member->address, count - 1U);
    default:
        return NULL;
    }
}

/*
 * When `current` is the element of the sequence under destruction, releases what the sequence
 * holds for it (a list node, a dict key) and yields the next element, NULL when none remains.
 */
static _Bool
r_runtime_drop_sequence_advance(const RRuntimeDropMember *member, void *current, void **next) {
    size_t index;

    switch (member->kind) {
    case R_RUNTIME_DROP_MEMBER_ARRAY:
        if (!r_runtime_array_destroy_locate(
                member->address, current, member->child->size, &index)) {
            return 0;
        }
        *next =
            index == 0U
                ? NULL
                : r_runtime_array_destroy_element(member->address, index - 1U, member->child->size);
        return 1;
    case R_RUNTIME_DROP_MEMBER_LIST:
        if (!r_runtime_list_destroy_is_last(member->address, current)) {
            return 0;
        }
        *next = r_runtime_list_destroy_release_last(member->address);
        return 1;
    case R_RUNTIME_DROP_MEMBER_DICT:
        if (!r_runtime_dict_destroy_locate(member->address, current, &index)) {
            return 0;
        }
        r_runtime_dict_destroy_key(member->address, index);
        *next = index == 0U ? NULL : r_runtime_dict_destroy_value(member->address, index - 1U);
        return 1;
    default:
        return 0;
    }
}

static RRuntimeTypeInfo *r_runtime_drop_scratch(RRuntimeDropMemberKind kind, void *storage) {
    switch (kind) {
    case R_RUNTIME_DROP_MEMBER_OWN:
        return &((RRuntimeOwn *)storage)->type;
    case R_RUNTIME_DROP_MEMBER_RC:
        return r_runtime_rc_destroy_scratch(storage);
    case R_RUNTIME_DROP_MEMBER_ARC:
        return r_runtime_arc_destroy_scratch(storage);
    case R_RUNTIME_DROP_MEMBER_ARRAY:
        return r_runtime_array_destroy_scratch(storage);
    case R_RUNTIME_DROP_MEMBER_LIST:
        return r_runtime_list_destroy_scratch(storage);
    case R_RUNTIME_DROP_MEMBER_DICT:
        return r_runtime_dict_destroy_scratch(storage);
    default:
        return NULL;
    }
}

/* Releases the container storage after every element has been destroyed. */
static void r_runtime_drop_sequence_finish(const RRuntimeDropMember *member) {
    switch (member->kind) {
    case R_RUNTIME_DROP_MEMBER_ARRAY:
        r_runtime_array_destroy_finish(member->address, member->child->alignment);
        break;
    case R_RUNTIME_DROP_MEMBER_LIST:
        r_runtime_list_destroy_finish(member->address);
        break;
    case R_RUNTIME_DROP_MEMBER_DICT:
        r_runtime_dict_destroy_finish(member->address);
        break;
    default:
        break;
    }
}

void r_runtime_drop_iterative(void *root, const RRuntimeDropNode *node) {
    void *current = root;
    const RRuntimeDropNode *current_node = node;
    void *parent = NULL;
    const RRuntimeDropNode *parent_node = NULL;
    size_t index = 0U;
    RRuntimeDropMember member;

    for (;;) {
        if (current_node->cursor(current, index, &member)) {
            void *child;
            RRuntimeTypeInfo *scratch;

            index += 1U;
            if (member.kind == R_RUNTIME_DROP_MEMBER_LEAF) {
                member.drop(member.address);
                continue;
            }
            if (r_runtime_drop_member_is_slot(member.kind)) {
                child = r_runtime_drop_slot_begin(member.kind, member.address);
            } else if (r_runtime_drop_member_is_sequence(member.kind)) {
                child = r_runtime_drop_sequence_first(&member);
                if (child == NULL) {
                    r_runtime_drop_sequence_finish(&member);
                }
            } else {
                child = NULL;
            }
            if (child == NULL) {
                continue;
            }
            /* Enter the pointee or element; the storage remembers where its owner resumes. */
            scratch = r_runtime_drop_scratch(member.kind, member.address);
            scratch->size = (size_t)(uintptr_t)parent;
            scratch->alignment = (size_t)(uintptr_t)parent_node;
            parent = current;
            parent_node = current_node;
            current = child;
            current_node = member.child;
            index = 0U;
            continue;
        }
        if (parent == NULL) {
            return;
        }
        /* The pointee or element is destroyed: find its owner in the parent and move on. */
        index = 0U;
        for (;;) {
            void *next = NULL;
            _Bool owner_found;

            if (!parent_node->cursor(parent, index, &member)) {
                return;
            }
            index += 1U;
            if (r_runtime_drop_member_is_slot(member.kind)) {
                owner_found = r_runtime_drop_slot_value(member.kind, member.address) == current;
            } else if (r_runtime_drop_member_is_sequence(member.kind)) {
                owner_found = r_runtime_drop_sequence_advance(&member, current, &next);
            } else {
                owner_found = 0;
            }
            if (!owner_found) {
                continue;
            }
            if (next != NULL) {
                current = next;
                current_node = member.child;
                index = 0U;
                break;
            }
            {
                RRuntimeTypeInfo *scratch = r_runtime_drop_scratch(member.kind, member.address);
                void *grandparent = (void *)(uintptr_t)scratch->size;
                const RRuntimeDropNode *grandparent_node =
                    (const RRuntimeDropNode *)(uintptr_t)scratch->alignment;

                if (r_runtime_drop_member_is_slot(member.kind)) {
                    r_runtime_drop_slot_finish(member.kind, member.address);
                } else {
                    r_runtime_drop_sequence_finish(&member);
                }
                current = parent;
                current_node = parent_node;
                parent = grandparent;
                parent_node = grandparent_node;
            }
            break;
        }
    }
}
