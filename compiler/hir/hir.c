#include "frontend_internal.h"
#include "standard_async_sync.h"
#include "standard_fs_async.h"

const char *r_hir_kind_name(RHirKind kind) {
    switch (kind) {
    case R_HIR_PROGRAM:
        return "program";
    case R_HIR_MODULE:
        return "module";
    case R_HIR_FUNCTION:
        return "function";
    case R_HIR_PARAMETER:
        return "parameter";
    case R_HIR_BLOCK:
        return "block";
    case R_HIR_LOCAL:
        return "local";
    case R_HIR_RETURN:
        return "return";
    case R_HIR_GENERIC_CONSTANT:
        return "generic_constant";
    case R_HIR_TYPE_QUERY:
        return "type_query";
    case R_HIR_LITERAL:
        return "literal";
    case R_HIR_STRING_LITERAL:
        return "string_literal";
    case R_HIR_VALUE_SCOPE:
        return "value_scope";
    case R_HIR_ENUM_CONSTANT:
        return "enum_constant";
    case R_HIR_DEFAULT_VALUE:
        return "default_value";
    case R_HIR_NEW:
        return "new";
    case R_HIR_FIELD_INIT:
        return "field_init";
    case R_HIR_AGGREGATE_INIT:
        return "aggregate_init";
    case R_HIR_ARRAY_INIT:
        return "array_init";
    case R_HIR_VARIANT:
        return "variant";
    case R_HIR_VARIANT_TAG:
        return "variant_tag";
    case R_HIR_VARIANT_PAYLOAD:
        return "variant_payload";
    case R_HIR_PLACE:
        return "place";
    case R_HIR_FIELD_PLACE:
        return "field_place";
    case R_HIR_INDEX_PLACE:
        return "index_place";
    case R_HIR_DEREF_PLACE:
        return "deref_place";
    case R_HIR_BORROW:
        return "borrow";
    case R_HIR_SLICE:
        return "slice";
    case R_HIR_LENGTH:
        return "length";
    case R_HIR_LOAD:
        return "load";
    case R_HIR_MOVE:
        return "move";
    case R_HIR_DROP:
        return "drop";
    case R_HIR_UNARY:
        return "unary";
    case R_HIR_CAST:
        return "cast";
    case R_HIR_DISCARD:
        return "discard";
    case R_HIR_BINARY:
        return "binary";
    case R_HIR_CONDITIONAL:
        return "conditional";
    case R_HIR_FUNCTION_ADDRESS:
        return "function_address";
    case R_HIR_INDIRECT_CALL:
        return "indirect_call";
    case R_HIR_CALL:
        return "call";
    case R_HIR_ASYNC_START:
        return "async_start";
    case R_HIR_AWAIT:
        return "await";
    case R_HIR_TASK_SCOPE_ENTER:
        return "task_scope_enter";
    case R_HIR_TASK_SCOPE_WAIT:
        return "task_scope_wait";
    case R_HIR_TASK_SCOPE_CLOSE:
        return "task_scope_close";
    case R_HIR_STANDARD_CALL:
        return "standard_call";
    case R_HIR_ASSIGN:
        return "assign";
    case R_HIR_COMPOUND_ASSIGN:
        return "compound_assign";
    case R_HIR_EXPRESSION_STATEMENT:
        return "expression_statement";
    case R_HIR_IF:
        return "if";
    case R_HIR_STATIC_IF:
        return "static_if";
    case R_HIR_WHILE:
        return "while";
    case R_HIR_FOR:
        return "for";
    case R_HIR_BREAK:
        return "break";
    case R_HIR_CONTINUE:
        return "continue";
    case R_HIR_TRY:
        return "try";
    case R_HIR_CATCH:
        return "catch";
    case R_HIR_FINALLY:
        return "finally";
    case R_HIR_THROW:
        return "throw";
    case R_HIR_SWITCH:
        return "switch";
    case R_HIR_CASE:
        return "case";
    case R_HIR_PACK_PARTITION:
        return "pack_partition";
    case R_HIR_PACK_ELEMENT:
        return "pack_element";
    case R_HIR_PACK_ARGS:
        return "pack_args";
    case R_HIR_INVALID:
    default:
        return "invalid";
    }
}

static bool r_hir_same_tree_at(const RFrontendContext *context,
                               RHirNodeId left_id,
                               RHirNodeId right_id,
                               uint32_t depth) {
    const RHirNode *left;
    const RHirNode *right;
    uint32_t child_index;

    if (left_id == right_id) {
        return true;
    }
    if ((left_id == R_HIR_NODE_ID_INVALID) || (right_id == R_HIR_NODE_ID_INVALID) ||
        ((size_t)left_id > context->hir_node_count) ||
        ((size_t)right_id > context->hir_node_count) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return false;
    }
    left = &context->hir_nodes[(size_t)left_id - 1U];
    right = &context->hir_nodes[(size_t)right_id - 1U];
    if ((left->kind != right->kind) || (left->type != right->type) ||
        (left->auxiliary_type != right->auxiliary_type) ||
        (left->runtime_type != right->runtime_type) || (left->symbol != right->symbol) ||
        (left->task_scope != right->task_scope) || (left->borrow_origin != right->borrow_origin) ||
        (left->borrow_referent != right->borrow_referent) ||
        (left->borrow_origin_set != right->borrow_origin_set) ||
        (left->projected_borrow_state != right->projected_borrow_state) ||
        (left->operation != right->operation) ||
        (left->standard_operation != right->standard_operation) ||
        (left->source_operation != right->source_operation) ||
        (left->child_count != right->child_count) || (left->effect_exit_count != 0U) ||
        (right->effect_exit_count != 0U) || (left->integer_value != right->integer_value) ||
        (left->aggregate_member != right->aggregate_member) ||
        (left->case_pattern != right->case_pattern) || (left->is_place != right->is_place) ||
        (left->is_move != right->is_move) ||
        (left->replaces_initialized != right->replaces_initialized) ||
        (left->borrow_origin_multiple != right->borrow_origin_multiple) ||
        (left->immediate_await != right->immediate_await) ||
        (left->function_item_receiver != right->function_item_receiver) ||
        (left->out_commit != right->out_commit) || (left->out_argument != right->out_argument) ||
        (left->out_destination != right->out_destination) ||
        (left->out_place != right->out_place) || (left->deref_local != right->deref_local)) {
        return false;
    }
    for (child_index = 0U; child_index < left->child_count; ++child_index) {
        const size_t left_child = (size_t)left->first_child + (size_t)child_index;
        const size_t right_child = (size_t)right->first_child + (size_t)child_index;

        if ((left_child >= context->hir_child_count) || (right_child >= context->hir_child_count) ||
            !r_hir_same_tree_at(context,
                                context->hir_children[left_child],
                                context->hir_children[right_child],
                                depth + 1U)) {
            return false;
        }
    }
    return true;
}

bool r_hir_same_tree(const RFrontendContext *context, RHirNodeId left, RHirNodeId right) {
    return r_hir_same_tree_at(context, left, right, 0U);
}

static const char *r_hir_case_pattern_name(RHirCasePatternKind kind) {
    switch (kind) {
    case R_HIR_CASE_PATTERN_CONSTANT:
        return "constant";
    case R_HIR_CASE_PATTERN_VARIANT:
        return "variant";
    case R_HIR_CASE_PATTERN_DEFAULT:
        return "default";
    case R_HIR_CASE_PATTERN_INVALID:
    default:
        return "invalid";
    }
}

static bool r_hir_write_indent(RFrontendWriteFn writer, void *user_data, uint32_t depth) {
    uint32_t index;
    for (index = 0U; index < depth; ++index) {
        if (!r_write_text(writer, user_data, "  ")) {
            return false;
        }
    }
    return true;
}

static bool r_hir_write_uint64(RFrontendWriteFn writer, void *user_data, uint64_t value);

