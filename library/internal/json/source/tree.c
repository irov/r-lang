#include "r_library_json_internal.h"

#include <stdalign.h>
#include <string.h>

RStdJsonValueResult r_json_new_node(RRuntimeAllocator *allocator, RStdJsonKind kind) {
    RStdJsonValueResult result = {0};
    void *storage = NULL;
    if (kind == R_STD_JSON_NULL)
        return result;
    result.outcome = r_json_allocation_result(r_runtime_allocator_allocate(
        allocator, sizeof(struct RJsonNode), alignof(struct RJsonNode), &storage));
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    result.value.node = storage;
    *result.value.node = (struct RJsonNode){0};
    result.value.node->kind = kind;
    result.value.node->allocator = allocator;
    if (kind == R_STD_JSON_ARRAY)
        r_runtime_array_initialize(
            &result.value.node->as.elements,
            allocator,
            (RRuntimeTypeInfo){sizeof(RStdJsonValue), alignof(RStdJsonValue), NULL, NULL});
    else if (kind == R_STD_JSON_OBJECT)
        r_runtime_array_initialize(
            &result.value.node->as.members,
            allocator,
            (RRuntimeTypeInfo){sizeof(RJsonMember), alignof(RJsonMember), NULL, NULL});
    else if (kind == R_STD_JSON_STRING || kind == R_STD_JSON_NUMBER)
        (void)r_runtime_string_initialize(&result.value.node->as.text, allocator);
    return result;
}
/* Destruction is iterative, including trees assembled by callers deeper than scanner limits. */
void r_json_value_destroy(RStdJsonValue *value) {
    struct RJsonNode *pending = value->node;
    value->node = NULL;
    if (pending != NULL)
        pending->drop_next = NULL;
    while (pending != NULL) {
        struct RJsonNode *node = pending;
        pending = node->drop_next;
        if (node->kind == R_STD_JSON_ARRAY) {
            RStdJsonValue *elements = node->as.elements.data;
            for (size_t i = 0U; i < node->as.elements.length; ++i) {
                if (elements[i].node == NULL)
                    continue;
                elements[i].node->drop_next = pending;
                pending = elements[i].node;
            }
            r_runtime_array_destroy(&node->as.elements);
        } else if (node->kind == R_STD_JSON_OBJECT) {
            RJsonMember *members = node->as.members.data;
            for (size_t i = 0U; i < node->as.members.length; ++i) {
                r_runtime_string_destroy(&members[i].key);
                if (members[i].value.node == NULL)
                    continue;
                members[i].value.node->drop_next = pending;
                pending = members[i].value.node;
            }
            r_runtime_array_destroy(&node->as.members);
        } else if (node->kind == R_STD_JSON_NUMBER || node->kind == R_STD_JSON_STRING) {
            r_runtime_string_destroy(&node->as.text);
        }
        r_runtime_allocator_deallocate(node, alignof(struct RJsonNode));
    }
}
RStdJsonResult r_json_append(RStdJsonValue *target, RStdJsonValue *value) {
    RStdJsonResult result;
    if (target == value || target->node == NULL || target->node->kind != R_STD_JSON_ARRAY)
        return r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
    result = r_json_array_result(r_runtime_array_push(&target->node->as.elements, value));
    if (result.status == R_STD_JSON_CALL_SUCCESS)
        value->node = NULL;
    return result;
}
RStdJsonResult r_json_insert(RStdJsonValue *target, RStdJsonByteView key, RStdJsonValue *value) {
    RStdJsonResult result;
    RJsonMember member = {0};
    if (target == value || target->node == NULL || target->node->kind != R_STD_JSON_OBJECT)
        return r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
    for (size_t i = 0U; i < target->node->as.members.length; ++i) {
        RJsonMember *existing = &((RJsonMember *)target->node->as.members.data)[i];
        if (!r_json_view_equal(r_json_string_view(&existing->key), key))
            continue;
        if (&existing->value == value)
            return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
        r_json_value_destroy(&existing->value);
        existing->value = *value;
        value->node = NULL;
        return (RStdJsonResult){0};
    }
    result = r_json_copy_text(&member.key, target->node->allocator, key);
    if (result.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    member.value = *value;
    result = r_json_array_result(r_runtime_array_push(&target->node->as.members, &member));
    if (result.status == R_STD_JSON_CALL_SUCCESS)
        value->node = NULL;
    else
        r_runtime_string_destroy(&member.key);
    return result;
}
RStdJsonValueResult r_json_take_index(RStdJsonValue *target, size_t index) {
    RStdJsonValueResult result = {0};
    if (target->node == NULL || target->node->kind != R_STD_JSON_ARRAY) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
        return result;
    }
    if (!r_runtime_array_remove(&target->node->as.elements, index, &result.value))
        result.outcome = r_json_failure(R_STD_JSON_ERROR_RANGE, 0U);
    return result;
}
RStdJsonValueResult r_json_take_field(RStdJsonValue *target, RStdJsonByteView key) {
    RStdJsonValueResult result = {0};
    if (target->node == NULL || target->node->kind != R_STD_JSON_OBJECT) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
        return result;
    }
    for (size_t i = 0U; i < target->node->as.members.length; ++i) {
        RJsonMember *existing = &((RJsonMember *)target->node->as.members.data)[i];
        if (r_json_view_equal(r_json_string_view(&existing->key), key)) {
            RJsonMember removed = {0};
            (void)r_runtime_array_remove(&target->node->as.members, i, &removed);
            result.value = removed.value;
            r_runtime_string_destroy(&removed.key);
            return result;
        }
    }
    result.outcome = r_json_failure(R_STD_JSON_ERROR_MISSING_FIELD, 0U);
    return result;
}
RStdJsonNumberResult r_json_number(RRuntimeAllocator *allocator, RStdJsonByteView source) {
    RStdJsonNumberResult result = {0};
    if (!r_json_number_valid(source)) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_SYNTAX, 0U);
        return result;
    }
    result.outcome = r_json_copy_text(&result.value.text, allocator, source);
    return result;
}
bool r_json_number_is_zero(RStdJsonByteView source) {
    for (size_t i = 0U; i < source.length && source.data[i] != 'e' && source.data[i] != 'E'; ++i)
        if (source.data[i] >= '1' && source.data[i] <= '9')
            return false;
    return true;
}

