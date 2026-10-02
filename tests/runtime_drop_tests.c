#include "r_runtime_allocator.h"
#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_drop.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_runtime_rc.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Self-nesting aggregates as the compiler lays them out: a user drop hook, then owner slots or
 * containers. Chains of one million nodes would exhaust the main thread stack under recursive
 * destruction.
 */
#define R_DROP_TEST_CHAIN_LENGTH ((uint32_t)1000000)
#define R_DROP_TEST_SPINE_LENGTH ((uint32_t)250000)

typedef struct RDropTestOwnNode {
    uint32_t value;
    RRuntimeOwn next;
} RDropTestOwnNode;

typedef struct RDropTestRcNode {
    uint32_t value;
    RRuntimeRc next;
} RDropTestRcNode;

typedef struct RDropTestArcNode {
    uint32_t value;
    RRuntimeArc next;
} RDropTestArcNode;

/* struct Tree { u32 value; Pair pair; }; struct Pair { own Tree*? left; own Tree*? right; }; */
typedef struct RDropTestPair {
    RRuntimeOwn left;
    RRuntimeOwn right;
} RDropTestPair;

typedef struct RDropTestTree {
    uint32_t value;
    RDropTestPair pair;
} RDropTestTree;

/* struct Forest { u32 value; array(Forest) children; }; */
typedef struct RDropTestArrayNode {
    uint32_t value;
    RRuntimeArray children;
} RDropTestArrayNode;

/* struct Owners { u32 value; array(own Owners*) children; }; */
typedef struct RDropTestOwnerArrayNode {
    uint32_t value;
    RRuntimeArray children;
} RDropTestOwnerArrayNode;

/* struct Chain { u32 value; list(Chain) children; }; */
typedef struct RDropTestListNode {
    uint32_t value;
    RRuntimeList children;
} RDropTestListNode;

/* struct Index { u32 value; dict(u32, Index) children; }; */
typedef struct RDropTestDictNode {
    uint32_t value;
    RRuntimeDict children;
} RDropTestDictNode;

static uint64_t r_drop_test_hook_count;
static uint64_t r_drop_test_key_count;
static uint32_t r_drop_test_first_value;
static uint32_t r_drop_test_last_value;
static _Bool r_drop_test_descending;

static void r_drop_test_reset(void) {
    r_drop_test_hook_count = 0U;
    r_drop_test_key_count = 0U;
    r_drop_test_first_value = 0U;
    r_drop_test_last_value = 0U;
    r_drop_test_descending = 1;
}

static void r_drop_test_observe(uint32_t value) {
    if (r_drop_test_hook_count == 0U) {
        r_drop_test_first_value = value;
    } else if (value + 1U != r_drop_test_last_value) {
        r_drop_test_descending = 0;
    }
    r_drop_test_last_value = value;
    r_drop_test_hook_count += 1U;
}

static _Bool r_drop_test_own_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_rc_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_arc_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_tree_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_pair_cursor(void *node, size_t index, RRuntimeDropMember *member);
static size_t r_drop_test_pair_count(void *node);
static _Bool r_drop_test_array_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_owner_array_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_owner_element_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_list_cursor(void *node, size_t index, RRuntimeDropMember *member);
static _Bool r_drop_test_dict_cursor(void *node, size_t index, RRuntimeDropMember *member);

static const RRuntimeDropNode r_drop_test_own_node = {
    r_drop_test_own_cursor, sizeof(RDropTestOwnNode), _Alignof(RDropTestOwnNode)};
static const RRuntimeDropNode r_drop_test_rc_node = {
    r_drop_test_rc_cursor, sizeof(RDropTestRcNode), _Alignof(RDropTestRcNode)};
static const RRuntimeDropNode r_drop_test_arc_node = {
    r_drop_test_arc_cursor, sizeof(RDropTestArcNode), _Alignof(RDropTestArcNode)};
static const RRuntimeDropNode r_drop_test_tree_node = {
    r_drop_test_tree_cursor, sizeof(RDropTestTree), _Alignof(RDropTestTree)};