static const char *r_hir_type_constructor_name(const RSemanticType *type) {
    switch (type->kind) {
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return "fixed_array";
    case R_SEMANTIC_TYPE_SLICE:
        return (type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U ? "const_slice" : "slice";
    case R_SEMANTIC_TYPE_BORROW:
        if ((type->flags & R_SEMANTIC_TYPE_FLAG_OUT) != 0U)
            return "out";
        return (type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U ? "const_borrow" : "borrow";
    case R_SEMANTIC_TYPE_ARRAY:
        return "array";
    case R_SEMANTIC_TYPE_LIST:
        return "list";
    case R_SEMANTIC_TYPE_TASK:
        return "task";
    case R_SEMANTIC_TYPE_ARC:
        return "arc";
    case R_SEMANTIC_TYPE_RC:
        return "rc";
    case R_SEMANTIC_TYPE_WEAK:
        return (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U ? "weak_rc" : "weak_arc";
    case R_SEMANTIC_TYPE_OWN:
        return "own";
    case R_SEMANTIC_TYPE_RAW:
        return (type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U ? "raw_const" : "raw";
    case R_SEMANTIC_TYPE_OPTION:
        return "option";
    default:
        return NULL;
    }
}

static bool r_hir_write_type(const RFrontendContext *context,
                             RTypeId type_id,
                             RFrontendWriteFn writer,
                             void *user_data) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    const char *constructor_name;

    if (type == NULL) {
        return r_write_text(writer, user_data, "invalid");
    }
    if ((type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION) || (type->kind == R_SEMANTIC_TYPE_FUNCTION)) {
        /* R-TYPE-0054 (L28): a function type is spelled like a raw one, as `fn`. */
        const bool function_value = type->kind == R_SEMANTIC_TYPE_FUNCTION;
        RTypeId return_id = type->base;
        RTypeId effects = R_TYPE_ID_INVALID;
        const RSemanticType *result_type = r_semantic_type(context, return_id);
        if (((type->flags & R_SEMANTIC_TYPE_FLAG_CALLABLE) != 0U || function_value) &&
            result_type != NULL && result_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
            return_id = result_type->base;
            effects = result_type->second;
        }
        RTypeId parameter_id = type->second;
        uint64_t index;
        if (!r_write_text(
                writer, user_data, function_value ? "(fn parameters=(" : "(raw_fn parameters=(")) {
            return false;
        }
        for (index = UINT64_C(0); index < type->length; ++index) {
            const RSemanticType *parameter = r_semantic_type(context, parameter_id);
            if (parameter == NULL || parameter->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER ||
                (index != UINT64_C(0) && !r_write_text(writer, user_data, " ")) ||
                !r_hir_write_type(context, parameter->base, writer, user_data)) {
                return false;
            }
            parameter_id = parameter->second;
        }
        return parameter_id == R_TYPE_ID_INVALID && r_write_text(writer, user_data, ") return=") &&
               r_hir_write_type(context, return_id, writer, user_data) &&
               (effects == R_TYPE_ID_INVALID ||
                (r_write_text(writer, user_data, " throws=") &&
                 r_hir_write_type(context, effects, writer, user_data))) &&
               ((type->flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) == 0U ||
                r_write_text(writer, user_data, " async=true")) &&
               ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U ||
                r_write_text(writer, user_data, " nullable")) &&
               ((type->flags & R_SEMANTIC_TYPE_FLAG_VARIADIC) == 0U ||
                r_write_text(writer, user_data, " variadic")) &&
               ((type->flags & R_SEMANTIC_TYPE_FLAG_NOALLOC) == 0U ||
                r_write_text(writer, user_data, " noalloc=true")) &&
               ((type->flags & R_SEMANTIC_TYPE_FLAG_NONBLOCKING) == 0U ||
                r_write_text(writer, user_data, " nonblocking=true")) &&
               r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_FUNCTION_PARAMETER) {
        return r_write_text(writer, user_data, "(function_parameter ") &&
               r_hir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR) {
        /* A literal names its type, untyped ones being usize; a computed formula and an
           associated constant are named by their form, not by the token that marks them. */
        const char *form = type->flags == (uint32_t)R_TOKEN_LPAREN ? "call"
                           : type->flags == (uint32_t)R_TOKEN_DOT  ? "associated"
                           : type->flags == (uint32_t)R_TOKEN_INVALID
                               ? "usize"
                               : r_token_kind_name((RTokenKind)type->flags);
        if (type->base == 0U && type->second == 0U) {
            return r_write_text(writer, user_data, "(constant ") &&
                   r_write_text(writer, user_data, form) && r_write_text(writer, user_data, " ") &&
                   r_hir_write_uint64(writer, user_data, type->length) &&
                   r_write_text(writer, user_data, ")");
        }
        return r_write_text(writer, user_data, "(constant ") &&
               r_write_text(writer, user_data, form) && r_write_text(writer, user_data, " ") &&
               r_hir_write_uint64(writer, user_data, type->length) &&
               (type->base == 0U || (r_write_text(writer, user_data, " ") &&
                                     r_hir_write_type(context, type->base, writer, user_data))) &&
               (type->second == 0U ||
                (r_write_text(writer, user_data, " ") &&
                 r_hir_write_type(context, type->second, writer, user_data))) &&
               r_write_text(writer, user_data, ")");
    }
    if ((type->kind == R_SEMANTIC_TYPE_CONST) || (type->kind == R_SEMANTIC_TYPE_ATOMIC)) {
        return r_write_text(writer,
                            user_data,
                            type->kind == R_SEMANTIC_TYPE_CONST ? "(const " : "(atomic ") &&
               r_hir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        RTokenKind token_kind;

        if (type->length >= (uint64_t)R_TOKEN_KIND_COUNT) {
            return false;
        }
        token_kind = (RTokenKind)type->length;
        return r_token_is_c_abi_type(token_kind) &&
               r_write_text(writer, user_data, r_token_kind_name(token_kind));
    }
    if (type->kind == R_SEMANTIC_TYPE_EFFECT_SET) {
        if (!r_write_text(writer, user_data, "(effects")) {
            return false;
        }
        while ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_EFFECT_SET)) {
            if (type->base == 0U) {
                type = NULL;
                break;
            }
            if (!r_write_text(writer, user_data, " ") ||
                !r_hir_write_type(context, type->base, writer, user_data)) {
                return false;
            }
            type = r_semantic_type(context, type->second);
        }
        return (type == NULL) && r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
        return r_write_text(writer, user_data, "(carrier ") &&
               r_hir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, " ") &&
               r_hir_write_type(context, type->second, writer, user_data) &&
               r_write_text(writer, user_data, ")");
    }
    constructor_name = r_hir_type_constructor_name(type);
    if (constructor_name != NULL) {
        if (!r_write_text(writer, user_data, "(") ||
            !r_write_text(writer, user_data, constructor_name) ||
            !r_write_text(writer, user_data, " ") ||
            !r_hir_write_type(context, type->base, writer, user_data)) {
            return false;
        }
        if ((type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) &&
            (!r_write_text(writer, user_data, " ") ||
             !(type->second != 0U ? r_hir_write_type(context, type->second, writer, user_data)
                                  : r_hir_write_uint64(writer, user_data, type->length)))) {
            return false;
        }
        if ((type->kind == R_SEMANTIC_TYPE_TASK) && (type->second != R_TYPE_ID_INVALID) &&
            (!r_write_text(writer, user_data, " ") ||
             !r_hir_write_type(context, type->second, writer, user_data))) {
            return false;
        }
        if (((type->kind == R_SEMANTIC_TYPE_BORROW) || (type->kind == R_SEMANTIC_TYPE_OWN) ||
             (type->kind == R_SEMANTIC_TYPE_RAW)) &&
            ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) &&
            !r_write_text(writer, user_data, " nullable")) {
            return false;
        }
        return r_write_text(writer, user_data, ")");
    }
    if ((type->kind == R_SEMANTIC_TYPE_DICT) || (type->kind == R_SEMANTIC_TYPE_RESULT)) {
        return r_write_text(
                   writer, user_data, type->kind == R_SEMANTIC_TYPE_DICT ? "(dict " : "(result ") &&
               r_hir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, " ") &&
               r_hir_write_type(context, type->second, writer, user_data) &&
               r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_STANDARD) {
        const uint32_t name_id = (uint32_t)type->length;
        const RInternEntry *entry =
            (name_id == UINT32_C(0)) || ((size_t)name_id > context->intern_count)
                ? NULL
                : &context->intern_entries[(size_t)name_id - 1U];
        if ((entry == NULL) || !r_write_text(writer, user_data, "(standard ") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)entry->bytes, entry->length)) {
            return false;
        }
        if ((type->base != R_TYPE_ID_INVALID) &&
            (!r_write_text(writer, user_data, " ") ||
             !r_hir_write_type(context, type->base, writer, user_data))) {
            return false;
        }
        if ((type->second != R_TYPE_ID_INVALID) &&
            (!r_write_text(writer, user_data, " ") ||
             !r_hir_write_type(context, type->second, writer, user_data))) {
            return false;
        }
        return r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_DYN) {
        /* R-TYPE-0051: an interface is written as its canonical contract. */
        const RSemanticType *carrier = r_semantic_type(context, type->base);
        const RInternEntry *key =
            &context->intern_entries[context->generic_parameters[carrier->base - 1U].dyn_key - 1U];
        return r_write_text(writer, user_data, "(dyn ") &&
               r_write_escaped(writer, user_data, (const uint8_t *)key->bytes, key->length) &&
               r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        const RGenericParameter *parameter = &context->generic_parameters[type->base - 1U];
        const RInternEntry *name = &context->intern_entries[parameter->name_intern_id - 1U];
        return r_write_text(writer, user_data, "(parameter ") &&
               r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) &&
               r_write_text(writer, user_data, ")");
    }
    if ((type->kind == R_SEMANTIC_TYPE_STRUCT) || (type->kind == R_SEMANTIC_TYPE_ENUM)) {
        const uint32_t aggregate_id = (uint32_t)type->base;
        const RSemanticAggregate *aggregate;
        const RSource *source;
        if ((aggregate_id == UINT32_C(0)) ||
            ((size_t)aggregate_id > context->semantic_aggregate_count)) {
            return false;
        }
        aggregate = &context->semantic_aggregates[(size_t)aggregate_id - 1U];
        if (aggregate->is_tuple) {
            /* R-TYPE-0052 (L18.1): a tuple is written by its elements. */
            const uint32_t count =
                context->generic_schemas[aggregate->generic_schema - 1U].parameter_count;
            if (!r_write_text(writer, user_data, "(tuple")) {
                return false;
            }
            for (uint32_t index = 0U; (aggregate->generic_origin != 0U) && (index < count);
                 ++index) {
                if (!r_write_text(writer, user_data, " ") ||
                    !r_hir_write_type(
                        context,
                        context->generic_arguments[aggregate->first_generic_argument + index],
                        writer,
                        user_data)) {
                    return false;
                }
            }
            return r_write_text(writer, user_data, ")");
        }
        if (aggregate->error_family_of != 0U) {
            /* R-AGG-0011 (L21.3): the family of an error with descendants. */
            const RTypeId root =
                aggregate->error_family_of == R_SEMANTIC_STANDARD_ERROR_ROOT
                    ? context->standard_fault_type
                    : context->semantic_aggregates[aggregate->error_family_of - 1U].type;
            return r_write_text(writer, user_data, "(error_family ") &&
                   r_hir_write_type(context, root, writer, user_data) &&
                   r_write_text(writer, user_data, ")");
        }
        source = r_get_source_const(context, aggregate->module_source);
        if ((source == NULL) && (aggregate->module_source == R_SOURCE_ID_INVALID) &&
            aggregate->is_protected && (aggregate->name_intern_id != UINT32_C(0)) &&
            ((size_t)aggregate->name_intern_id <= context->intern_count)) {
            const RInternEntry *entry =
                &context->intern_entries[(size_t)aggregate->name_intern_id - 1U];

            return r_write_text(writer, user_data, "(standard_payload ") &&
                   r_write_escaped(
                       writer, user_data, (const uint8_t *)entry->bytes, entry->length) &&
                   r_write_text(writer, user_data, ")");
        }
        if ((source == NULL) || (source->module_name == NULL) ||
            (aggregate->name_span.end < aggregate->name_span.start) ||
            ((size_t)aggregate->name_span.end > source->length)) {
            return false;
        }
        return r_write_text(writer,
                            user_data,
                            type->kind == R_SEMANTIC_TYPE_STRUCT ? "(struct " : "(enum ") &&
               r_write_escaped(writer,
                               user_data,
                               (const uint8_t *)source->module_name,
                               strlen(source->module_name)) &&
               r_write_text(writer, user_data, "::") &&
               r_write_escaped(
                   writer,
                   user_data,
                   (const uint8_t *)context->intern_entries[aggregate->name_intern_id - 1U].bytes,
                   context->intern_entries[aggregate->name_intern_id - 1U].length) &&
               r_write_text(writer, user_data, ")");
    }
    return r_write_text(writer, user_data, r_semantic_type_kind_name(type->kind));
}

static bool r_hir_write_symbol_name(const RFrontendContext *context,
                                    const RSemanticSymbol *symbol,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    const RSource *source;
    size_t length;

    if ((symbol == NULL) || (symbol->name_span.end < symbol->name_span.start)) {
        return false;
    }
    if ((symbol->name_span.source == R_SOURCE_ID_INVALID) && (symbol->name_intern_id != 0U) &&
        ((size_t)symbol->name_intern_id <= context->intern_count)) {
        /* A synthetic symbol, such as the dispatcher of a function type, has no source. */
        const RInternEntry *name = &context->intern_entries[symbol->name_intern_id - 1U];
        return r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length);
    }
    source = r_get_source_const(context, symbol->name_span.source);
    if ((source == NULL) || ((size_t)symbol->name_span.end > source->length)) {
        return false;
    }
    length = (size_t)(symbol->name_span.end - symbol->name_span.start);
    return r_write_escaped(writer, user_data, source->bytes + symbol->name_span.start, length);
}