/* A parsed string or number node and a parsed object node carry their text or key bytes after
 * the node in its own allocation. The strings that refer to those bytes have no allocator, so
 * destroying them leaves the bytes alone, and the bytes end with the node; the module hands
 * them out only as views of the value. */
static uint8_t *r_json_node_bytes(struct RJsonNode *node) {
    return (uint8_t *)(node + 1);
}
static RStdString r_json_node_text(uint8_t *bytes, size_t length) {
    RStdString text = {0};
    text.bytes.element = (RRuntimeTypeInfo){sizeof(uint8_t), alignof(uint8_t), NULL, NULL};
    text.bytes.data = bytes;
    text.bytes.length = length;
    text.bytes.capacity = length;
    return text;
}
static RStdJsonValueResult
r_json_new_node_with_bytes(RRuntimeAllocator *allocator, RStdJsonKind kind, size_t extra) {
    RStdJsonValueResult result = {0};
    void *storage = NULL;
    if (extra > SIZE_MAX - sizeof(struct RJsonNode)) {
        result.outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_SIZE_OVERFLOW);
        return result;
    }
    result.outcome = r_json_allocation_result(r_runtime_allocator_allocate(
        allocator, sizeof(struct RJsonNode) + extra, alignof(struct RJsonNode), &storage));
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    result.value.node = storage;
    *result.value.node = (struct RJsonNode){0};
    result.value.node->kind = kind;
    result.value.node->allocator = allocator;
    if (kind == R_STD_JSON_OBJECT)
        r_runtime_array_initialize(
            &result.value.node->as.members,
            allocator,
            (RRuntimeTypeInfo){sizeof(RJsonMember), alignof(RJsonMember), NULL, NULL});
    return result;
}
static RStdJsonValueResult
r_json_text_node(RRuntimeAllocator *allocator, RStdJsonKind kind, RStdJsonByteView text) {
    RStdJsonValueResult result = r_json_new_node_with_bytes(allocator, kind, text.length);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    if (text.length != 0U)
        memcpy(r_json_node_bytes(result.value.node), text.data, text.length);
    result.value.node->as.text =
        r_json_node_text(r_json_node_bytes(result.value.node), text.length);
    return result;
}