static const RRuntimeDropNode r_drop_test_array_node = {
    r_drop_test_array_cursor, sizeof(RDropTestArrayNode), _Alignof(RDropTestArrayNode)};
static const RRuntimeDropNode r_drop_test_owner_array_node = {r_drop_test_owner_array_cursor,
                                                              sizeof(RDropTestOwnerArrayNode),
                                                              _Alignof(RDropTestOwnerArrayNode)};
/* The element type of array(own Owners*): one owner slot per element. */
static const RRuntimeDropNode r_drop_test_owner_element_node = {
    r_drop_test_owner_element_cursor, sizeof(RRuntimeOwn), _Alignof(RRuntimeOwn)};
static const RRuntimeDropNode r_drop_test_list_node = {
    r_drop_test_list_cursor, sizeof(RDropTestListNode), _Alignof(RDropTestListNode)};
static const RRuntimeDropNode r_drop_test_dict_node = {
    r_drop_test_dict_cursor, sizeof(RDropTestDictNode), _Alignof(RDropTestDictNode)};

static void r_drop_test_own_hook(void *value) {
    r_drop_test_observe(((RDropTestOwnNode *)value)->value);
}

static void r_drop_test_rc_hook(void *value) {
    r_drop_test_observe(((RDropTestRcNode *)value)->value);
}

static void r_drop_test_arc_hook(void *value) {
    r_drop_test_observe(((RDropTestArcNode *)value)->value);
}

static void r_drop_test_count_hook(void *value) {
    r_drop_test_hook_count += 1U;
    r_drop_test_last_value = *(const uint32_t *)value;
}

static void r_drop_test_key_drop(void *value) {
    (void)value;
    r_drop_test_key_count += 1U;
}

static uint64_t r_drop_test_key_hash(const void *value) {
    return (uint64_t)*(const uint32_t *)value * UINT64_C(0x9e3779b1);
}

static _Bool r_drop_test_key_equal(const void *left, const void *right) {
    return *(const uint32_t *)left == *(const uint32_t *)right;
}

static _Bool r_drop_test_hook_member(void *node, RRuntimeDropFn hook, RRuntimeDropMember *member) {
    member->kind = R_RUNTIME_DROP_MEMBER_LEAF;
    member->address = node;
    member->drop = hook;
    member->child = NULL;
    return 1;
}

static _Bool r_drop_test_storage_member(RRuntimeDropMemberKind kind,
                                        void *address,
                                        const RRuntimeDropNode *child,
                                        RRuntimeDropMember *member) {
    member->kind = kind;
    member->address = address;
    member->drop = NULL;
    member->child = child;
    return 1;
}

static _Bool r_drop_test_own_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestOwnNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_own_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_OWN, &value->next, &r_drop_test_own_node, member);
    }
    return 0;
}

static _Bool r_drop_test_rc_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestRcNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_rc_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_RC, &value->next, &r_drop_test_rc_node, member);
    }
    return 0;
}

static _Bool r_drop_test_arc_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestArcNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_arc_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_ARC, &value->next, &r_drop_test_arc_node, member);
    }
    return 0;
}

/* Members of Pair in reverse declaration order: right, then left. */
static _Bool r_drop_test_pair_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestPair *pair = node;

    if (index == 0U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_OWN, &pair->right, &r_drop_test_tree_node, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_OWN, &pair->left, &r_drop_test_tree_node, member);
    }
    return 0;
}

static size_t r_drop_test_pair_count(void *node) {
    (void)node;
    return 2U;
}

/* Tree delegates its by-value Pair member the way generated cursors do. */
static _Bool r_drop_test_tree_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestTree *tree = node;
    size_t count;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_count_hook, member);
    }
    index -= 1U;
    count = r_drop_test_pair_count(&tree->pair);
    if (index < count) {
        return r_drop_test_pair_cursor(&tree->pair, index, member);
    }
    return 0;
}

static _Bool r_drop_test_array_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestArrayNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_count_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_ARRAY, &value->children, &r_drop_test_array_node, member);
    }
    return 0;
}

