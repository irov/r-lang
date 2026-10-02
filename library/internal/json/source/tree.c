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

typedef struct RJsonBuildFrame {
    struct RJsonNode *node;
    RStdString key;
    bool has_key;
} RJsonBuildFrame;
static void r_json_build_frame_drop(void *value) {
    RJsonBuildFrame *frame = value;
    r_runtime_string_destroy(&frame->key);
}
void r_json_tree_builder_initialize(RStdJsonTreeBuilder *builder, RRuntimeAllocator *allocator) {
    *builder = (RStdJsonTreeBuilder){0};
    r_runtime_array_initialize(
        &builder->frames,
        allocator,
        (RRuntimeTypeInfo){
            sizeof(RJsonBuildFrame), alignof(RJsonBuildFrame), NULL, r_json_build_frame_drop});
}
void r_json_tree_builder_destroy(RStdJsonTreeBuilder *builder) {
    r_runtime_array_destroy(&builder->frames);
    r_json_value_destroy(&builder->value);
    *builder = (RStdJsonTreeBuilder){0};
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
    if (!builder->started && quoted) {
        if (!r_json_cursor_expect(cursor, R_STD_JSON_TOKEN_STRING) ||
            !r_json_number_valid(cursor->token.text))
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        created = r_json_new_node(cursor->allocator, R_STD_JSON_NUMBER);
    } else if (token == R_STD_JSON_TOKEN_KEY) {
        if (parent == NULL || parent->node->kind != R_STD_JSON_OBJECT || parent->has_key)
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        parent->key = r_json_scanner_take_text(&cursor->scanner);
        parent->has_key = true;
        return true;
    } else if (token == R_STD_JSON_TOKEN_ARRAY_END || token == R_STD_JSON_TOKEN_OBJECT_END) {
        if (parent == NULL || parent->has_key ||
            parent->node->kind !=
                (token == R_STD_JSON_TOKEN_ARRAY_END ? R_STD_JSON_ARRAY : R_STD_JSON_OBJECT))
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        r_json_build_frame_drop(parent);
        --builder->frames.length;
        builder->complete = builder->frames.length == 0U;
        return true;
    } else {
        switch (token) {
        case R_STD_JSON_TOKEN_OBJECT_BEGIN:
            created = r_json_new_node(cursor->allocator, R_STD_JSON_OBJECT);
            break;
        case R_STD_JSON_TOKEN_ARRAY_BEGIN:
            created = r_json_new_node(cursor->allocator, R_STD_JSON_ARRAY);
            break;
        case R_STD_JSON_TOKEN_STRING:
            created = r_json_new_node(cursor->allocator, R_STD_JSON_STRING);
            break;
        case R_STD_JSON_TOKEN_NUMBER:
            created = r_json_new_node(cursor->allocator, R_STD_JSON_NUMBER);
            break;
        case R_STD_JSON_TOKEN_TRUE:
        case R_STD_JSON_TOKEN_FALSE:
            created = r_json_new_node(cursor->allocator, R_STD_JSON_BOOLEAN);
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
    struct RJsonNode *node = created.value.node;
    if (node != NULL && (node->kind == R_STD_JSON_STRING || node->kind == R_STD_JSON_NUMBER))
        node->as.text = r_json_scanner_take_text(&cursor->scanner);
    else if (node != NULL && node->kind == R_STD_JSON_BOOLEAN)
        node->as.boolean = token == R_STD_JSON_TOKEN_TRUE;
    if (parent == NULL) {
        builder->value = created.value;
        builder->started = true;
    } else if (parent->node->kind == R_STD_JSON_ARRAY) {
        RStdJsonValue target = {parent->node};
        cursor->outcome = r_json_append(&target, &created.value);
    } else if (!parent->has_key) {
        r_json_value_destroy(&created.value);
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    } else {
        RJsonMember member = {parent->key, created.value};
        cursor->outcome =
            r_json_array_result(r_runtime_array_push(&parent->node->as.members, &member));
        if (cursor->outcome.status == R_STD_JSON_CALL_SUCCESS) {
            parent->key = (RStdString){0};
            parent->has_key = false;
            created.value = (RStdJsonValue){0};
        }
    }
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS) {
        r_json_value_destroy(&created.value);
        return false;
    }
    if (token == R_STD_JSON_TOKEN_OBJECT_BEGIN || token == R_STD_JSON_TOKEN_ARRAY_BEGIN) {
        RJsonBuildFrame frame = {.node = node};
        cursor->outcome = r_json_array_result(r_runtime_array_push(&builder->frames, &frame));
        if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
            return false;
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