/* A completed member or element of a container that is still open; key_* locate an object
 * member's key in the builder's key bytes. A child container's value stays null until it closes. */
typedef struct RJsonPendingChild {
    size_t key_offset;
    size_t key_length;
    RStdJsonValue value;
} RJsonPendingChild;
typedef struct RJsonBuildFrame {
    RStdJsonKind kind;
    size_t slot;
    size_t children_first;
    size_t key_bytes_first;
    size_t key_offset;
    size_t key_length;
    bool has_key;
} RJsonBuildFrame;
static void r_json_pending_child_drop(void *value) {
    RJsonPendingChild *child = value;
    r_json_value_destroy(&child->value);
}
void r_json_tree_builder_initialize(RStdJsonTreeBuilder *builder, RRuntimeAllocator *allocator) {
    *builder = (RStdJsonTreeBuilder){0};
    r_runtime_array_initialize(
        &builder->frames,
        allocator,
        (RRuntimeTypeInfo){sizeof(RJsonBuildFrame), alignof(RJsonBuildFrame), NULL, NULL});
    r_runtime_array_initialize(&builder->children,
                               allocator,
                               (RRuntimeTypeInfo){sizeof(RJsonPendingChild),
                                                  alignof(RJsonPendingChild),
                                                  NULL,
                                                  r_json_pending_child_drop});
    r_runtime_array_initialize(&builder->key_bytes,
                               allocator,
                               (RRuntimeTypeInfo){sizeof(uint8_t), alignof(uint8_t), NULL, NULL});
}
void r_json_tree_builder_destroy(RStdJsonTreeBuilder *builder) {
    r_runtime_array_destroy(&builder->frames);
    r_runtime_array_destroy(&builder->children);
    r_runtime_array_destroy(&builder->key_bytes);
    r_json_value_destroy(&builder->value);
    *builder = (RStdJsonTreeBuilder){0};
}
/* Creates the closing container's node with its children sized exactly and, for an object, its
 * keys in the node's allocation; on failure the children stay pending and are destroyed with
 * the builder. */