static _Bool r_drop_test_owner_array_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestOwnerArrayNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_count_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_ARRAY, &value->children, &r_drop_test_owner_element_node, member);
    }
    return 0;
}

static _Bool
r_drop_test_owner_element_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    if (index == 0U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_OWN, node, &r_drop_test_owner_array_node, member);
    }
    return 0;
}

static _Bool r_drop_test_list_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestListNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_count_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_LIST, &value->children, &r_drop_test_list_node, member);
    }
    return 0;
}

static _Bool r_drop_test_dict_cursor(void *node, size_t index, RRuntimeDropMember *member) {
    RDropTestDictNode *value = node;

    if (index == 0U) {
        return r_drop_test_hook_member(node, r_drop_test_count_hook, member);
    }
    if (index == 1U) {
        return r_drop_test_storage_member(
            R_RUNTIME_DROP_MEMBER_DICT, &value->children, &r_drop_test_dict_node, member);
    }
    return 0;
}

static void r_drop_test_own_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_own_node);
}

static void r_drop_test_rc_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_rc_node);
}

static void r_drop_test_arc_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_arc_node);
}

static void r_drop_test_tree_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_tree_node);
}

static void r_drop_test_array_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_array_node);
}

static void r_drop_test_owner_array_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_owner_array_node);
}

static void r_drop_test_list_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_list_node);
}

static void r_drop_test_dict_drop(void *value) {
    r_runtime_drop_iterative(value, &r_drop_test_dict_node);
}

static int r_drop_test_failure(const char *message) {
    (void)fprintf(stderr, "runtime drop test failure: %s\n", message);
    return 1;
}

static int r_drop_test_own_chain(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestOwnNode), _Alignof(RDropTestOwnNode), NULL, r_drop_test_own_drop};
    RRuntimeOwn head = {0};
    uint32_t value;

    for (value = 0U; value < R_DROP_TEST_CHAIN_LENGTH; ++value) {
        RDropTestOwnNode node;
        RRuntimeOwn created;

        node.value = value;
        node.next = head;
        if (r_runtime_own_create(allocator, type, &node, &created) != R_RUNTIME_OWN_OK) {
            return r_drop_test_failure("own chain allocation failed");
        }
        head = created;
    }
    r_drop_test_reset();
    r_runtime_own_release(&head);
    if ((head.allocation != NULL) || (r_drop_test_hook_count != R_DROP_TEST_CHAIN_LENGTH) ||
        (r_drop_test_first_value != R_DROP_TEST_CHAIN_LENGTH - 1U) ||
        (r_drop_test_last_value != 0U) || !r_drop_test_descending) {
        return r_drop_test_failure("own chain hooks did not run once each from head to tail");
    }
    return 0;
}

static int r_drop_test_rc_chain(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestRcNode), _Alignof(RDropTestRcNode), NULL, r_drop_test_rc_drop};
    const uint32_t shared = R_DROP_TEST_CHAIN_LENGTH / 2U;
    RRuntimeRc head = {0};
    RRuntimeRc retained = {0};
    uint32_t value;

    for (value = 0U; value < R_DROP_TEST_CHAIN_LENGTH; ++value) {
        RDropTestRcNode node;
        RRuntimeRc created;

        node.value = value;
        node.next = head;
        if (r_runtime_rc_create(allocator, type, &node, &created) != R_RUNTIME_RC_OK) {
            return r_drop_test_failure("rc chain allocation failed");
        }
        head = created;
        if ((value == shared) && (r_runtime_rc_clone(&head, &retained) != R_RUNTIME_RC_OK)) {
            return r_drop_test_failure("rc clone failed");
        }
    }
    r_drop_test_reset();
    r_runtime_rc_release(&head);
    if ((head.control != NULL) ||
        (r_drop_test_hook_count != (uint64_t)(R_DROP_TEST_CHAIN_LENGTH - 1U - shared)) ||
        (r_drop_test_first_value != R_DROP_TEST_CHAIN_LENGTH - 1U) ||
        (r_drop_test_last_value != shared + 1U) || !r_drop_test_descending) {
        return r_drop_test_failure("rc chain destruction did not stop at the shared node");
    }
    r_drop_test_reset();
    r_runtime_rc_release(&retained);
    if ((retained.control != NULL) || (r_drop_test_hook_count != (uint64_t)shared + 1U) ||
        (r_drop_test_first_value != shared) || (r_drop_test_last_value != 0U) ||
        !r_drop_test_descending) {
        return r_drop_test_failure("rc tail destruction did not follow the last strong release");
    }
    return 0;
}