static bool r_hir_write_uint64(RFrontendWriteFn writer, void *user_data, uint64_t value) {
    char digits[20];
    size_t length = 0U;

    do {
        digits[length++] = (char)('0' + (value % UINT64_C(10)));
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    while (length != 0U) {
        char digit = digits[--length];
        if (!writer(user_data, &digit, 1U)) {
            return false;
        }
    }
    return true;
}

static bool r_hir_write_type_attribute(const RFrontendContext *context,
                                       RTypeId type,
                                       RFrontendWriteFn writer,
                                       void *user_data) {
    return r_write_text(writer, user_data, " type=") &&
           r_hir_write_type(context, type, writer, user_data);
}

static bool
r_hir_write_operation_attribute(RTokenKind operation, RFrontendWriteFn writer, void *user_data) {
    const char *name = r_token_kind_name(operation);
    return r_write_text(writer, user_data, " op=") &&
           r_write_escaped(writer, user_data, (const uint8_t *)name, strlen(name));
}

static bool r_hir_write_symbol_attribute(const RFrontendContext *context,
                                         const RHirNode *node,
                                         const RSemanticSymbol *symbol,
                                         RFrontendWriteFn writer,
                                         void *user_data) {
    if (symbol == NULL) {
        return r_write_text(writer, user_data, " symbol=invalid name=\"<unresolved>\"");
    }
    return r_write_text(writer, user_data, " symbol=") &&
           r_write_uint32(writer, user_data, node->symbol) &&
           r_write_text(writer, user_data, " name=") &&
           r_hir_write_symbol_name(context, symbol, writer, user_data);
}

static bool r_hir_dump_node(const RFrontendContext *context,
                            RHirNodeId node_id,
                            uint32_t depth,
                            RFrontendWriteFn writer,
                            void *user_data) {
    const RHirNode *node;
    const RSemanticSymbol *symbol = NULL;
    uint32_t child_index;

    if ((node_id == R_HIR_NODE_ID_INVALID) || ((size_t)node_id > context->hir_node_count)) {
        return false;
    }
    node = &context->hir_nodes[(size_t)node_id - 1U];
    if ((node->symbol != R_SYMBOL_ID_INVALID) &&
        ((size_t)node->symbol <= context->semantic_symbol_count)) {
        symbol = &context->semantic_symbols[(size_t)node->symbol - 1U];
    }
    if (!r_hir_write_indent(writer, user_data, depth) || !r_write_text(writer, user_data, "(") ||
        !r_write_text(writer, user_data, r_hir_kind_name(node->kind))) {
        return false;
    }
    if (node->source_operation != 0U) {
        const RInternEntry *operation = &context->intern_entries[node->source_operation - 1U];
        if (!r_write_text(writer, user_data, " source_operation=") ||
            !r_write_escaped(
                writer, user_data, (const uint8_t *)operation->bytes, operation->length)) {
            return false;
        }
    }
    if (node->kind == R_HIR_MODULE) {
        const RSource *source = r_get_source_const(context, node->span.source);
        const char *module_name =
            ((source == NULL) || (source->module_name == NULL) || (source->module_name[0] == '\0'))
                ? "<unresolved>"
                : source->module_name;
        if ((source == NULL) || !r_write_text(writer, user_data, " ") ||
            !r_write_escaped(
                writer, user_data, (const uint8_t *)module_name, strlen(module_name))) {
            return false;
        }
    } else if ((node->kind == R_HIR_FUNCTION) && (symbol != NULL)) {
        const char *state = symbol->has_definition ? "definition" : "prototype";
        if (!r_write_text(writer, user_data, " symbol=") ||
            !r_write_uint32(writer, user_data, node->symbol) ||
            !r_write_text(writer, user_data, " name=") ||
            !r_hir_write_symbol_name(context, symbol, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          symbol->is_protected ? " visibility=protected return="
                                               : " visibility=exported return=") ||
            !r_hir_write_type(context, node->type, writer, user_data) ||
            ((symbol->throws_type != R_TYPE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " throws=") ||
              !r_hir_write_type(context, symbol->throws_type, writer, user_data))) ||
            (symbol->is_async &&
             (!r_write_text(writer, user_data, " async start=") ||
              !r_hir_write_type(context, node->auxiliary_type, writer, user_data))) ||
            !r_write_text(writer, user_data, " state=") ||
            !r_write_text(writer, user_data, state) ||
            (symbol->poisoned && !r_write_text(writer, user_data, " poisoned"))) {
            return false;
        }
    } else if ((node->kind == R_HIR_PARAMETER) && (symbol != NULL)) {
        if (!r_write_text(writer, user_data, " symbol=") ||
            !r_write_uint32(writer, user_data, node->symbol) ||
            !r_write_text(writer, user_data, " name=") ||
            !r_hir_write_symbol_name(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_hir_write_type(context, node->type, writer, user_data)) {
            return false;
        }
    } else if (node->kind == R_HIR_NEW) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " payload=") ||
            !r_hir_write_type(context, node->auxiliary_type, writer, user_data) ||
            !r_write_text(writer, user_data, " owner=") ||
            !r_write_text(writer, user_data, r_token_kind_name(node->operation))) {
            return false;
        }
    } else if (node->kind == R_HIR_STANDARD_CALL) {
        const char *operation_name = NULL;
        const RAsyncSyncDescriptor *async_sync = r_async_sync_descriptor(node->standard_operation);
        switch (node->standard_operation) {
        case R_STANDARD_CALL_CORE_HASH:
            operation_name = "core::hash";
            break;
        case R_STANDARD_CALL_CORE_KEY_EQUAL:
            operation_name = "core::key_equal";
            break;
        case R_STANDARD_CALL_CORE_ASSUME:
            operation_name = "core::assume";
            break;
        case R_STANDARD_CALL_CORE_PANIC:
            operation_name = "panic";
            break;
        case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS:
            /* L38: an anchored slice is the same descriptor; only its region differs. */
            operation_name = node->integer_value == UINT64_C(1) ? "core::slice_from_raw_parts_in"
                                                                : "core::slice_from_raw_parts";
            break;
        case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT:
            operation_name = node->integer_value == UINT64_C(1)
                                 ? "core::slice_from_raw_parts_in_mut"
                                 : "core::slice_from_raw_parts_mut";
            break;
        case R_STANDARD_CALL_CORE_VOLATILE_LOAD:
            operation_name = "core::volatile_load";
            break;
        case R_STANDARD_CALL_CORE_VOLATILE_STORE:
            operation_name = "core::volatile_store";
            break;
        case R_STANDARD_CALL_CORE_ADOPT:
            operation_name = "core::adopt";
            break;
        case R_STANDARD_CALL_CORE_REPLACE:
            operation_name = "core::replace";
            break;
        case R_STANDARD_CALL_CORE_TAKE:
            operation_name = "core::take";
            break;
        case R_STANDARD_CALL_CORE_SWAP:
            operation_name = "core::swap";
            break;
        case R_STANDARD_CALL_CORE_CLONE:
            operation_name = "core::clone";
            break;
        case R_STANDARD_CALL_CORE_FORMAT_RENDER:
            operation_name = "core::format_render";
            break;
        case R_STANDARD_CALL_CORE_FORMAT_APPEND:
            operation_name = "core::format_append";
            break;
        case R_STANDARD_CALL_CORE_RELEASE:
            operation_name = "core::release";
            break;
        case R_STANDARD_CALL_CORE_CHECKED_ADD:
            operation_name = "core::checked_add";
            break;
        case R_STANDARD_CALL_CORE_CHECKED_SUB:
            operation_name = "core::checked_sub";
            break;
        case R_STANDARD_CALL_CORE_CHECKED_MUL:
            operation_name = "core::checked_mul";
            break;
        case R_STANDARD_CALL_CORE_WRAPPING_ADD:
            operation_name = "core::wrapping_add";
            break;
        case R_STANDARD_CALL_CORE_WRAPPING_SUB:
            operation_name = "core::wrapping_sub";
            break;
        case R_STANDARD_CALL_CORE_WRAPPING_MUL:
            operation_name = "core::wrapping_mul";
            break;
        case R_STANDARD_CALL_CORE_SATURATING_ADD:
            operation_name = "core::saturating_add";
            break;
        case R_STANDARD_CALL_CORE_SATURATING_SUB:
            operation_name = "core::saturating_sub";
            break;
        case R_STANDARD_CALL_CORE_SATURATING_MUL:
            operation_name = "core::saturating_mul";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_LOAD:
            operation_name = "core::atomic_load";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_STORE:
            operation_name = "core::atomic_store";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_EXCHANGE:
            operation_name = "core::atomic_exchange";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE:
            operation_name = "core::atomic_compare_exchange";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD:
            operation_name = "core::atomic_fetch_add";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_FETCH_SUB:
            operation_name = "core::atomic_fetch_sub";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_FETCH_AND:
            operation_name = "core::atomic_fetch_and";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_FETCH_OR:
            operation_name = "core::atomic_fetch_or";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_FETCH_XOR:
            operation_name = "core::atomic_fetch_xor";
            break;
        case R_STANDARD_CALL_CORE_ATOMIC_IS_LOCK_FREE:
            operation_name = "core::atomic_is_lock_free";
            break;
        case R_STANDARD_CALL_CORE_ENUM_NAME:
            operation_name = "core::enum_name";
            break;
        case R_STANDARD_CALL_CORE_ENUM_ORDINAL:
            operation_name = "core::enum_ordinal";
            break;
        case R_STANDARD_CALL_CORE_ENUM_AT:
            operation_name = "core::enum_at";
            break;
        case R_STANDARD_CALL_CORE_ENUM_FROM_NAME:
            operation_name = "core::enum_from_name";
            break;
        case R_STANDARD_CALL_CORE_VARIANT_NAME:
            operation_name = "core::variant_name";
            break;
        case R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE:
            operation_name = "core::type_attribute";
            break;
        case R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE:
            operation_name = "core::field_attribute";
            break;
        case R_STANDARD_CALL_CORE_VARIANT_ATTRIBUTE:
            operation_name = "core::variant_attribute";
            break;
        case R_STANDARD_CALL_CORE_FIELD_NAME_AT:
            operation_name = "core::field_name";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_ENUM_COUNT:
            operation_name = "core::enum_count";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_ENUM_MIN:
            operation_name = "core::enum_min";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_ENUM_MAX:
            operation_name = "core::enum_max";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_VARIANT_COUNT:
            operation_name = "core::variant_count";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_TYPE_NAME:
            operation_name = "core::type_name";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT:
            operation_name = "core::field_count";
            break;
        case R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME:
            operation_name = "core::field_name";
            break;
        case R_STANDARD_CALL_ARC_CLONE:
            operation_name = "std.arc::clone";
            break;
        case R_STANDARD_CALL_RC_CLONE:
            operation_name = "std.rc::clone";
            break;
        case R_STANDARD_CALL_ARC_CLONE_WEAK:
            operation_name = "std.arc::clone_weak";
            break;
        case R_STANDARD_CALL_ARC_DOWNGRADE:
            operation_name = "std.arc::downgrade";
            break;
        case R_STANDARD_CALL_ARC_UPGRADE:
            operation_name = "std.arc::upgrade";
            break;
        case R_STANDARD_CALL_ARC_GET_MUT:
            operation_name = "std.arc::get_mut";
            break;
        case R_STANDARD_CALL_ARC_STRONG_COUNT:
            operation_name = "std.arc::strong_count";
            break;
        case R_STANDARD_CALL_ARC_WEAK_COUNT:
            operation_name = "std.arc::weak_count";
            break;
        case R_STANDARD_CALL_ARC_PTR_EQ:
            operation_name = "std.arc::ptr_eq";
            break;
        case R_STANDARD_CALL_ARC_INTO_RAW:
            operation_name = "std.arc::into_raw";
            break;
        case R_STANDARD_CALL_ARC_FROM_RAW:
            operation_name = "std.arc::from_raw";
            break;
        case R_STANDARD_CALL_ARC_TRY_UNWRAP:
            operation_name = "std.arc::try_unwrap";
            break;
        case R_STANDARD_CALL_RC_CLONE_WEAK:
            operation_name = "std.rc::clone_weak";
            break;
        case R_STANDARD_CALL_RC_DOWNGRADE:
            operation_name = "std.rc::downgrade";
            break;
        case R_STANDARD_CALL_RC_UPGRADE:
            operation_name = "std.rc::upgrade";
            break;
        case R_STANDARD_CALL_RC_GET_MUT:
            operation_name = "std.rc::get_mut";
            break;
        case R_STANDARD_CALL_RC_STRONG_COUNT:
            operation_name = "std.rc::strong_count";
            break;
        case R_STANDARD_CALL_RC_WEAK_COUNT:
            operation_name = "std.rc::weak_count";
            break;
        case R_STANDARD_CALL_RC_PTR_EQ:
            operation_name = "std.rc::ptr_eq";
            break;
        case R_STANDARD_CALL_RC_INTO_RAW:
            operation_name = "std.rc::into_raw";
            break;
        case R_STANDARD_CALL_RC_FROM_RAW:
            operation_name = "std.rc::from_raw";
            break;
        case R_STANDARD_CALL_RC_TRY_UNWRAP:
            operation_name = "std.rc::try_unwrap";
            break;
        case R_STANDARD_CALL_ASYNC_CANCEL:
            operation_name = "std.async::cancel";
            break;
        case R_STANDARD_CALL_ASYNC_DETACH:
            operation_name = "std.async::detach";
            break;
        case R_STANDARD_CALL_ASYNC_DEADLINE_ENTER:
            operation_name = "std.async::deadline_enter";
            break;
        case R_STANDARD_CALL_ASYNC_DEADLINE_LEAVE:
            operation_name = "std.async::deadline_leave";
            break;
        case R_STANDARD_CALL_ASYNC_BUDGET_ENTER:
            operation_name = "std.async::budget_enter";
            break;
        case R_STANDARD_CALL_ASYNC_BUDGET_LEAVE:
            operation_name = "std.async::budget_leave";
            break;
        case R_STANDARD_CALL_CORE_RECURSION_ENTER:
            operation_name = "core::recursion_enter";
            break;
        case R_STANDARD_CALL_CORE_RECURSION_LEAVE:
            operation_name = "core::recursion_leave";
            break;
        case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_IN:
            operation_name = "core::slice_from_raw_parts_in";
            break;
        case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_IN_MUT:
            operation_name = "core::slice_from_raw_parts_in_mut";
            break;
        case R_STANDARD_CALL_SYNC_RECEIVE:
            operation_name = "std.sync::receive";
            break;
        case R_STANDARD_CALL_THREAD_SPAWN:
            operation_name = "std.thread::spawn";
            break;
        case R_STANDARD_CALL_ASYNC_BLOCKING:
            operation_name = "std.async::blocking";
            break;
        case R_STANDARD_CALL_THREAD_SPAWN_SCOPED:
            operation_name = "std.thread::spawn_scoped";
            break;
        case R_STANDARD_CALL_THREAD_JOIN:
            operation_name = "std.thread::join";
            break;
        case R_STANDARD_CALL_THREAD_DETACH:
            operation_name = "std.thread::detach";
            break;
        case R_STANDARD_CALL_SYNC_ONCE_NEW:
            operation_name = "std.sync::once_new";
            break;
        case R_STANDARD_CALL_SYNC_ONCE_LOCK:
            operation_name = "std.sync::once_lock";
            break;
        case R_STANDARD_CALL_SYNC_CALL_ONCE:
            operation_name = "std.sync::call_once";
            break;
        case R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE:
            operation_name = "std.sync::call_once_force";
            break;
        case R_STANDARD_CALL_SYNC_GET_OR_INIT:
            operation_name = "std.sync::get_or_init";
            break;
        case R_STANDARD_CALL_SYNC_CHANNEL:
            operation_name = "std.sync::channel";
            break;
        case R_STANDARD_CALL_SYNC_SYNC_CHANNEL:
            operation_name = "std.sync::sync_channel";
            break;
        case R_STANDARD_CALL_SYNC_SENDER:
            operation_name = "std.sync::sender";
            break;
        case R_STANDARD_CALL_SYNC_SYNC_SENDER:
            operation_name = "std.sync::sync_sender";
            break;
        case R_STANDARD_CALL_SYNC_CLONE_SENDER:
            operation_name = "std.sync::clone_sender";
            break;
        case R_STANDARD_CALL_SYNC_CLONE_SYNC_SENDER:
            operation_name = "std.sync::clone_sync_sender";
            break;
        case R_STANDARD_CALL_SYNC_SYNC_RECEIVER:
            operation_name = "std.sync::sync_receiver";
            break;
        case R_STANDARD_CALL_SYNC_SEND:
            operation_name = "std.sync::send";
            break;
        case R_STANDARD_CALL_SYNC_SYNC_SEND:
            operation_name = "std.sync::sync_send";
            break;
        case R_STANDARD_CALL_SYNC_TRY_SEND:
            operation_name = "std.sync::try_send";
            break;
        case R_STANDARD_CALL_SYNC_RECV:
            operation_name = "std.sync::recv";
            break;
        case R_STANDARD_CALL_SYNC_TRY_RECV:
            operation_name = "std.sync::try_recv";
            break;
        case R_STANDARD_CALL_SYNC_GET:
            operation_name = "std.sync::get";
            break;
        case R_STANDARD_CALL_SYNC_SET:
            operation_name = "std.sync::set";
            break;
        case R_STANDARD_CALL_SYNC_MUTEX_NEW:
            operation_name = "std.sync::mutex_new";
            break;
        case R_STANDARD_CALL_SYNC_RWLOCK_NEW:
            operation_name = "std.sync::rwlock_new";
            break;
        case R_STANDARD_CALL_SYNC_LOCK:
            operation_name = "std.sync::lock";
            break;
        case R_STANDARD_CALL_SYNC_TRY_LOCK:
            operation_name = "std.sync::try_lock";
            break;
        case R_STANDARD_CALL_SYNC_READ:
            operation_name = "std.sync::read";
            break;
        case R_STANDARD_CALL_SYNC_TRY_READ:
            operation_name = "std.sync::try_read";
            break;
        case R_STANDARD_CALL_SYNC_WRITE:
            operation_name = "std.sync::write";
            break;
        case R_STANDARD_CALL_SYNC_TRY_WRITE:
            operation_name = "std.sync::try_write";
            break;
        case R_STANDARD_CALL_SYNC_MUTEX_GUARD_REF:
            operation_name = "std.sync::mutex_guard_ref";
            break;
        case R_STANDARD_CALL_SYNC_MUTEX_GUARD_MUT:
            operation_name = "std.sync::mutex_guard_mut";
            break;
        case R_STANDARD_CALL_SYNC_RW_READ_GUARD_REF:
            operation_name = "std.sync::rw_read_guard_ref";
            break;
        case R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_REF:
            operation_name = "std.sync::rw_write_guard_ref";
            break;
        case R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT:
            operation_name = "std.sync::rw_write_guard_mut";
            break;
        case R_STANDARD_CALL_SYNC_UNLOCK:
            operation_name = "std.sync::unlock";
            break;
        case R_STANDARD_CALL_SYNC_WAIT:
            operation_name = "std.sync::wait";
            break;
        case R_STANDARD_CALL_SYNC_RECEIVER:
            operation_name = "std.sync::receiver";
            break;
        case R_STANDARD_CALL_SYNC_BARRIER_NEW:
            operation_name = "std.sync::barrier_new";
            break;
        case R_STANDARD_CALL_SYNC_BARRIER_WAIT:
            operation_name = "std.sync::barrier_wait";
            break;
        case R_STANDARD_CALL_SYNC_CONDVAR_NEW:
            operation_name = "std.sync::condvar_new";
            break;
        case R_STANDARD_CALL_SYNC_NOTIFY_ONE:
            operation_name = "std.sync::notify_one";
            break;
        case R_STANDARD_CALL_SYNC_NOTIFY_ALL:
            operation_name = "std.sync::notify_all";
            break;
        case R_STANDARD_CALL_FORMAT_CREATE:
            operation_name = "std.format::create";
            break;
        case R_STANDARD_CALL_FORMAT_WITH_CAPACITY:
            operation_name = "std.format::with_capacity";
            break;
        case R_STANDARD_CALL_FORMAT_AS_STR:
            operation_name = "std.format::as_str";
            break;
        case R_STANDARD_CALL_FORMAT_CLEAR:
            operation_name = "std.format::clear";
            break;
        case R_STANDARD_CALL_FORMAT_FINISH:
            operation_name = "std.format::finish";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_STR:
            operation_name = "std.format::append_str";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_CHAR:
            operation_name = "std.format::append_char";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_F32:
            operation_name = "std.format::append_f32";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_F64:
            operation_name = "std.format::append_f64";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_C_FLOAT:
            operation_name = "std.format::append_c_float";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_C_DOUBLE:
            operation_name = "std.format::append_c_double";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE:
            operation_name = "std.format::append_c_long_double";
            break;
        case R_STANDARD_CALL_FORMAT_APPEND_INTEGER:
            operation_name = "std.format::append_SUFFIX";
            break;
        case R_STANDARD_CALL_ALLOC_TRY_NEW:
            operation_name = "std.alloc::try_new";
            break;
        case R_STANDARD_CALL_ALLOC_INTO_VALUE:
            operation_name = "std.alloc::into_value";
            break;
        case R_STANDARD_CALL_ARRAY_CAPACITY:
            operation_name = "std.array::capacity";
            break;
        case R_STANDARD_CALL_ARRAY_RESERVE:
            operation_name = "std.array::reserve";
            break;
        case R_STANDARD_CALL_ARRAY_POP:
            operation_name = "std.array::pop";
            break;
        case R_STANDARD_CALL_ARRAY_REMOVE:
            operation_name = "std.array::remove";
            break;
        case R_STANDARD_CALL_ARRAY_GET:
            operation_name = "std.array::get";
            break;
        case R_STANDARD_CALL_ARRAY_GET_MUT:
            operation_name = "std.array::get_mut";
            break;
        case R_STANDARD_CALL_ARRAY_CLEAR:
            operation_name = "std.array::clear";
            break;
        case R_STANDARD_CALL_LIST_CREATE:
            operation_name = "std.list::create";
            break;
        case R_STANDARD_CALL_LIST_PUSH_FRONT:
            operation_name = "std.list::push_front";
            break;
        case R_STANDARD_CALL_LIST_PUSH_BACK:
            operation_name = "std.list::push_back";
            break;
        case R_STANDARD_CALL_LIST_INSERT_BEFORE:
            operation_name = "std.list::insert_before";
            break;
        case R_STANDARD_CALL_LIST_INSERT_AFTER:
            operation_name = "std.list::insert_after";
            break;
        case R_STANDARD_CALL_LIST_FRONT:
            operation_name = "std.list::front";
            break;
        case R_STANDARD_CALL_LIST_BACK:
            operation_name = "std.list::back";
            break;
        case R_STANDARD_CALL_LIST_FRONT_MUT:
            operation_name = "std.list::front_mut";
            break;
        case R_STANDARD_CALL_LIST_BACK_MUT:
            operation_name = "std.list::back_mut";
            break;
        case R_STANDARD_CALL_LIST_GET:
            operation_name = "std.list::get";
            break;
        case R_STANDARD_CALL_LIST_GET_MUT:
            operation_name = "std.list::get_mut";
            break;
        case R_STANDARD_CALL_LIST_REMOVE:
            operation_name = "std.list::remove";
            break;
        case R_STANDARD_CALL_LIST_POP_FRONT:
            operation_name = "std.list::pop_front";
            break;
        case R_STANDARD_CALL_LIST_POP_BACK:
            operation_name = "std.list::pop_back";
            break;
        case R_STANDARD_CALL_LIST_CLEAR:
            operation_name = "std.list::clear";
            break;
        case R_STANDARD_CALL_LIST_ITER:
            operation_name = "std.list::iter";
            break;
        case R_STANDARD_CALL_LIST_NEXT:
            operation_name = "std.list::next";
            break;
        case R_STANDARD_CALL_DICT_CREATE:
            operation_name = "std.dict::create";
            break;
        case R_STANDARD_CALL_DICT_WITH_CAPACITY:
            operation_name = "std.dict::with_capacity";
            break;
        case R_STANDARD_CALL_DICT_RESERVE:
            operation_name = "std.dict::reserve";
            break;
        case R_STANDARD_CALL_DICT_INSERT:
            operation_name = "std.dict::insert";
            break;
        case R_STANDARD_CALL_DICT_CONTAINS:
            operation_name = "std.dict::contains";
            break;
        case R_STANDARD_CALL_DICT_GET:
            operation_name = "std.dict::get";
            break;
        case R_STANDARD_CALL_DICT_GET_MUT:
            operation_name = "std.dict::get_mut";
            break;
        case R_STANDARD_CALL_DICT_REMOVE:
            operation_name = "std.dict::remove";
            break;
        case R_STANDARD_CALL_DICT_CLEAR:
            operation_name = "std.dict::clear";
            break;
        case R_STANDARD_CALL_DICT_ITER:
            operation_name = "std.dict::iter";
            break;
        case R_STANDARD_CALL_DICT_NEXT:
            operation_name = "std.dict::next";
            break;
        case R_STANDARD_CALL_ARRAY_CREATE:
            operation_name = "std.array::create";
            break;
        case R_STANDARD_CALL_ARRAY_PUSH:
            operation_name = "std.array::push";
            break;
        case R_STANDARD_CALL_ARRAY_WITH_CAPACITY:
            operation_name = "std.array::with_capacity";
            break;
        case R_STANDARD_CALL_ARRAY_FILLED:
            operation_name = "std.array::filled";
            break;
        case R_STANDARD_CALL_BYTES_WITH_CAPACITY:
            operation_name = "std.bytes::with_capacity";
            break;
        case R_STANDARD_CALL_BYTES_APPEND:
            operation_name = "std.bytes::append";
            break;
        case R_STANDARD_CALL_BYTES_APPEND_U8:
            operation_name = "std.bytes::append_u8";
            break;
        case R_STANDARD_CALL_BYTES_APPEND_U16_LE:
            operation_name = "std.bytes::append_u16_le";
            break;
        case R_STANDARD_CALL_BYTES_APPEND_U32_LE:
            operation_name = "std.bytes::append_u32_le";
            break;
        case R_STANDARD_CALL_BYTES_APPEND_U64_LE:
            operation_name = "std.bytes::append_u64_le";
            break;
        case R_STANDARD_CALL_BYTES_COPY:
            operation_name = "std.bytes::copy";
            break;
        case R_STANDARD_CALL_BYTES_COPY_WITHIN:
            operation_name = "std.bytes::copy_within";
            break;
        case R_STANDARD_CALL_BYTES_FILL:
            operation_name = "std.bytes::fill";
            break;
        case R_STANDARD_CALL_BYTES_FIND:
            operation_name = "std.bytes::find";
            break;
        case R_STANDARD_CALL_BYTES_FIND_SLICE:
            operation_name = "std.bytes::find_slice";
            break;
        case R_STANDARD_CALL_BYTES_STARTS_WITH:
            operation_name = "std.bytes::starts_with";
            break;
        case R_STANDARD_CALL_BYTES_ENDS_WITH:
            operation_name = "std.bytes::ends_with";
            break;
        case R_STANDARD_CALL_HASH_CRC32:
            operation_name = "std.hash::crc32";
            break;
        case R_STANDARD_CALL_HASH_MD5:
            operation_name = "std.hash::md5";
            break;
        case R_STANDARD_CALL_HASH_SHA1:
            operation_name = "std.hash::sha1";
            break;
        case R_STANDARD_CALL_HASH_SHA256:
            operation_name = "std.hash::sha256";
            break;
        case R_STANDARD_CALL_HASH_SHA512:
            operation_name = "std.hash::sha512";
            break;
        case R_STANDARD_CALL_UTF8_IS_VALID:
            operation_name = "std.utf8::is_valid";
            break;
        case R_STANDARD_CALL_UTF8_VALIDATE:
            operation_name = "std.utf8::validate";
            break;
        case R_STANDARD_CALL_BITS_READ:
            operation_name = "std.bits::read";
            break;
        case R_STANDARD_CALL_BITS_ALIGN_BYTE:
            operation_name = "std.bits::align_byte";
            break;
        case R_STANDARD_CALL_FS_PATH_FROM_UTF8:
            operation_name = "std.fs::path_from_utf8";
            break;
        case R_STANDARD_CALL_FS_PATH_FROM_UTF8_BYTES:
            operation_name = "std.fs::path_from_utf8_bytes";
            break;
        case R_STANDARD_CALL_FS_PATH_CLONE:
            operation_name = "std.fs::path_clone";
            break;
        case R_STANDARD_CALL_FS_PATH_TO_UTF8:
            operation_name = "std.fs::path_to_utf8";
            break;
        case R_STANDARD_CALL_FS_PATH_JOIN:
            operation_name = "std.fs::path_join";
            break;
        case R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE:
            operation_name = "std.fs::path_is_absolute";
            break;
        case R_STANDARD_CALL_FS_READ_FILE:
            operation_name = "std.fs::read_file";
            break;
        case R_STANDARD_CALL_FS_OPEN_DIRECTORY:
            operation_name = "std.fs::open_directory";
            break;
        case R_STANDARD_CALL_FS_OPEN_FILE:
            operation_name = "std.fs::open_file";
            break;
        case R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH:
            operation_name = "std.fs::create_directory_beneath";
            break;
        case R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH:
            operation_name = "std.fs::write_file_atomic_no_replace_beneath";
            break;
        case R_STANDARD_CALL_FS_AS_ERROR:
            operation_name = "std.fs::as_error";
            break;
        case R_STANDARD_CALL_FS_FILE_METADATA:
            operation_name = "std.fs::file_metadata";
            break;
        case R_STANDARD_CALL_FS_CLOSE_DIRECTORY:
            operation_name = "std.fs::close_directory";
            break;
        case R_STANDARD_CALL_FS_CLOSE_FILE:
            operation_name = "std.fs::close_file";
            break;
        case R_STANDARD_CALL_FS_CREATE_DIRECTORY:
            operation_name = "std.fs::create_directory";
            break;
        case R_STANDARD_CALL_FS_FLUSH:
            operation_name = "std.fs::flush";
            break;
        case R_STANDARD_CALL_FS_ITERATE:
            operation_name = "std.fs::iterate";
            break;
        case R_STANDARD_CALL_FS_METADATA:
            operation_name = "std.fs::metadata";
            break;
        case R_STANDARD_CALL_FS_METADATA_BENEATH:
            operation_name = "std.fs::metadata_beneath";
            break;
        case R_STANDARD_CALL_FS_NEXT:
            operation_name = "std.fs::next";
            break;
        case R_STANDARD_CALL_FS_OPEN_DIRECTORY_BENEATH:
            operation_name = "std.fs::open_directory_beneath";
            break;
        case R_STANDARD_CALL_FS_OPEN_FILE_BENEATH:
            operation_name = "std.fs::open_file_beneath";
            break;
        case R_STANDARD_CALL_FS_READ:
            operation_name = "std.fs::read";
            break;
        case R_STANDARD_CALL_FS_READ_FILE_BENEATH:
            operation_name = "std.fs::read_file_beneath";
            break;
        case R_STANDARD_CALL_FS_REMOVE_DIRECTORY_BENEATH:
            operation_name = "std.fs::remove_directory_beneath";
            break;
        case R_STANDARD_CALL_FS_REMOVE_FILE_BENEATH:
            operation_name = "std.fs::remove_file_beneath";
            break;
        case R_STANDARD_CALL_FS_RENAME_BENEATH:
            operation_name = "std.fs::rename_beneath";
            break;
        case R_STANDARD_CALL_FS_SEEK:
            operation_name = "std.fs::seek";
            break;
        case R_STANDARD_CALL_FS_SYNC:
            operation_name = "std.fs::sync";
            break;
        case R_STANDARD_CALL_FS_READ_AT:
            operation_name = "std.fs::read_at";
            break;
        case R_STANDARD_CALL_FS_WRITE_ALL_AT:
            operation_name = "std.fs::write_all_at";
            break;
        case R_STANDARD_CALL_FS_TRY_LOCK:
            operation_name = "std.fs::try_lock";
            break;
        case R_STANDARD_CALL_FS_LOCK:
            operation_name = "std.fs::lock";
            break;
        case R_STANDARD_CALL_FS_UNLOCK:
            operation_name = "std.fs::unlock";
            break;
        case R_STANDARD_CALL_FS_WRITE:
            operation_name = "std.fs::write";
            break;
        case R_STANDARD_CALL_FS_WRITE_ALL:
            operation_name = "std.fs::write_all";
            break;
        case R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE:
            operation_name = "std.fs::write_file_atomic_no_replace";
            break;
        case R_STANDARD_CALL_IO_STDIN:
            operation_name = "std.io::stdin";
            break;
        case R_STANDARD_CALL_IO_STDOUT:
            operation_name = "std.io::stdout";
            break;
        case R_STANDARD_CALL_IO_STDERR:
            operation_name = "std.io::stderr";
            break;
        case R_STANDARD_CALL_IO_READ:
            operation_name = "std.io::read";
            break;
        case R_STANDARD_CALL_IO_FLUSH:
            operation_name = "std.io::flush";
            break;
        case R_STANDARD_CALL_IO_CLOSE_INPUT:
            operation_name = "std.io::close_input";
            break;
        case R_STANDARD_CALL_IO_CLOSE_OUTPUT:
            operation_name = "std.io::close_output";
            break;
        case R_STANDARD_CALL_IO_WRITE:
            operation_name = "std.io::write";
            break;
        case R_STANDARD_CALL_IO_WRITE_ALL:
            operation_name = "std.io::write_all";
            break;
        case R_STANDARD_CALL_IO_WRITE_SHARED:
            operation_name = "std.io::write_shared";
            break;
        case R_STANDARD_CALL_IO_AS_ERROR:
            operation_name = "std.io::as_error";
            break;
        case R_STANDARD_CALL_CONVERT_PARSE:
            operation_name = "std.convert::parse";
            break;
        case R_STANDARD_CALL_CONVERT_CHECKED:
            operation_name = "std.convert::checked";
            break;
        case R_STANDARD_CALL_C_CHECKED:
            operation_name = "std.c::checked";
            break;
        case R_STANDARD_CALL_C_LINK_AVAILABLE:
            operation_name = "std.c::link_available";
            break;
        case R_STANDARD_CALL_ALLOC_BYTES:
            operation_name = "std.alloc::bytes";
            break;
        case R_STANDARD_CALL_BYTES_EQUAL:
            operation_name = "std.bytes::equal";
            break;
        case R_STANDARD_CALL_BYTES_COMPARE:
            operation_name = "std.bytes::compare";
            break;
        case R_STANDARD_CALL_C_TARGET:
            operation_name = "std.c::target";
            break;
        case R_STANDARD_CALL_C_STRING_FROM_STR:
            operation_name = "std.c::string_from_str";
            break;
        case R_STANDARD_CALL_C_STRING_AS_SLICE:
            operation_name = "std.c::string_as_slice";
            break;
        case R_STANDARD_CALL_C_STRING_AS_PTR:
            operation_name = "std.c::string_as_ptr";
            break;
        case R_STANDARD_CALL_C_VALIDATE_UTF8:
            operation_name = "std.c::validate_utf8";
            break;
        case R_STANDARD_CALL_C_COPY_UTF8:
            operation_name = "std.c::copy_utf8";
            break;
        case R_STANDARD_CALL_C_ATTACH_THREAD:
            operation_name = "std.c::attach_thread";
            break;
        case R_STANDARD_CALL_C_DETACH_THREAD:
            operation_name = "std.c::detach_thread";
            break;
        case R_STANDARD_CALL_C_HANDLE_POINTER:
            operation_name = "std.c::handle_pointer";
            break;
        case R_STANDARD_CALL_C_ADOPT_HANDLE:
            operation_name = "std.c::adopt_handle";
            break;
        case R_STANDARD_CALL_C_RELEASE_HANDLE:
            operation_name = "std.c::release_handle";
            break;
        case R_STANDARD_CALL_ENV_ARGUMENTS:
            operation_name = "std.env::arguments";
            break;
        case R_STANDARD_CALL_ENV_VARIABLES:
            operation_name = "std.env::variables";
            break;
        case R_STANDARD_CALL_ENV_GET:
            operation_name = "std.env::get";
            break;
        case R_STANDARD_CALL_ENV_SET:
            operation_name = "std.env::set";
            break;
        case R_STANDARD_CALL_ENV_REMOVE:
            operation_name = "std.env::remove";
            break;
        case R_STANDARD_CALL_NET_PARSE_IP:
            operation_name = "std.net::parse_ip";
            break;
        case R_STANDARD_CALL_NET_FORMAT_IP:
            operation_name = "std.net::format_ip";
            break;
        case R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS:
            operation_name = "std.net::tcp_local_address";
            break;
        case R_STANDARD_CALL_NET_TCP_LISTENER_LOCAL_ADDRESS:
            operation_name = "std.net::tcp_listener_local_address";
            break;
        case R_STANDARD_CALL_NET_TCP_PEER_ADDRESS:
            operation_name = "std.net::tcp_peer_address";
            break;
        case R_STANDARD_CALL_NET_UDP_LOCAL_ADDRESS:
            operation_name = "std.net::udp_local_address";
            break;
        case R_STANDARD_CALL_NET_TCP_GET_OPTIONS:
            operation_name = "std.net::tcp_get_options";
            break;
        case R_STANDARD_CALL_NET_TCP_SET_OPTIONS:
            operation_name = "std.net::tcp_set_options";
            break;
        case R_STANDARD_CALL_NET_UDP_GET_OPTIONS:
            operation_name = "std.net::udp_get_options";
            break;
        case R_STANDARD_CALL_NET_UDP_SET_OPTIONS:
            operation_name = "std.net::udp_set_options";
            break;
        case R_STANDARD_CALL_NET_UDP_JOIN_MULTICAST:
            operation_name = "std.net::udp_join_multicast";
            break;
        case R_STANDARD_CALL_NET_UDP_LEAVE_MULTICAST:
            operation_name = "std.net::udp_leave_multicast";
            break;
        case R_STANDARD_CALL_NET_UNIX_PEER_CREDENTIALS:
            operation_name = "std.net::unix_peer_credentials";
            break;
        case R_STANDARD_CALL_NET_RESOLVE:
            operation_name = "std.net::resolve";
            break;
        case R_STANDARD_CALL_NET_TCP_LISTEN:
            operation_name = "std.net::tcp_listen";
            break;
        case R_STANDARD_CALL_NET_TCP_ACCEPT:
            operation_name = "std.net::tcp_accept";
            break;
        case R_STANDARD_CALL_NET_TCP_CONNECT:
            operation_name = "std.net::tcp_connect";
            break;
        case R_STANDARD_CALL_NET_TCP_READ:
            operation_name = "std.net::tcp_read";
            break;
        case R_STANDARD_CALL_NET_TCP_WRITE:
            operation_name = "std.net::tcp_write";
            break;
        case R_STANDARD_CALL_NET_TCP_WRITE_ALL:
            operation_name = "std.net::tcp_write_all";
            break;
        case R_STANDARD_CALL_NET_TCP_SHUTDOWN:
            operation_name = "std.net::tcp_shutdown";
            break;
        case R_STANDARD_CALL_NET_TCP_CLOSE:
            operation_name = "std.net::tcp_close";
            break;
        case R_STANDARD_CALL_NET_TCP_LISTENER_CLOSE:
            operation_name = "std.net::tcp_listener_close";
            break;
        case R_STANDARD_CALL_NET_UDP_BIND:
            operation_name = "std.net::udp_bind";
            break;
        case R_STANDARD_CALL_NET_UDP_SEND_TO:
            operation_name = "std.net::udp_send_to";
            break;
        case R_STANDARD_CALL_NET_UDP_RECEIVE_FROM:
            operation_name = "std.net::udp_receive_from";
            break;
        case R_STANDARD_CALL_NET_UDP_CLOSE:
            operation_name = "std.net::udp_close";
            break;
        case R_STANDARD_CALL_NET_UNIX_LISTEN:
            operation_name = "std.net::unix_listen";
            break;
        case R_STANDARD_CALL_NET_UNIX_ACCEPT:
            operation_name = "std.net::unix_accept";
            break;
        case R_STANDARD_CALL_NET_UNIX_CONNECT:
            operation_name = "std.net::unix_connect";
            break;
        case R_STANDARD_CALL_NET_UNIX_SHUTDOWN:
            operation_name = "std.net::unix_shutdown";
            break;
        case R_STANDARD_CALL_NET_UNIX_CLOSE:
            operation_name = "std.net::unix_close";
            break;
        case R_STANDARD_CALL_NET_UNIX_LISTENER_CLOSE:
            operation_name = "std.net::unix_listener_close";
            break;
        case R_STANDARD_CALL_NET_UNIX_DATAGRAM_BIND:
            operation_name = "std.net::unix_datagram_bind";
            break;
        case R_STANDARD_CALL_NET_UNIX_DATAGRAM_CONNECT:
            operation_name = "std.net::unix_datagram_connect";
            break;
        case R_STANDARD_CALL_NET_UNIX_DATAGRAM_CLOSE:
            operation_name = "std.net::unix_datagram_close";
            break;
        case R_STANDARD_CALL_IO_READ_INTO:
            operation_name = "std.io::read_into";
            break;
        case R_STANDARD_CALL_IO_WRITE_FROM:
            operation_name = "std.io::write_from";
            break;
        case R_STANDARD_CALL_IO_WRITE_ALL_FROM:
            operation_name = "std.io::write_all_from";
            break;
        case R_STANDARD_CALL_FS_READ_INTO:
            operation_name = "std.fs::read_into";
            break;
        case R_STANDARD_CALL_FS_WRITE_FROM:
            operation_name = "std.fs::write_from";
            break;
        case R_STANDARD_CALL_FS_WRITE_ALL_FROM:
            operation_name = "std.fs::write_all_from";
            break;
        case R_STANDARD_CALL_FS_READ_AT_INTO:
            operation_name = "std.fs::read_at_into";
            break;
        case R_STANDARD_CALL_FS_WRITE_ALL_AT_FROM:
            operation_name = "std.fs::write_all_at_from";
            break;
        case R_STANDARD_CALL_NET_TCP_READ_INTO:
            operation_name = "std.net::tcp_read_into";
            break;
        case R_STANDARD_CALL_NET_TCP_WRITE_FROM:
            operation_name = "std.net::tcp_write_from";
            break;
        case R_STANDARD_CALL_NET_TCP_WRITE_ALL_FROM:
            operation_name = "std.net::tcp_write_all_from";
            break;
        case R_STANDARD_CALL_NET_UDP_SEND_FROM:
            operation_name = "std.net::udp_send_from";
            break;
        case R_STANDARD_CALL_NET_UDP_RECEIVE_INTO:
            operation_name = "std.net::udp_receive_into";
            break;
        case R_STANDARD_CALL_NET_UNIX_READ_INTO:
            operation_name = "std.net::unix_read_into";
            break;
        case R_STANDARD_CALL_NET_UNIX_WRITE_FROM:
            operation_name = "std.net::unix_write_from";
            break;
        case R_STANDARD_CALL_NET_UNIX_WRITE_ALL_FROM:
            operation_name = "std.net::unix_write_all_from";
            break;
        case R_STANDARD_CALL_NET_UNIX_SEND_FROM:
            operation_name = "std.net::unix_send_from";
            break;
        case R_STANDARD_CALL_NET_UNIX_RECEIVE_INTO:
            operation_name = "std.net::unix_receive_into";
            break;
        case R_STANDARD_CALL_PROCESS_COMMAND_CREATE:
            operation_name = "std.process::command_create";
            break;
        case R_STANDARD_CALL_PROCESS_ARG:
            operation_name = "std.process::arg";
            break;
        case R_STANDARD_CALL_PROCESS_ENVIRONMENT:
            operation_name = "std.process::environment";
            break;
        case R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT:
            operation_name = "std.process::remove_environment";
            break;
        case R_STANDARD_CALL_PROCESS_CLEAR_ENVIRONMENT:
            operation_name = "std.process::clear_environment";
            break;
        case R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY:
            operation_name = "std.process::working_directory";
            break;
        case R_STANDARD_CALL_PROCESS_SET_STDIO:
            operation_name = "std.process::set_stdio";
            break;
        case R_STANDARD_CALL_PROCESS_TAKE_STDIN:
            operation_name = "std.process::take_stdin";
            break;
        case R_STANDARD_CALL_PROCESS_TAKE_STDOUT:
            operation_name = "std.process::take_stdout";
            break;
        case R_STANDARD_CALL_PROCESS_TAKE_STDERR:
            operation_name = "std.process::take_stderr";
            break;
        case R_STANDARD_CALL_PROCESS_ID:
            operation_name = "std.process::id";
            break;
        case R_STANDARD_CALL_PROCESS_EXIT:
            operation_name = "std.process::exit";
            break;
        case R_STANDARD_CALL_PROCESS_ABORT:
            operation_name = "std.process::abort";
            break;
        case R_STANDARD_CALL_PROCESS_SPAWN:
            operation_name = "std.process::spawn";
            break;
        case R_STANDARD_CALL_PROCESS_WAIT:
            operation_name = "std.process::wait";
            break;
        case R_STANDARD_CALL_PROCESS_TERMINATE:
            operation_name = "std.process::terminate";
            break;
        case R_STANDARD_CALL_SIGNAL_LISTEN:
            operation_name = "std.signal::listen";
            break;
        case R_STANDARD_CALL_SIGNAL_RAISE:
            operation_name = "std.signal::raise";
            break;
        case R_STANDARD_CALL_SIGNAL_NEXT:
            operation_name = "std.signal::next";
            break;
        case R_STANDARD_CALL_ERROR_NAME:
            operation_name = "std.error::name";
            break;
        case R_STANDARD_CALL_ERROR_DIAGNOSTIC:
            operation_name = "std.error::diagnostic";
            break;
        case R_STANDARD_CALL_THREAD_CURRENT:
            operation_name = "std.thread::current";
            break;
        case R_STANDARD_CALL_THREAD_CLONE:
            operation_name = "std.thread::clone_thread";
            break;
        case R_STANDARD_CALL_THREAD_UNPARK:
            operation_name = "std.thread::unpark";
            break;
        case R_STANDARD_CALL_THREAD_PARK:
            operation_name = "std.thread::park";
            break;
        case R_STANDARD_CALL_THREAD_YIELD_NOW:
            operation_name = "std.thread::yield_now";
            break;
        case R_STANDARD_CALL_THREAD_SLEEP_NANOSECONDS:
            operation_name = "std.thread::sleep_nanoseconds";
            break;
        case R_STANDARD_CALL_THREAD_PANIC_CATEGORY:
            operation_name = "std.thread::panic_category";
            break;
        case R_STANDARD_CALL_THREAD_PANIC_TEXT:
            operation_name = "std.thread::panic_text";
            break;
        case R_STANDARD_CALL_ERROR_ERASURE: {
            const RStandardErrorErasureDescriptor *descriptor =
                r_standard_error_erasure(node->integer_value);
            if (descriptor == NULL) {
                return false;
            }
            operation_name = descriptor->qualified_name;
            break;
        }
        case R_STANDARD_CALL_MATH_OPERATION: {
            const RStandardMathOperationDescriptor *descriptor =
                r_standard_math_operation(node->integer_value);
            if (descriptor == NULL) {
                return false;
            }
            operation_name = descriptor->name;
            break;
        }
        case R_STANDARD_CALL_MATH_ABS_F64:
            operation_name = "std.math::abs_f64";
            break;
        case R_STANDARD_CALL_MATH_SIN_F64:
            operation_name = "std.math::sin_f64";
            break;
        case R_STANDARD_CALL_STRING_CREATE:
            operation_name = "std.string::create";
            break;
        case R_STANDARD_CALL_STRING_FROM_STR:
            operation_name = "std.string::from_str";
            break;
        case R_STANDARD_CALL_STRING_FROM_BYTES:
            operation_name = "std.string::from_bytes";
            break;
        case R_STANDARD_CALL_STRING_AS_STR:
            operation_name = "std.string::as_str";
            break;
        case R_STANDARD_CALL_STRING_AS_BYTES:
            operation_name = "std.string::as_bytes";
            break;
        case R_STANDARD_CALL_STRING_INTO_BYTES:
            operation_name = "std.string::into_bytes";
            break;
        case R_STANDARD_CALL_STRING_LEN:
            operation_name = "std.string::len";
            break;
        case R_STANDARD_CALL_STRING_CAPACITY:
            operation_name = "std.string::capacity";
            break;
        case R_STANDARD_CALL_STRING_CLEAR:
            operation_name = "std.string::clear";
            break;
        case R_STANDARD_CALL_STRING_WITH_CAPACITY:
            operation_name = "std.string::with_capacity";
            break;
        case R_STANDARD_CALL_STRING_FROM_UTF8:
            operation_name = "std.string::from_utf8";
            break;
        case R_STANDARD_CALL_STRING_RESERVE:
            operation_name = "std.string::reserve";
            break;
        case R_STANDARD_CALL_STRING_APPEND_STR:
            operation_name = "std.string::append_str";
            break;
        case R_STANDARD_CALL_STRING_APPEND_UTF8:
            operation_name = "std.string::append_utf8";
            break;
        case R_STANDARD_CALL_STRING_PUSH_SCALAR:
            operation_name = "std.string::push_scalar";
            break;
        case R_STANDARD_CALL_STRING_TRUNCATE:
            operation_name = "std.string::truncate";
            break;
        case R_STANDARD_CALL_TIME_DURATION_FROM_SECONDS:
            operation_name = "std.time::duration_from_seconds";
            break;
        case R_STANDARD_CALL_TIME_DURATION_SECONDS:
            operation_name = "std.time::duration_seconds";
            break;
        case R_STANDARD_CALL_TIME_DURATION_NANOSECONDS:
            operation_name = "std.time::duration_nanoseconds";
            break;
        case R_STANDARD_CALL_TIME_DURATION_COMPARE:
            operation_name = "std.time::duration_compare";
            break;
        case R_STANDARD_CALL_TIME_DURATION_FROM_PARTS:
            operation_name = "std.time::duration_from_parts";
            break;
        case R_STANDARD_CALL_TIME_DURATION_ADD:
            operation_name = "std.time::duration_add";
            break;
        case R_STANDARD_CALL_TIME_DURATION_SUB:
            operation_name = "std.time::duration_sub";
            break;
        case R_STANDARD_CALL_TIME_DURATION_MULTIPLY:
            operation_name = "std.time::duration_multiply";
            break;
        case R_STANDARD_CALL_TIME_MONOTONIC_NOW:
            operation_name = "std.time::monotonic_now";
            break;
        case R_STANDARD_CALL_TIME_SYSTEM_NOW:
            operation_name = "std.time::system_now";
            break;
        case R_STANDARD_CALL_TIME_INSTANT_ADD:
            operation_name = "std.time::instant_add";
            break;
        case R_STANDARD_CALL_TIME_INSTANT_DURATION:
            operation_name = "std.time::instant_duration";
            break;
        case R_STANDARD_CALL_TIME_SYSTEM_ADD:
            operation_name = "std.time::system_add";
            break;
        case R_STANDARD_CALL_TIME_TO_UTC:
            operation_name = "std.time::to_utc";
            break;
        case R_STANDARD_CALL_TIME_FROM_UTC:
            operation_name = "std.time::from_utc";
            break;
        case R_STANDARD_CALL_TIME_AS_ERROR:
            operation_name = "std.time::as_error";
            break;
        case R_STANDARD_CALL_TIME_SLEEP_FOR:
            operation_name = "std.time::sleep_for";
            break;
        case R_STANDARD_CALL_TIME_SLEEP_UNTIL:
            operation_name = "std.time::sleep_until";
            break;
        case R_STANDARD_CALL_SECRET_WITH_LENGTH:
            operation_name = "std.secret::with_length";
            break;
        case R_STANDARD_CALL_SECRET_FROM_BYTES:
            operation_name = "std.secret::from_bytes";
            break;
        case R_STANDARD_CALL_SECRET_LEN:
            operation_name = "std.secret::len";
            break;
        case R_STANDARD_CALL_SECRET_AS_SLICE:
            operation_name = "std.secret::as_slice";
            break;
        case R_STANDARD_CALL_SECRET_AS_SLICE_MUT:
            operation_name = "std.secret::as_slice_mut";
            break;
        case R_STANDARD_CALL_SECRET_ZEROIZE:
            operation_name = "std.secret::zeroize";
            break;
        case R_STANDARD_CALL_SECRET_CONSTANT_TIME_EQUAL:
            operation_name = "std.secret::constant_time_equal";
            break;
        case R_STANDARD_CALL_RANDOM_FILL:
            operation_name = "std.random::fill";
            break;
        case R_STANDARD_CALL_INVALID:
        default:
            if (async_sync == NULL) {
                return false;
            }
            operation_name = async_sync->name;
            break;
        }
        if (!r_write_text(writer, user_data, " operation=") ||
            ((node->standard_operation == R_STANDARD_CALL_MATH_OPERATION) &&
             !r_write_text(writer, user_data, "std.math::")) ||
            ((async_sync != NULL) && (!r_write_text(writer, user_data, "std.") ||
                                      !r_write_text(writer, user_data, async_sync->module) ||
                                      !r_write_text(writer, user_data, "::"))) ||
            !r_write_text(writer, user_data, operation_name) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
        /* L30: the carried value type and the effect or start carrier. */
        if ((async_sync != NULL) &&
            (((node->runtime_type != R_TYPE_ID_INVALID) &&
              (!r_write_text(writer, user_data, " element=") ||
               !r_hir_write_type(context, node->runtime_type, writer, user_data))) ||
             ((node->auxiliary_type != node->runtime_type) &&
              (!r_write_text(writer, user_data, " carrier=") ||
               !r_hir_write_type(context, node->auxiliary_type, writer, user_data))))) {
            return false;
        }
        if ((node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
            (node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT) ||
            (node->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_LOAD) ||
            (node->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_STORE) ||
            (node->standard_operation == R_STANDARD_CALL_CORE_ADOPT) ||
            (node->standard_operation == R_STANDARD_CALL_CORE_RELEASE) ||
            ((node->standard_operation >= R_STANDARD_CALL_CORE_CHECKED_ADD) &&
             (node->standard_operation <= R_STANDARD_CALL_CORE_SATURATING_MUL))) {
            if (!r_write_text(writer, user_data, " pointee=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if (((node->standard_operation >= R_STANDARD_CALL_CORE_ENUM_NAME) &&
                    (node->standard_operation <= R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME)) ||
                   ((node->standard_operation >= R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE) &&
                    (node->standard_operation <= R_STANDARD_CALL_CORE_FIELD_NAME_AT))) {
            if (!r_write_text(writer, user_data, " subject=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_ARC_CLONE) ||
                   (node->standard_operation == R_STANDARD_CALL_RC_CLONE) ||
                   ((node->standard_operation >= R_STANDARD_CALL_ARC_CLONE_WEAK) &&
                    (node->standard_operation <= R_STANDARD_CALL_RC_TRY_UNWRAP))) {
            if (!r_write_text(writer, user_data, " pointee=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_ASYNC_CANCEL) ||
                   (node->standard_operation == R_STANDARD_CALL_ASYNC_DETACH)) {
            if (!r_write_text(writer, user_data, " task_result=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if (node->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING) {
            if (!r_write_text(writer, user_data, " start_contract=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
                   (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
                   (node->standard_operation == R_STANDARD_CALL_THREAD_JOIN) ||
                   (node->standard_operation == R_STANDARD_CALL_THREAD_DETACH)) {
            if (!r_write_text(writer, user_data, " thread_contract=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
            if ((node->standard_operation == R_STANDARD_CALL_THREAD_JOIN) &&
                (!r_write_text(writer, user_data, " completion_storage=") ||
                 !r_hir_write_type(context, node->runtime_type, writer, user_data))) {
                return false;
            }
            if (((node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
                 (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED)) &&
                ((symbol == NULL) || !r_write_text(writer, user_data, " entry=") ||
                 !r_hir_write_symbol_name(context, symbol, writer, user_data))) {
                return false;
            }
            if ((node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) &&
                (!r_write_text(writer, user_data, " region=") ||
                 !r_hir_write_uint64(writer, user_data, node->integer_value))) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE) ||
                   (node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE) ||
                   (node->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT)) {
            if ((symbol == NULL) || !r_write_text(writer, user_data, " initializer=") ||
                !r_hir_write_symbol_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, " sync_contract=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data) ||
                !r_write_text(writer, user_data, " callback_storage=") ||
                !r_hir_write_type(context, node->runtime_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_SYNC_CHANNEL) ||
                   (node->standard_operation == R_STANDARD_CALL_SYNC_SYNC_CHANNEL)) {
            if (!r_write_text(writer, user_data, " effect_carrier=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data) ||
                !r_write_text(writer, user_data, " element=") ||
                !r_hir_write_type(context, node->runtime_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_SYNC_SENDER) ||
                   (node->standard_operation == R_STANDARD_CALL_SYNC_SYNC_SENDER) ||
                   (node->standard_operation == R_STANDARD_CALL_SYNC_RECEIVER)) {
            if (!r_write_text(writer, user_data, " element=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if (node->standard_operation == R_STANDARD_CALL_IO_STDIN) {
            if (!r_write_text(writer, user_data, " input=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_IO_STDOUT) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_STDERR)) {
            if (!r_write_text(writer, user_data, " output=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE) ||
                   (node->standard_operation == R_STANDARD_CALL_FS_AS_ERROR) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_AS_ERROR)) {
            if (!r_write_text(writer, user_data, " input=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_FS_READ_FILE) ||
                   (node->standard_operation == R_STANDARD_CALL_FS_OPEN_DIRECTORY) ||
                   (node->standard_operation == R_STANDARD_CALL_FS_OPEN_FILE) ||
                   (node->standard_operation == R_STANDARD_CALL_FS_FILE_METADATA) ||
                   (r_standard_fs_async_descriptor(node->standard_operation) != NULL) ||
                   (node->standard_operation == R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) ||
                   (node->standard_operation ==
                    R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_READ) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_FLUSH) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_CLOSE_INPUT) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_CLOSE_OUTPUT) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_WRITE) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_WRITE_ALL) ||
                   (node->standard_operation == R_STANDARD_CALL_IO_WRITE_SHARED) ||
                   (node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_FOR) ||
                   (node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_UNTIL)) {
            if (!r_write_text(writer, user_data, " task=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((node->standard_operation == R_STANDARD_CALL_CONVERT_PARSE) ||
                   (node->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_INTEGER) ||
                   (node->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED) ||
                   (node->standard_operation == R_STANDARD_CALL_C_CHECKED)) {
            if (!r_write_text(writer, user_data, " destination=") ||
                !r_hir_write_type(context, node->auxiliary_type, writer, user_data) ||
                !r_write_text(writer, user_data, " tag=") ||
                !r_hir_write_uint64(writer, user_data, node->integer_value)) {
                return false;
            }
        } else if (!r_write_text(
                       writer,
                       user_data,
                       (node->standard_operation == R_STANDARD_CALL_ARRAY_CREATE) ||
                               (node->standard_operation == R_STANDARD_CALL_ARRAY_PUSH) ||
                               (node->standard_operation == R_STANDARD_CALL_ARRAY_WITH_CAPACITY) ||
                               (node->standard_operation == R_STANDARD_CALL_ARRAY_FILLED)
                           ? " element="
                           : " input=") ||
                   !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
            return false;
        }
    } else if ((node->kind == R_HIR_LOCAL) || (node->kind == R_HIR_PLACE) ||
               (node->kind == R_HIR_CALL) || (node->kind == R_HIR_FUNCTION_ADDRESS) ||
               (node->kind == R_HIR_ASYNC_START)) {
        if (!r_hir_write_symbol_attribute(context, node, symbol, writer, user_data) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
        if ((node->kind == R_HIR_ASYNC_START) &&
            (!r_write_text(writer, user_data, " task=") ||
             !r_hir_write_type(context, node->auxiliary_type, writer, user_data))) {
            return false;
        }
        if ((node->kind == R_HIR_LOCAL) && (symbol != NULL) && symbol->poisoned &&
            !r_write_text(writer, user_data, " poisoned")) {
            return false;
        }
    } else if (node->kind == R_HIR_STRING_LITERAL) {
        const RInternEntry *entry =
            (node->aggregate_member == UINT32_C(0)) ||
                    ((size_t)node->aggregate_member > context->intern_count)
                ? NULL
                : &context->intern_entries[(size_t)node->aggregate_member - 1U];
        if ((entry == NULL) || (entry->length != (size_t)node->integer_value) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " value=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)entry->bytes, entry->length)) {
            return false;
        }
    } else if (node->kind == R_HIR_TYPE_QUERY) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          node->operation == R_TOKEN_KW_SIZEOF ? " sizeof=" : " alignof=") ||
            !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
            return false;
        }
    } else if ((node->kind == R_HIR_LITERAL) || (node->kind == R_HIR_ENUM_CONSTANT)) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " value=") ||
            !r_hir_write_uint64(writer, user_data, node->integer_value) ||
            ((node->kind == R_HIR_ENUM_CONSTANT) &&
             (!r_write_text(writer, user_data, " variant=") ||
              !r_write_uint32(writer, user_data, node->aggregate_member)))) {
            return false;
        }
    } else if ((node->kind == R_HIR_FIELD_PLACE) || (node->kind == R_HIR_FIELD_INIT)) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " field=") ||
            !r_write_uint32(writer, user_data, node->aggregate_member)) {
            return false;
        }
    } else if (node->kind == R_HIR_INDEX_PLACE) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          node->integer_value == UINT64_MAX ? " bound=dynamic" : " bound=") ||
            ((node->integer_value != UINT64_MAX) &&
             !r_hir_write_uint64(writer, user_data, node->integer_value)) ||
            (node->aggregate_member != UINT32_C(0) &&
             !r_write_text(writer, user_data, " readonly"))) {
            return false;
        }
    } else if ((node->kind == R_HIR_DEFAULT_VALUE) || (node->kind == R_HIR_AGGREGATE_INIT) ||
               (node->kind == R_HIR_ARRAY_INIT) || (node->kind == R_HIR_SLICE)) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            ((node->kind == R_HIR_ARRAY_INIT) &&
             (!r_write_text(writer, user_data, " length=") ||
              !r_hir_write_uint64(writer, user_data, node->integer_value))) ||
            ((node->kind == R_HIR_SLICE) && (node->operation == R_TOKEN_KW_ARRAY) &&
             (!r_write_text(writer, user_data, " source=std.array input=") ||
              !r_hir_write_type(context, node->auxiliary_type, writer, user_data))) ||
            ((node->kind == R_HIR_SLICE) && (node->child_count == UINT32_C(1)) &&
             (node->operation != R_TOKEN_KW_ARRAY) &&
             (!r_write_text(writer, user_data, " length=") ||
              !r_hir_write_uint64(writer, user_data, node->integer_value))) ||
            ((node->kind == R_HIR_SLICE) &&
             ((node->child_count == UINT32_C(3)) || (node->child_count == UINT32_C(2))) &&
             (!r_write_text(writer,
                            user_data,
                            node->integer_value == UINT64_MAX ? " bound=dynamic checked=true"
                                                              : " bound=") ||
              ((node->integer_value != UINT64_MAX) &&
               (!r_hir_write_uint64(writer, user_data, node->integer_value) ||
                !r_write_text(writer, user_data, " checked=true")))))) {
            return false;
        }
    } else if (node->kind == R_HIR_LENGTH) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          node->integer_value == UINT64_MAX ? " bound=dynamic" : " bound=") ||
            ((node->integer_value != UINT64_MAX) &&
             !r_hir_write_uint64(writer, user_data, node->integer_value))) {
            return false;
        }
    } else if (node->kind == R_HIR_VARIANT) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " tag=") ||
            !r_hir_write_uint64(writer, user_data, node->integer_value)) {
            return false;
        }
    } else if (node->kind == R_HIR_CAST) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(
                writer, user_data, node->operation == R_TOKEN_KW_AS ? " explicit" : " implicit")) {
            return false;
        }
    } else if (node->kind == R_HIR_MOVE) {
        if (!r_hir_write_symbol_attribute(context, node, symbol, writer, user_data) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
    } else if (node->kind == R_HIR_AWAIT) {
        if (!r_hir_write_symbol_attribute(context, node, symbol, writer, user_data) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " task=") ||
            !r_hir_write_type(context, node->auxiliary_type, writer, user_data) ||
            !r_write_text(writer, user_data, " consuming")) {
            return false;
        }
    } else if (node->kind == R_HIR_DROP) {
        if (!r_hir_write_symbol_attribute(context, node, symbol, writer, user_data)) {
            return false;
        }
    } else if ((node->kind == R_HIR_UNARY) || (node->kind == R_HIR_BINARY) ||
               (node->kind == R_HIR_CONDITIONAL) || (node->kind == R_HIR_ASSIGN) ||
               (node->kind == R_HIR_COMPOUND_ASSIGN)) {
        if (!r_hir_write_operation_attribute(node->operation, writer, user_data) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
    } else if ((node->kind == R_HIR_BLOCK) || (node->kind == R_HIR_RETURN) ||
               (node->kind == R_HIR_DEREF_PLACE) || (node->kind == R_HIR_BORROW) ||
               (node->kind == R_HIR_LOAD) || (node->kind == R_HIR_DISCARD) ||
               (node->kind == R_HIR_EXPRESSION_STATEMENT) || (node->kind == R_HIR_IF) ||
               (node->kind == R_HIR_STATIC_IF) || (node->kind == R_HIR_WHILE) ||
               (node->kind == R_HIR_FOR) || (node->kind == R_HIR_BREAK) ||
               (node->kind == R_HIR_CONTINUE) || (node->kind == R_HIR_SWITCH)) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
        /* R-STMT-0004: the loop a labeled jump targets. */
        if ((node->loop_target != 0U) &&
            (!r_write_text(writer, user_data, " loop=") ||
             !r_hir_write_uint64(writer, user_data, node->loop_target))) {
            return false;
        }
    } else if (node->kind == R_HIR_TRY) {
        if (!r_hir_write_operation_attribute(node->operation, writer, user_data) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data) ||
            !r_write_text(writer, user_data, " propagate=") ||
            !r_hir_write_type(context, node->auxiliary_type, writer, user_data)) {
            return false;
        }
    } else if (node->kind == R_HIR_CATCH) {
        if (!r_hir_write_symbol_attribute(context, node, symbol, writer, user_data) ||
            !r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
    } else if (node->kind == R_HIR_FINALLY) {
        if (!r_hir_write_type_attribute(context, node->type, writer, user_data)) {
            return false;
        }
    } else if (node->kind == R_HIR_THROW) {
        if (!r_write_text(writer, user_data, " error=") ||
            !r_hir_write_type(context, node->auxiliary_type, writer, user_data) ||
            !r_write_text(
                writer, user_data, node->integer_value == UINT64_C(0) ? " caught" : " propagate")) {
            return false;
        }
    } else if (node->kind == R_HIR_CASE) {
        if (!r_write_text(writer, user_data, " pattern=") ||
            !r_write_text(writer, user_data, r_hir_case_pattern_name(node->case_pattern)) ||
            ((node->case_pattern != R_HIR_CASE_PATTERN_DEFAULT) &&
             (!r_write_text(writer, user_data, " value=") ||
              !r_hir_write_uint64(writer, user_data, node->integer_value))) ||
            ((node->case_pattern == R_HIR_CASE_PATTERN_CONSTANT) &&
             (!r_write_text(writer, user_data, " pattern_type=") ||
              !r_hir_write_type(context, node->auxiliary_type, writer, user_data))) ||
            !r_hir_write_operation_attribute(node->operation, writer, user_data) ||
            ((node->symbol != R_SYMBOL_ID_INVALID) &&
             (!r_hir_write_symbol_attribute(context, node, symbol, writer, user_data) ||
              !r_hir_write_type_attribute(context, node->type, writer, user_data))) ||
            (node->is_move && !r_write_text(writer, user_data, " binding=move"))) {
            return false;
        }
    }
    if ((node->borrow_origin != R_SYMBOL_ID_INVALID) && (node->borrow_origin != node->symbol) &&
        (!r_write_text(writer, user_data, " borrow_origin=") ||
         !r_write_uint32(writer, user_data, node->borrow_origin))) {
        return false;
    }
    if (node->child_count == UINT32_C(0)) {
        return r_write_text(writer, user_data, ")\n");
    }
    if (!r_write_text(writer, user_data, "\n")) {
        return false;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        size_t index = (size_t)node->first_child + (size_t)child_index;
        if ((index >= context->hir_child_count) ||
            !r_hir_dump_node(
                context, context->hir_children[index], depth + 1U, writer, user_data)) {
            return false;
        }
    }
    return r_hir_write_indent(writer, user_data, depth) && r_write_text(writer, user_data, ")\n");
}

RFrontendStatus
r_frontend_dump_hir(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data) {
    size_t type_index;

    if ((context == NULL) || (writer == NULL) || !context->semantic_analyzed ||
        (context->hir_root == R_HIR_NODE_ID_INVALID)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_write_text(writer, user_data, "(types\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    for (type_index = 0U; type_index < context->semantic_type_count; ++type_index) {
        if (!r_hir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(type ") ||
            !r_write_uint32(writer, user_data, (uint32_t)(type_index + 1U)) ||
            !r_write_text(writer, user_data, " ") ||
            !r_hir_write_type(context, (RTypeId)(type_index + 1U), writer, user_data) ||
            !r_write_text(writer, user_data, ")\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (!r_write_text(writer, user_data, ")\n") ||
        !r_hir_dump_node(context, context->hir_root, UINT32_C(0), writer, user_data)) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}