static RStdJsonResult r_json_tree_builder_close(RStdJsonTreeBuilder *builder,
                                                const RJsonBuildFrame *frame,
                                                RRuntimeAllocator *allocator) {
    const size_t first = frame->children_first;
    const size_t count = builder->children.length - first;
    const RJsonPendingChild *pending = (const RJsonPendingChild *)builder->children.data + first;
    const size_t key_bytes = builder->key_bytes.length - frame->key_bytes_first;
    RStdJsonValueResult created;
    RStdJsonResult result = {0};
    struct RJsonNode *node;
    if (frame->kind == R_STD_JSON_ARRAY) {
        created = r_json_new_node(allocator, R_STD_JSON_ARRAY);
        if (created.outcome.status != R_STD_JSON_CALL_SUCCESS)
            return created.outcome;
        node = created.value.node;
        if (count != 0U) {
            RStdJsonValue *elements;
            result = r_json_array_result(r_runtime_array_with_capacity(
                &node->as.elements,
                allocator,
                (RRuntimeTypeInfo){sizeof(RStdJsonValue), alignof(RStdJsonValue), NULL, NULL},
                count));
            if (result.status != R_STD_JSON_CALL_SUCCESS) {
                r_json_value_destroy(&created.value);
                return result;
            }
            elements = node->as.elements.data;
            for (size_t i = 0U; i < count; ++i)
                elements[i] = pending[i].value;
            node->as.elements.length = count;
        }
    } else {
        created = r_json_new_node_with_bytes(allocator, R_STD_JSON_OBJECT, key_bytes);
        if (created.outcome.status != R_STD_JSON_CALL_SUCCESS)
            return created.outcome;
        node = created.value.node;
        if (key_bytes != 0U)
            memcpy(r_json_node_bytes(node),
                   (const uint8_t *)builder->key_bytes.data + frame->key_bytes_first,
                   key_bytes);
        if (count != 0U) {
            RJsonMember *members;
            result = r_json_array_result(r_runtime_array_with_capacity(
                &node->as.members,
                allocator,
                (RRuntimeTypeInfo){sizeof(RJsonMember), alignof(RJsonMember), NULL, NULL},
                count));
            if (result.status != R_STD_JSON_CALL_SUCCESS) {
                r_json_value_destroy(&created.value);
                return result;
            }
            members = node->as.members.data;
            for (size_t i = 0U; i < count; ++i) {
                members[i].key = r_json_node_text(
                    r_json_node_bytes(node) + (pending[i].key_offset - frame->key_bytes_first),
                    pending[i].key_length);
                members[i].value = pending[i].value;
            }
            node->as.members.length = count;
        }
    }
    builder->children.length = first;
    builder->key_bytes.length = frame->key_bytes_first;
    if (frame->slot == SIZE_MAX)
        builder->value = created.value;
    else
        ((RJsonPendingChild *)builder->children.data)[frame->slot].value = created.value;
    return result;
}
static bool r_json_tree_builder_key(RStdJsonTreeBuilder *builder,
                                    RJsonBuildFrame *frame,
                                    RStdJsonCursor *cursor) {
    const RStdJsonByteView text = cursor->token.text;
    if (builder->key_bytes.capacity - builder->key_bytes.length < text.length) {
        cursor->outcome = r_json_array_result(
            r_runtime_array_reserve(&builder->key_bytes, text.length < 256U ? 256U : text.length));
        if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
            return false;
    }
    if (text.length != 0U)
        memcpy(
            (uint8_t *)builder->key_bytes.data + builder->key_bytes.length, text.data, text.length);
    frame->key_offset = builder->key_bytes.length;
    frame->key_length = text.length;
    frame->has_key = true;
    builder->key_bytes.length += text.length;
    return true;
}
bool r_json_tree_builder_token(RStdJsonTreeBuilder *builder, RStdJsonCursor *cursor, bool quoted) {
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    if (builder->complete || !cursor->has_token)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_INVALID_STATE);
    RJsonBuildFrame *parent =
        builder->frames.length == 0U
            ? NULL
            : r_runtime_array_get_mut(&builder->frames, builder->frames.length - 1U);
    RStdJsonTokenKind token = cursor->token.kind;
    RStdJsonValueResult created = {0};
    bool container = false;
    size_t slot = SIZE_MAX;
    if (!builder->started && quoted) {
        if (!r_json_cursor_expect(cursor, R_STD_JSON_TOKEN_STRING) ||
            !r_json_number_valid(cursor->token.text))
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        created = r_json_text_node(cursor->allocator, R_STD_JSON_NUMBER, cursor->token.text);
    } else if (token == R_STD_JSON_TOKEN_KEY) {
        if (parent == NULL || parent->kind != R_STD_JSON_OBJECT || parent->has_key)
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        return r_json_tree_builder_key(builder, parent, cursor);
    } else if (token == R_STD_JSON_TOKEN_ARRAY_END || token == R_STD_JSON_TOKEN_OBJECT_END) {
        if (parent == NULL || parent->has_key ||
            parent->kind !=
                (token == R_STD_JSON_TOKEN_ARRAY_END ? R_STD_JSON_ARRAY : R_STD_JSON_OBJECT))
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        cursor->outcome = r_json_tree_builder_close(builder, parent, cursor->allocator);
        if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
            return false;
        --builder->frames.length;
        builder->complete = builder->frames.length == 0U;
        return true;
    } else {
        switch (token) {
        case R_STD_JSON_TOKEN_OBJECT_BEGIN:
        case R_STD_JSON_TOKEN_ARRAY_BEGIN:
            container = true;
            break;
        case R_STD_JSON_TOKEN_STRING:
            created = r_json_text_node(cursor->allocator, R_STD_JSON_STRING, cursor->token.text);
            break;
        case R_STD_JSON_TOKEN_NUMBER:
            created = r_json_text_node(cursor->allocator, R_STD_JSON_NUMBER, cursor->token.text);
            break;
        case R_STD_JSON_TOKEN_TRUE:
        case R_STD_JSON_TOKEN_FALSE:
            created = r_json_new_node(cursor->allocator, R_STD_JSON_BOOLEAN);
            if (created.outcome.status == R_STD_JSON_CALL_SUCCESS)
                created.value.node->as.boolean = token == R_STD_JSON_TOKEN_TRUE;
            break;
        case R_STD_JSON_TOKEN_NULL:
            break;
        default:
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        }
    }
    cursor->outcome = created.outcome;
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    if (parent == NULL) {
        builder->value = created.value;
        builder->started = true;
    } else if (parent->kind == R_STD_JSON_OBJECT && !parent->has_key) {
        r_json_value_destroy(&created.value);
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    } else {
        RJsonPendingChild child = {parent->key_offset, parent->key_length, created.value};
        if (builder->children.length == builder->children.capacity) {
            cursor->outcome = r_json_array_result(r_runtime_array_reserve(&builder->children, 32U));
            if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS) {
                r_json_value_destroy(&created.value);
                return false;
            }
        }
        ((RJsonPendingChild *)builder->children.data)[builder->children.length] = child;
        slot = builder->children.length;
        ++builder->children.length;
        parent->has_key = false;
    }
    if (container) {
        RJsonBuildFrame frame = {
            .kind = token == R_STD_JSON_TOKEN_OBJECT_BEGIN ? R_STD_JSON_OBJECT : R_STD_JSON_ARRAY,
            .slot = slot,
            .children_first = builder->children.length,
            .key_bytes_first = builder->key_bytes.length,
        };
        if (builder->frames.length == builder->frames.capacity) {
            cursor->outcome = r_json_array_result(r_runtime_array_reserve(&builder->frames, 8U));
            if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
                return false;
        }
        ((RJsonBuildFrame *)builder->frames.data)[builder->frames.length] = frame;
        ++builder->frames.length;
    }
    builder->complete = builder->frames.length == 0U;
    return true;
}
bool r_json_cursor_value(RStdJsonCursor *cursor, RStdJsonValue *value, bool quoted) {
    RStdJsonTreeBuilder builder;
    *value = (RStdJsonValue){0};
    r_json_tree_builder_initialize(&builder, cursor->allocator);
    while (cursor->outcome.status == R_STD_JSON_CALL_SUCCESS && !builder.complete) {
        if (!cursor->has_token) {
            (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_UNEXPECTED_EOF);
            break;
        }
        if (!r_json_tree_builder_token(&builder, cursor, quoted) || !r_json_cursor_next(cursor))
            break;
    }
    bool success = cursor->outcome.status == R_STD_JSON_CALL_SUCCESS;
    if (success) {
        *value = builder.value;
        builder.value = (RStdJsonValue){0};
    }
    r_json_tree_builder_destroy(&builder);
    return success;
}
RStdJsonValueResult
r_json_parse(RRuntimeAllocator *allocator, RStdJsonByteView source, RStdJsonOptions options) {
    RStdJsonValueResult result = {0};
    RStdJsonCursor cursor;
    if (options.mode != R_STD_JSON_MODE_DOCUMENT) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
        return result;
    }
    (void)r_json_cursor_initialize(&cursor, allocator, source, options);
    (void)r_json_cursor_value(&cursor, &result.value, false);
    result.outcome = r_json_cursor_finish(&cursor);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        r_json_value_destroy(&result.value);
    return result;
}