static int r_drop_test_arc_chain(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestArcNode), _Alignof(RDropTestArcNode), NULL, r_drop_test_arc_drop};
    RRuntimeArc head = {0};
    RRuntimeWeakArc weak = {0};
    uint32_t value;

    for (value = 0U; value < R_DROP_TEST_CHAIN_LENGTH; ++value) {
        RDropTestArcNode node;
        RRuntimeArc created;

        node.value = value;
        node.next = head;
        if (r_runtime_arc_create(allocator, type, &node, &created) != R_RUNTIME_ARC_OK) {
            return r_drop_test_failure("arc chain allocation failed");
        }
        head = created;
    }
    if (r_runtime_arc_downgrade(&head, &weak) != R_RUNTIME_ARC_OK) {
        return r_drop_test_failure("arc downgrade failed");
    }
    r_drop_test_reset();
    r_runtime_arc_release(&head);
    if ((head.control != NULL) || (r_drop_test_hook_count != R_DROP_TEST_CHAIN_LENGTH) ||
        (r_drop_test_first_value != R_DROP_TEST_CHAIN_LENGTH - 1U) ||
        (r_drop_test_last_value != 0U) || !r_drop_test_descending) {
        return r_drop_test_failure("arc chain hooks did not run once each from head to tail");
    }
    if (r_runtime_weak_arc_upgrade(&weak, &head) != R_RUNTIME_ARC_EXPIRED) {
        return r_drop_test_failure("weak arc survived the destruction of the last strong owner");
    }
    r_runtime_weak_arc_release(&weak);
    return 0;
}

static int r_drop_test_tree(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestTree), _Alignof(RDropTestTree), NULL, r_drop_test_tree_drop};
    RRuntimeOwn spine = {0};
    uint32_t value;

    /* A left spine whose every node also owns one right leaf. */
    for (value = 0U; value < R_DROP_TEST_SPINE_LENGTH; ++value) {
        RDropTestTree leaf;
        RDropTestTree node;
        RRuntimeOwn created_leaf;
        RRuntimeOwn created_node;

        (void)memset(&leaf, 0, sizeof(leaf));
        leaf.value = R_DROP_TEST_SPINE_LENGTH + value;
        if (r_runtime_own_create(allocator, type, &leaf, &created_leaf) != R_RUNTIME_OWN_OK) {
            return r_drop_test_failure("tree leaf allocation failed");
        }
        node.value = value;
        node.pair.left = spine;
        node.pair.right = created_leaf;
        if (r_runtime_own_create(allocator, type, &node, &created_node) != R_RUNTIME_OWN_OK) {
            return r_drop_test_failure("tree node allocation failed");
        }
        spine = created_node;
    }
    r_drop_test_reset();
    r_runtime_own_release(&spine);
    /* Hooks run top-down: each spine node, then its right leaf; the last one is leaf 0. */
    if ((spine.allocation != NULL) ||
        (r_drop_test_hook_count != (uint64_t)R_DROP_TEST_SPINE_LENGTH * 2U) ||
        (r_drop_test_last_value != R_DROP_TEST_SPINE_LENGTH)) {
        return r_drop_test_failure("tree destruction did not visit every node exactly once");
    }
    return 0;
}