typedef struct RJsonCloneFrame {
    const struct RJsonNode *source;
    struct RJsonNode *target;
    size_t next;
} RJsonCloneFrame;
static RStdJsonValueResult r_json_clone_shallow(RRuntimeAllocator *allocator,
                                                const RStdJsonValue *value) {
    RStdJsonValueResult result =
        r_json_new_node(allocator, value->node == NULL ? R_STD_JSON_NULL : value->node->kind);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS || value->node == NULL)
        return result;
    if (value->node->kind == R_STD_JSON_BOOLEAN)
        result.value.node->as.boolean = value->node->as.boolean;
    else if (value->node->kind == R_STD_JSON_NUMBER || value->node->kind == R_STD_JSON_STRING) {
        result.outcome = r_json_copy_text(
            &result.value.node->as.text, allocator, r_json_string_view(&value->node->as.text));
        if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
            r_json_value_destroy(&result.value);
    }
    return result;
}
RStdJsonValueResult r_json_clone_value(RRuntimeAllocator *allocator, const RStdJsonValue *value) {
    RStdJsonValueResult result = r_json_clone_shallow(allocator, value);
    RRuntimeArray frames;
    r_runtime_array_initialize(
        &frames,
        allocator,
        (RRuntimeTypeInfo){sizeof(RJsonCloneFrame), alignof(RJsonCloneFrame), NULL, NULL});
    if (result.outcome.status == R_STD_JSON_CALL_SUCCESS && value->node != NULL &&
        (value->node->kind == R_STD_JSON_OBJECT || value->node->kind == R_STD_JSON_ARRAY)) {
        RJsonCloneFrame frame = {value->node, result.value.node, 0U};
        result.outcome = r_json_array_result(r_runtime_array_push(&frames, &frame));
    }
    while (result.outcome.status == R_STD_JSON_CALL_SUCCESS && frames.length != 0U) {
        RJsonCloneFrame *frame = &((RJsonCloneFrame *)frames.data)[frames.length - 1U];
        bool object = frame->source->kind == R_STD_JSON_OBJECT;
        size_t count =
            object ? frame->source->as.members.length : frame->source->as.elements.length;
        if (frame->next == count) {
            --frames.length;
            continue;
        }
        const RJsonMember *member =
            object ? &((const RJsonMember *)frame->source->as.members.data)[frame->next] : NULL;
        const RStdJsonValue *child =
            object ? &member->value
                   : &((const RStdJsonValue *)frame->source->as.elements.data)[frame->next];
        ++frame->next;
        RStdJsonValueResult copied = r_json_clone_shallow(allocator, child);
        result.outcome = copied.outcome;
        if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
            break;
        RJsonCloneFrame nested = {child->node, copied.value.node, 0U};
        RStdJsonValue target = {frame->target};
        result.outcome =
            object ? r_json_insert(&target, r_json_string_view(&member->key), &copied.value)
                   : r_json_append(&target, &copied.value);
        r_json_value_destroy(&copied.value);
        if (result.outcome.status == R_STD_JSON_CALL_SUCCESS && nested.source != NULL &&
            (nested.source->kind == R_STD_JSON_OBJECT || nested.source->kind == R_STD_JSON_ARRAY))
            result.outcome = r_json_array_result(r_runtime_array_push(&frames, &nested));
    }
    r_runtime_array_destroy(&frames);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        r_json_value_destroy(&result.value);
    return result;
}

/* Both objects are temporary encoder owners; partial extension is destroyed on failure. */
RStdJsonResult r_json_object_extend(RStdJsonValue *target, RStdJsonValue *source) {
    if (target == source || target->node == NULL || source->node == NULL ||
        target->node->kind != R_STD_JSON_OBJECT || source->node->kind != R_STD_JSON_OBJECT)
        return r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
    for (size_t i = 0U; i < source->node->as.members.length; ++i) {
        RJsonMember *member = r_runtime_array_get_mut(&source->node->as.members, i);
        RStdJsonByteView key = r_json_string_view(&member->key);
        for (size_t j = 0U; j < target->node->as.members.length; ++j) {
            const RJsonMember *existing = r_runtime_array_get(&target->node->as.members, j);
            if (r_json_view_equal(key, r_json_string_view(&existing->key)))
                return r_json_failure(R_STD_JSON_ERROR_KEY_CONFLICT, 0U);
        }
        RStdJsonResult result = r_json_insert(target, key, &member->value);
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
    }
    return (RStdJsonResult){0};
}