/* A spine of nested arrays: children = [leaf, deeper]; the deeper child is destroyed first. */
static int r_drop_test_array_forest(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestArrayNode), _Alignof(RDropTestArrayNode), NULL, r_drop_test_array_drop};
    RDropTestArrayNode spine;
    uint32_t value;

    spine.value = 0U;
    r_runtime_array_initialize(&spine.children, allocator, type);
    for (value = 1U; value < R_DROP_TEST_SPINE_LENGTH; ++value) {
        RDropTestArrayNode leaf;
        RDropTestArrayNode node;

        leaf.value = R_DROP_TEST_SPINE_LENGTH + value;
        r_runtime_array_initialize(&leaf.children, allocator, type);
        node.value = value;
        r_runtime_array_initialize(&node.children, allocator, type);
        if ((r_runtime_array_push(&node.children, &leaf) != R_RUNTIME_ARRAY_OK) ||
            (r_runtime_array_push(&node.children, &spine) != R_RUNTIME_ARRAY_OK)) {
            return r_drop_test_failure("array forest push failed");
        }
        spine = node;
    }
    r_drop_test_reset();
    r_runtime_drop_iterative(&spine, &r_drop_test_array_node);
    /* Root, then index 1 (the deeper node) down to node 0, then every leaf on the way back. */
    if ((spine.children.data != NULL) || (spine.children.length != 0U) ||
        (r_drop_test_hook_count != (uint64_t)R_DROP_TEST_SPINE_LENGTH * 2U - 1U) ||
        (r_drop_test_last_value != R_DROP_TEST_SPINE_LENGTH * 2U - 1U)) {
        return r_drop_test_failure("array forest destruction did not visit every node once");
    }
    return 0;
}

/* A spine of arrays holding owner slots: each node owns the deeper node through an element. */
static int r_drop_test_owner_array_chain(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo node_type = {sizeof(RDropTestOwnerArrayNode),
                                        _Alignof(RDropTestOwnerArrayNode),
                                        NULL,
                                        r_drop_test_owner_array_drop};
    const RRuntimeTypeInfo slot_type = {sizeof(RRuntimeOwn), _Alignof(RRuntimeOwn), NULL, NULL};
    RRuntimeOwn deeper = {0};
    uint32_t value;

    for (value = 0U; value < R_DROP_TEST_SPINE_LENGTH; ++value) {
        RDropTestOwnerArrayNode node;
        RRuntimeOwn created;

        node.value = value;
        r_runtime_array_initialize(&node.children, allocator, slot_type);
        if ((deeper.allocation != NULL) &&
            (r_runtime_array_push(&node.children, &deeper) != R_RUNTIME_ARRAY_OK)) {
            return r_drop_test_failure("owner array push failed");
        }
        if (r_runtime_own_create(allocator, node_type, &node, &created) != R_RUNTIME_OWN_OK) {
            return r_drop_test_failure("owner array node allocation failed");
        }
        deeper = created;
    }
    r_drop_test_reset();
    r_runtime_own_release(&deeper);
    if ((deeper.allocation != NULL) || (r_drop_test_hook_count != R_DROP_TEST_SPINE_LENGTH) ||
        (r_drop_test_last_value != 0U)) {
        return r_drop_test_failure("owner array chain destruction did not reach the tail");
    }
    return 0;
}

static int r_drop_test_list_chain(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestListNode), _Alignof(RDropTestListNode), NULL, r_drop_test_list_drop};
    RDropTestListNode spine;
    uint32_t value;

    spine.value = 0U;
    if (r_runtime_list_initialize(&spine.children, allocator, type) != R_RUNTIME_LIST_OK) {
        return r_drop_test_failure("list initialize failed");
    }
    for (value = 1U; value < R_DROP_TEST_SPINE_LENGTH; ++value) {
        RDropTestListNode leaf;
        RDropTestListNode node;
        void *stored = NULL;

        leaf.value = R_DROP_TEST_SPINE_LENGTH + value;
        node.value = value;
        if ((r_runtime_list_initialize(&leaf.children, allocator, type) != R_RUNTIME_LIST_OK) ||
            (r_runtime_list_initialize(&node.children, allocator, type) != R_RUNTIME_LIST_OK) ||
            (r_runtime_list_push_back(&node.children, &leaf, &stored) != R_RUNTIME_LIST_OK) ||
            (r_runtime_list_push_back(&node.children, &spine, &stored) != R_RUNTIME_LIST_OK)) {
            return r_drop_test_failure("list chain push failed");
        }
        spine = node;
    }
    r_drop_test_reset();
    r_runtime_drop_iterative(&spine, &r_drop_test_list_node);
    if ((spine.children.first != NULL) || (spine.children.length != 0U) ||
        (r_drop_test_hook_count != (uint64_t)R_DROP_TEST_SPINE_LENGTH * 2U - 1U) ||
        (r_drop_test_last_value != R_DROP_TEST_SPINE_LENGTH * 2U - 1U)) {
        return r_drop_test_failure("list chain destruction did not visit every node once");
    }
    return 0;
}

static int r_drop_test_dict_chain(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo type = {
        sizeof(RDropTestDictNode), _Alignof(RDropTestDictNode), NULL, r_drop_test_dict_drop};
    const RRuntimeDictKeyInfo key = {
        {sizeof(uint32_t), _Alignof(uint32_t), NULL, r_drop_test_key_drop},
        r_drop_test_key_hash,
        r_drop_test_key_equal,
    };
    RDropTestDictNode spine;
    uint32_t value;

    spine.value = 0U;
    if (r_runtime_dict_initialize(&spine.children, allocator, key, type, UINT64_C(7)) !=
        R_RUNTIME_DICT_OK) {
        return r_drop_test_failure("dict initialize failed");
    }
    for (value = 1U; value < R_DROP_TEST_SPINE_LENGTH; ++value) {
        RDropTestDictNode leaf;
        RDropTestDictNode node;
        uint32_t leaf_key = 1U;
        uint32_t deeper_key = 2U;
        _Bool replaced = 0;

        leaf.value = R_DROP_TEST_SPINE_LENGTH + value;
        node.value = value;
        if ((r_runtime_dict_initialize(&leaf.children, allocator, key, type, UINT64_C(7)) !=
             R_RUNTIME_DICT_OK) ||
            (r_runtime_dict_initialize(&node.children, allocator, key, type, UINT64_C(7)) !=
             R_RUNTIME_DICT_OK) ||
            (r_runtime_dict_insert(&node.children, &leaf_key, &leaf, NULL, &replaced) !=
             R_RUNTIME_DICT_OK) ||
            (r_runtime_dict_insert(&node.children, &deeper_key, &spine, NULL, &replaced) !=
             R_RUNTIME_DICT_OK)) {
            return r_drop_test_failure("dict chain insert failed");
        }
        spine = node;
    }
    r_drop_test_reset();
    r_runtime_drop_iterative(&spine, &r_drop_test_dict_node);
    if ((spine.children.slots != NULL) || (spine.children.length != 0U) ||
        (r_drop_test_hook_count != (uint64_t)R_DROP_TEST_SPINE_LENGTH * 2U - 1U) ||
        (r_drop_test_key_count != (uint64_t)(R_DROP_TEST_SPINE_LENGTH - 1U) * 2U) ||
        (r_drop_test_last_value != R_DROP_TEST_SPINE_LENGTH * 2U - 1U)) {
        return r_drop_test_failure("dict chain destruction did not visit every entry once");
    }
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    int status;

    r_runtime_allocator_initialize(&allocator);
    status = r_drop_test_own_chain(&allocator);
    if (status == 0) {
        status = r_drop_test_rc_chain(&allocator);
    }
    if (status == 0) {
        status = r_drop_test_arc_chain(&allocator);
    }
    if (status == 0) {
        status = r_drop_test_tree(&allocator);
    }
    if (status == 0) {
        status = r_drop_test_array_forest(&allocator);
    }
    if (status == 0) {
        status = r_drop_test_owner_array_chain(&allocator);
    }
    if (status == 0) {
        status = r_drop_test_list_chain(&allocator);
    }
    if (status == 0) {
        status = r_drop_test_dict_chain(&allocator);
    }
    if (status == 0) {
        (void)printf("runtime_iterative_drop\n");
    }
    return status;
}
