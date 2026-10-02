/*
 * Translation-time evaluation (Core R-FUNC-0023, R-EXPR-0032).
 *
 * An ordinary R function is translation-time evaluable when its checked body stays inside a
 * closed subset: values of constant value types (integers, bool, char, fieldless enumerations,
 * string literals, fixed arrays and plain structs of them), borrows and slices of such storage,
 * local control flow, panics and calls of other evaluable functions. A body that reaches an
 * object with static or thread storage, allocates, starts a task, crosses the C ABI or has any
 * other effect keeps the function a run-time function; there is no attribute. The evaluator
 * interprets the checked HIR with the numeric rules of the abstract machine and the layout of
 * the selected target. The static call graph is acyclic (R-FUNC-0004), so a function has at most
 * one activation and its locals are addressed by symbol.
 */

enum {
    R_CONSTEVAL_STEP_LIMIT = 4000000,
    R_CONSTEVAL_MEMORY_LIMIT = 64 * 1024 * 1024,
    R_CONSTEVAL_BLOCK_SIZE = 64 * 1024,
    R_CONSTEVAL_ALIGNMENT = 16,
    R_CONSTEVAL_INLINE_CELLS = 256
};

typedef enum RConstevalReason {
    R_CONSTEVAL_REASON_NONE = 0,
    R_CONSTEVAL_REASON_NO_BODY,
    R_CONSTEVAL_REASON_ASYNC,
    R_CONSTEVAL_REASON_UNSAFE,
    R_CONSTEVAL_REASON_THROWS,
    R_CONSTEVAL_REASON_CALLABLE,
    R_CONSTEVAL_REASON_SIGNATURE,
    R_CONSTEVAL_REASON_STATIC_STORAGE,
    R_CONSTEVAL_REASON_TYPE,
    R_CONSTEVAL_REASON_OPERATION,
    R_CONSTEVAL_REASON_CALLEE,
    R_CONSTEVAL_REASON_RECURSION,
    R_CONSTEVAL_REASON_ILL_FORMED
} RConstevalReason;

static const char *r_consteval_reason_text(RConstevalReason reason) {
    switch (reason) {
    case R_CONSTEVAL_REASON_NO_BODY:
        return "has no R body available to translation";
    case R_CONSTEVAL_REASON_ASYNC:
        return "is asynchronous";
    case R_CONSTEVAL_REASON_UNSAFE:
        return "is unsafe";
    case R_CONSTEVAL_REASON_THROWS:
        return "declares a checked error outside the constant value types";
    case R_CONSTEVAL_REASON_CALLABLE:
        return "is a lambda body, a callback or a compiler-made body";
    case R_CONSTEVAL_REASON_SIGNATURE:
        return "has a parameter or result outside the constant value types";
    case R_CONSTEVAL_REASON_STATIC_STORAGE:
        return "accesses an object with static or thread storage duration";
    case R_CONSTEVAL_REASON_TYPE:
        return "uses a value outside the constant value types";
    case R_CONSTEVAL_REASON_OPERATION:
        return "performs an operation with an effect outside translation-time evaluation";
    case R_CONSTEVAL_REASON_CALLEE:
        return "calls a function that is not translation-time evaluable";
    case R_CONSTEVAL_REASON_RECURSION:
        return "is part of a recursive call chain";
    case R_CONSTEVAL_REASON_ILL_FORMED:
        return "has an ill-formed body";
    default:
        return "is not translation-time evaluable";
    }
}

static RSemanticSymbol *r_consteval_symbol(RFrontendContext *context, RSymbolId symbol_id) {
    if ((symbol_id == R_SYMBOL_ID_INVALID) ||
        ((size_t)symbol_id > context->semantic_symbol_count)) {
        return NULL;
    }
    return &context->semantic_symbols[(size_t)symbol_id - 1U];
}

static bool
r_consteval_node_copy(const RFrontendContext *context, RHirNodeId node_id, RHirNode *node) {
    if ((node_id == R_HIR_NODE_ID_INVALID) || ((size_t)node_id > context->hir_node_count)) {
        return false;
    }
    *node = context->hir_nodes[(size_t)node_id - 1U];
    return true;
}

static RHirNodeId
r_consteval_child(const RFrontendContext *context, const RHirNode *node, uint32_t index) {
    const size_t position = (size_t)node->first_child + (size_t)index;
    if ((index >= node->child_count) || (position >= context->hir_child_count)) {
        return R_HIR_NODE_ID_INVALID;
    }
    return context->hir_children[position];
}

/* ---- constant value types ---- */

/* Integer representation used by arithmetic, comparison and conversion of a scalar type. */
static RSemanticTypeKind r_consteval_integer_kind(const RFrontendContext *context,
                                                  RTypeId type_id) {
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);
    const RSemanticAggregate *aggregate;

    if (type == NULL) {
        return R_SEMANTIC_TYPE_INVALID;
    }
    if (type->kind == R_SEMANTIC_TYPE_CHAR) {
        return R_SEMANTIC_TYPE_U32;
    }
    if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        aggregate = r_semantic_aggregate_for_type(context, value_id);
        if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_ENUM) ||
            aggregate->is_tagged) {
            return R_SEMANTIC_TYPE_INVALID;
        }
        return r_semantic_integer_representation(context, aggregate->enum_underlying_type);
    }
    return r_semantic_integer_representation(context, value_id);
}

static const RSemanticAggregate *r_consteval_fieldless_enum(const RFrontendContext *context,
                                                            RTypeId type_id) {
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);
    const RSemanticAggregate *aggregate;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_ENUM)) {
        return NULL;
    }
    aggregate = r_semantic_aggregate_for_type(context, value_id);
    return (aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
                   !aggregate->is_tagged && !aggregate->poisoned
               ? aggregate
               : NULL;
}

/* R-TYPE-0006 (L22.2): the IEC 60559 width of a floating type, 32 or 64, or zero for any other
 * type. A long double of the target that is not binary64 (binary128, x87 extended) is not
 * emulated during translation, so it keeps its operations at run time. */
static uint32_t r_consteval_float_width(const RFrontendContext *context, RTypeId type_id) {
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);
    uint64_t size = 0U;
    uint64_t alignment = 0U;

    if (type == NULL) {
        return 0U;
    }
    if (type->kind == R_SEMANTIC_TYPE_F32) {
        return 32U;
    }
    if (type->kind == R_SEMANTIC_TYPE_F64) {
        return 64U;
    }
    if ((type->kind != R_SEMANTIC_TYPE_C_ABI_NUMERIC) ||
        (r_semantic_c_abi_float_rank(context, value_id) == 0U) ||
        !r_generic_layout(context, value_id, &size, &alignment, 0U)) {
        return 0U;
    }
    return size == 4U ? 32U : (size == 8U ? 64U : 0U);
}

static bool
r_consteval_value_type(const RFrontendContext *context, RTypeId type_id, uint32_t depth);

/* L22.3: the standard owners evaluated during translation (Library R-LIB-0019..0023). */
typedef enum RConstevalOwner {
    R_CONSTEVAL_OWNER_NONE = 0,
    R_CONSTEVAL_OWNER_ARRAY,
    R_CONSTEVAL_OWNER_LIST,
    R_CONSTEVAL_OWNER_DICT,
    R_CONSTEVAL_OWNER_STRING
} RConstevalOwner;

static RConstevalOwner r_consteval_owner(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));

    if (type == NULL) {
        return R_CONSTEVAL_OWNER_NONE;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARRAY:
        return R_CONSTEVAL_OWNER_ARRAY;
    case R_SEMANTIC_TYPE_LIST:
        return R_CONSTEVAL_OWNER_LIST;
    case R_SEMANTIC_TYPE_DICT:
        return R_CONSTEVAL_OWNER_DICT;
    case R_SEMANTIC_TYPE_STANDARD:
        return r_semantic_standard_type_name_equal(context, type, "std.string::string")
                   ? R_CONSTEVAL_OWNER_STRING
                   : R_CONSTEVAL_OWNER_NONE;
    default:
        return R_CONSTEVAL_OWNER_NONE;
    }
}

/* A dictionary key with the predefined contract of R-LIB-0020 that translation reproduces:
 * booleans, characters, integers, fieldless enumerations, floating values and strings. */
static bool r_consteval_key_type(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type_id);

    return (kind == R_SEMANTIC_TYPE_BOOL) || (kind == R_SEMANTIC_TYPE_CHAR) ||
           (kind == R_SEMANTIC_TYPE_STR) || (kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) ||
           (r_consteval_integer_kind(context, type_id) != R_SEMANTIC_TYPE_INVALID) ||
           (r_consteval_float_width(context, type_id) != 0U) ||
           (r_consteval_owner(context, type_id) == R_CONSTEVAL_OWNER_STRING);
}

/* The standard errors of the evaluated operations: 1 for an allocation failure, which never
 * happens during translation, 2 for a string or boundary error, thrown without an observable
 * value, and 0 for any other type. */
static int r_consteval_standard_error(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));

    if (r_semantic_standard_type_name_equal(context, type, "std.alloc::alloc_error") ||
        r_semantic_standard_type_name_equal(context, type, "std.array::push_error") ||
        r_semantic_standard_type_name_equal(context, type, "std.list::push_error") ||
        r_semantic_standard_type_name_equal(context, type, "std.dict::insert_error")) {
        return 1;
    }
    if (r_semantic_standard_type_name_equal(context, type, "std.string::string_error") ||
        r_semantic_standard_type_name_equal(context, type, "std.string::boundary_error") ||
        r_semantic_standard_type_name_equal(context, type, "std.math::math_error")) {
        return 2;
    }
    return 0;
}

/* L22.3: a tagged enumeration whose payloads are constant value types, including the family of
 * a user error (L22.1, R-AGG-0011); the family of the standard errors is not evaluated. */
static const RSemanticAggregate *
r_consteval_tagged_enum(const RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);
    const RSemanticAggregate *tagged;
    uint32_t tag;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_ENUM) ||
        (depth > context->options.limits.max_nesting)) {
        return NULL;
    }
    tagged = r_semantic_tagged_enum(context, value_id);
    if ((tagged == NULL) || !tagged->complete || tagged->poisoned ||
        (tagged->error_family_of == R_SEMANTIC_STANDARD_ERROR_ROOT) ||
        (tagged->drop_function != R_SYMBOL_ID_INVALID)) {
        return NULL;
    }
    for (tag = 0U; tag < tagged->variant_count; ++tag) {
        const RTypeId payload =
            context->semantic_variants[(size_t)tagged->first_variant + tag].payload_type;
        if ((payload != R_TYPE_ID_INVALID) &&
            !r_consteval_value_type(context, payload, depth + 1U)) {
            return NULL;
        }
    }
    return tagged;
}

static bool r_consteval_storage_type(const RFrontendContext *context, RTypeId type_id);

/* L22.3: `o<T>` of a constant value type; as storage, also of a borrow designating one. */
static bool r_consteval_option_type(const RFrontendContext *context,
                                    RTypeId type_id,
                                    bool storage,
                                    uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_OPTION) &&
           (depth <= context->options.limits.max_nesting) &&
           (storage ? r_consteval_storage_type(context, type->base)
                    : r_consteval_value_type(context, type->base, depth + 1U));
}

/* The tagged type of a variant node or of the borrow a variant test reads through. */
static bool r_consteval_tagged_type(const RFrontendContext *context, RTypeId type_id) {
    return (r_consteval_tagged_enum(context, type_id, 0U) != NULL) ||
           r_consteval_option_type(context, type_id, true, 0U);
}

/* R-FUNC-0023: integers, floating values of an IEC 60559 binary32 or binary64 format, bool,
 * char, fieldless enumerations, strings backed by literals, and fixed arrays and drop-free
 * structs whose elements and fields are constant value types. */
static bool
r_consteval_value_type(const RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);
    const RSemanticAggregate *aggregate;
    uint32_t field_index;

    if ((type == NULL) || (depth > context->options.limits.max_nesting) ||
        r_generic_type_is_dependent(context, value_id, 0U)) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BOOL:
    case R_SEMANTIC_TYPE_CHAR:
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
        return true;
    case R_SEMANTIC_TYPE_ENUM:
        return (r_consteval_fieldless_enum(context, value_id) != NULL) ||
               (r_consteval_tagged_enum(context, value_id, depth) != NULL);
    case R_SEMANTIC_TYPE_OPTION:
        return r_consteval_option_type(context, value_id, false, depth);
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
        /* L22.3: the standard owners of constant values. */
        return r_consteval_value_type(context, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_DICT:
        return r_consteval_key_type(context, type->base) &&
               r_consteval_value_type(context, type->second, depth + 1U);
    case R_SEMANTIC_TYPE_STANDARD:
        return r_consteval_owner(context, value_id) == R_CONSTEVAL_OWNER_STRING;
    case R_SEMANTIC_TYPE_STRUCT:
        aggregate = r_semantic_aggregate_for_type(context, value_id);
        if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
            !aggregate->complete || aggregate->poisoned || aggregate->is_opaque ||
            aggregate->is_closure || (aggregate->drop_function != R_SYMBOL_ID_INVALID)) {
            return false;
        }
        for (field_index = 0U; field_index < aggregate->field_count; ++field_index) {
            const RSemanticField *field =
                &context->semantic_fields[(size_t)aggregate->first_field + field_index];
            if (!r_consteval_value_type(context, field->type, depth + 1U)) {
                return false;
            }
        }
        return true;
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return (type->length != 0U) && (type->second == R_TYPE_ID_INVALID) &&
               r_consteval_value_type(context, type->base, depth + 1U);
    default:
        return (r_consteval_integer_kind(context, value_id) != R_SEMANTIC_TYPE_INVALID) ||
               (r_consteval_float_width(context, value_id) != 0U);
    }
}

/* Storage of an evaluable body: constant values, and borrows or slices designating them. */
static bool
r_consteval_storage_type_at(const RFrontendContext *context, RTypeId type_id, uint32_t depth);

static bool r_consteval_storage_type(const RFrontendContext *context, RTypeId type_id) {
    return r_consteval_storage_type_at(context, type_id, 0U);
}

static bool
r_consteval_storage_type_at(const RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_BORROW) {
        /* L22.3: also a borrow of an optional borrow or of a cursor, as a match reads it. */
        return ((type->flags & (uint32_t)R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) &&
               r_consteval_storage_type_at(context, type->base, depth + 1U);
    }
    if (type->kind == R_SEMANTIC_TYPE_SLICE) {
        return ((type->flags & (uint32_t)R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) &&
               r_consteval_value_type(context, type->base, 0U);
    }
    if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        /* An optional borrow, as a lookup yields it (L22.3). */
        return r_consteval_option_type(context, value_id, true, 0U);
    }
    if (r_semantic_standard_type_name_equal(context, type, "std.dict::iter") ||
        r_semantic_standard_type_name_equal(context, type, "std.dict::entry_ref")) {
        /* The cursor of a dictionary and the entries it yields (R-LIB-0021). */
        return r_consteval_key_type(context, type->base) &&
               r_consteval_value_type(context, type->second, 0U);
    }
    if (r_semantic_standard_type_name_equal(context, type, "std.list::iter")) {
        return r_consteval_value_type(context, type->base, 0U);
    }
    return r_consteval_value_type(context, value_id, 0U);
}

static bool r_consteval_node_type(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type_id);
    return (type_id == R_TYPE_ID_INVALID) || (kind == R_SEMANTIC_TYPE_VOID) ||
           (kind == R_SEMANTIC_TYPE_NEVER) || r_consteval_storage_type(context, type_id);
}

/* A synchronous direct call; a call of a throwing function names its result carrier. */
static bool r_consteval_direct_call(const RSemanticSymbol *callee, const RHirNode *node) {
    return (node->auxiliary_type == R_TYPE_ID_INVALID) ||
           (!callee->is_async && (node->auxiliary_type == callee->effect_carrier_type));
}

/* L22.1: a checked-error set whose errors are all constant value types. */
static bool r_consteval_error_set(const RFrontendContext *context, RTypeId effect_set) {
    const uint32_t count = r_semantic_effect_count(context, effect_set);
    uint32_t index;

    for (index = 0U; index < count; ++index) {
        const RTypeId error = r_semantic_effect_at(context, effect_set, index);
        if (!r_consteval_value_type(context, error, 0U) &&
            (r_consteval_standard_error(context, error) == 0)) {
            return false;
        }
    }
    return true;
}

/* Drops that leave a scope destroy nothing the evaluation observes (L22.3). */
static bool r_consteval_drops_only(const RFrontendContext *context, RHirNodeId node_id) {
    RHirNode node;
    uint32_t index;

    if (!r_consteval_node_copy(context, node_id, &node)) {
        return false;
    }
    if (node.kind == R_HIR_DROP) {
        return true;
    }
    if (node.kind != R_HIR_BLOCK) {
        return false;
    }
    for (index = 0U; index < node.child_count; ++index) {
        if (!r_consteval_drops_only(context, r_consteval_child(context, &node, index))) {
            return false;
        }
    }
    return true;
}

/* ---- evaluability ---- */

typedef struct RConstevalVerdict {
    RConstevalReason reason;
    RHirNodeId blocker;
    RSymbolId callee;
} RConstevalVerdict;

static bool r_consteval_classify(RFrontendContext *context, RSymbolId function_id, uint32_t depth);
static bool r_consteval_prepare_module_object(RFrontendContext *context, RSymbolId symbol_id);
static bool r_consteval_resolve_module_constant(
    RFrontendContext *context, RSymbolId symbol_id, RSourceSpan use_span, bool report, bool *known);
static bool r_consteval_record_failure(RFrontendContext *context,
                                       RSourceId source,
                                       RAstNodeId node,
                                       uint32_t instance,
                                       size_t first_diagnostic,
                                       RSourceSpan span,
                                       const char *reason);

static void r_consteval_block(RConstevalVerdict *verdict,
                              RConstevalReason reason,
                              RHirNodeId blocker,
                              RSymbolId callee) {
    if (verdict->reason == R_CONSTEVAL_REASON_NONE) {
        verdict->reason = reason;
        verdict->blocker = blocker;
        verdict->callee = callee;
    }
}

/* Builds the body of a function on demand, like call lowering does for borrowed outputs. */
static bool
r_consteval_ensure_body(RFrontendContext *context, RSymbolId function_id, RHirNodeId *body) {
    RSemanticSymbol *function = r_consteval_symbol(context, function_id);
    RHirNodeId built = R_HIR_NODE_ID_INVALID;
    bool bindings_incomplete = false;

    *body = R_HIR_NODE_ID_INVALID;
    if (function == NULL) {
        return true;
    }
    if (function->generic_origin != R_SYMBOL_ID_INVALID) {
        const RSymbolId origin_id = function->generic_origin;
        RSemanticSymbol *origin = r_consteval_symbol(context, origin_id);

        if ((origin == NULL) || origin->hir_building || function->hir_building) {
            return true;
        }
        context->consteval_builds += 1U;
        if ((origin->hir_node == R_HIR_NODE_ID_INVALID) &&
            (!r_semantic_source_bindings_incomplete(
                 context, origin->module_source, &bindings_incomplete) ||
             !r_hir_build_function(context, origin_id, bindings_incomplete, &built))) {
            return context->resource_status == R_FRONTEND_OK;
        }
        if (!r_generic_close_function(context, function_id)) {
            return context->resource_status == R_FRONTEND_OK;
        }
        function = r_consteval_symbol(context, function_id);
        if ((function == NULL) || r_generic_function_is_open(context, function)) {
            return true;
        }
        *body = function->hir_node;
        return true;
    }
    if (function->hir_node != R_HIR_NODE_ID_INVALID) {
        *body = function->hir_node;
        return true;
    }
    if (function->hir_building || !function->has_definition) {
        return true;
    }
    context->consteval_builds += 1U;
    if (!r_semantic_source_bindings_incomplete(
            context, function->module_source, &bindings_incomplete) ||
        !r_hir_build_function(context, function_id, bindings_incomplete, &built)) {
        return context->resource_status == R_FRONTEND_OK;
    }
    *body = built;
    return true;
}

static bool r_consteval_standard_operation_allowed(RStandardCallOperation operation) {
    switch (operation) {
    case R_STANDARD_CALL_CORE_PANIC:
    case R_STANDARD_CALL_CORE_WRAPPING_ADD:
    case R_STANDARD_CALL_CORE_WRAPPING_SUB:
    case R_STANDARD_CALL_CORE_WRAPPING_MUL:
    case R_STANDARD_CALL_CORE_SATURATING_ADD:
    case R_STANDARD_CALL_CORE_SATURATING_SUB:
    case R_STANDARD_CALL_CORE_SATURATING_MUL:
    case R_STANDARD_CALL_CORE_ENUM_NAME:
    case R_STANDARD_CALL_CORE_ENUM_ORDINAL:
        return true;
    default:
        return false;
    }
}

/* L22.2: an exact non-failing std.math operation over an evaluable floating format. */
static bool r_consteval_math_base(const char *name, char *base, size_t capacity);
static bool r_consteval_math_exact(const char *name);
static bool r_consteval_math_checked(const char *name);
static bool r_consteval_owner_operation(RStandardCallOperation operation);
static bool r_consteval_string_recipe(const RFrontendContext *context,
                                      const RSemanticSymbol *function);

/* The name of an exact std.math operation, also for the dedicated abs_f64 call. */
static const char *r_consteval_math_name(const RHirNode *node) {
    const RStandardMathOperationDescriptor *descriptor =
        node->standard_operation == R_STANDARD_CALL_MATH_OPERATION
            ? r_standard_math_operation(node->integer_value)
            : NULL;

    if (node->standard_operation == R_STANDARD_CALL_MATH_ABS_F64) {
        return node->child_count == 1U ? "abs_f64" : NULL;
    }
    if ((descriptor == NULL) ||
        (descriptor->checked ? !r_consteval_math_checked(descriptor->name)
                             : !r_consteval_math_exact(descriptor->name)) ||
        (node->child_count == 0U) || (node->child_count != descriptor->parameter_count)) {
        return NULL;
    }
    return descriptor->name;
}

static bool r_consteval_math_allowed(const RFrontendContext *context, const RHirNode *node) {
    const RHirNode *operand;

    if (r_consteval_math_name(node) == NULL) {
        return false;
    }
    operand = &context->hir_nodes[(size_t)r_consteval_child(context, node, 0U) - 1U];
    return r_consteval_float_width(context, operand->type) != 0U;
}

/* The cast forms the evaluator reproduces: integer, char, bool and enum conversions and the
 * view of a string literal as str. */
static bool
r_consteval_cast_allowed(const RFrontendContext *context, RTypeId source, RTypeId target) {
    const RSemanticTypeKind source_kind = r_semantic_value_kind(context, source);
    const RSemanticTypeKind target_kind = r_semantic_value_kind(context, target);

    if (r_semantic_value_type(context, source) == r_semantic_value_type(context, target)) {
        return true;
    }
    if ((source_kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) && (target_kind == R_SEMANTIC_TYPE_STR)) {
        return true;
    }
    if (((source_kind == R_SEMANTIC_TYPE_STR) || (source_kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)) &&
        (target_kind == R_SEMANTIC_TYPE_SLICE)) {
        /* L22.1: the bytes of a string as a read-only slice, as indexing a string does. */
        const RSemanticType *slice =
            r_semantic_type(context, r_semantic_value_type(context, target));
        return (slice != NULL) && ((slice->flags & (uint32_t)R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) &&
               ((slice->flags & (uint32_t)R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) &&
               (r_semantic_value_kind(context, slice->base) == R_SEMANTIC_TYPE_U8);
    }
    if (target_kind == R_SEMANTIC_TYPE_BOOL) {
        return false;
    }
    /* L22.2: conversions between floating formats and between them and integers. */
    if ((r_consteval_float_width(context, source) != 0U) ||
        (r_consteval_float_width(context, target) != 0U)) {
        return ((r_consteval_float_width(context, source) != 0U) ||
                ((source_kind != R_SEMANTIC_TYPE_CHAR) &&
                 (r_consteval_integer_kind(context, source) != R_SEMANTIC_TYPE_INVALID))) &&
               ((r_consteval_float_width(context, target) != 0U) ||
                ((target_kind != R_SEMANTIC_TYPE_CHAR) && (target_kind != R_SEMANTIC_TYPE_ENUM) &&
                 (r_consteval_integer_kind(context, target) != R_SEMANTIC_TYPE_INVALID)));
    }
    if (target_kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_consteval_fieldless_enum(context, target);
        return (aggregate != NULL) && aggregate->is_repr_c &&
               (r_consteval_integer_kind(context, source) != R_SEMANTIC_TYPE_INVALID) &&
               (source_kind != R_SEMANTIC_TYPE_BOOL);
    }
    return ((source_kind == R_SEMANTIC_TYPE_BOOL) ||
            (r_consteval_integer_kind(context, source) != R_SEMANTIC_TYPE_INVALID)) &&
           (r_consteval_integer_kind(context, target) != R_SEMANTIC_TYPE_INVALID);
}

static bool r_consteval_check_node(RFrontendContext *context,
                                   RSymbolId function_id,
                                   RHirNodeId node_id,
                                   uint32_t depth,
                                   RConstevalVerdict *verdict) {
    RHirNode node;
    RSemanticSymbol *symbol;
    uint32_t child_index;

    if (verdict->reason != R_CONSTEVAL_REASON_NONE) {
        return true;
    }
    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        r_consteval_block(verdict, R_CONSTEVAL_REASON_ILL_FORMED, node_id, R_SYMBOL_ID_INVALID);
        return true;
    }
    if ((node.effect_exit_count != 0U) && (node.kind != R_HIR_CALL) &&
        ((node.kind != R_HIR_STANDARD_CALL) ||
         (!r_consteval_owner_operation(node.standard_operation) &&
          !r_consteval_math_allowed(context, &node)))) {
        /* L22.1: only a call of an evaluable function throws; its errors are checked with it.
           L22.3: an owner operation reports allocation failures that translation never has. */
        r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
        return true;
    }
    if ((node.kind == R_HIR_CATCH) && (r_consteval_standard_error(context, node.type) != 0)) {
        /* L22.3: a clause for an allocation failure is never taken during translation; one for
           a string error runs without observing the error. */
        return (r_consteval_standard_error(context, node.type) == 1) ||
               r_consteval_check_node(context,
                                      function_id,
                                      r_consteval_child(context, &node, 0U),
                                      depth + 1U,
                                      verdict);
    }
    switch (node.kind) {
    case R_HIR_DROP:
        break;
    case R_HIR_TRY:
    case R_HIR_CATCH:
    case R_HIR_FINALLY:
    case R_HIR_BLOCK:
    case R_HIR_EXPRESSION_STATEMENT:
    case R_HIR_IF:
    case R_HIR_WHILE:
    case R_HIR_FOR:
    case R_HIR_BREAK:
    case R_HIR_CONTINUE:
    case R_HIR_VALUE_SCOPE:
    case R_HIR_DISCARD:
    case R_HIR_LOAD:
    case R_HIR_MOVE:
    case R_HIR_UNARY:
    case R_HIR_BINARY:
    case R_HIR_CONDITIONAL:
    case R_HIR_FIELD_PLACE:
    case R_HIR_INDEX_PLACE:
    case R_HIR_DEREF_PLACE:
    case R_HIR_BORROW:
    case R_HIR_LENGTH:
    case R_HIR_AGGREGATE_INIT:
    case R_HIR_FIELD_INIT:
    case R_HIR_ARRAY_INIT:
    case R_HIR_DEFAULT_VALUE:
    case R_HIR_STRING_LITERAL:
    case R_HIR_TYPE_QUERY:
    case R_HIR_SWITCH:
        break;
    case R_HIR_ENUM_CONSTANT:
        if (r_consteval_fieldless_enum(context, node.type) == NULL) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_TYPE, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    case R_HIR_LITERAL: {
        const RSemanticTypeKind kind = r_semantic_value_kind(context, node.type);
        if ((kind != R_SEMANTIC_TYPE_BOOL) && (kind != R_SEMANTIC_TYPE_CHAR) &&
            (r_consteval_integer_kind(context, node.type) == R_SEMANTIC_TYPE_INVALID) &&
            (r_consteval_float_width(context, node.type) == 0U)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_TYPE, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    }
    case R_HIR_SLICE:
        /* L22.3: the elements of an array owner as a slice, as a range `for` reads them. */
        if (node.child_count != 1U) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    case R_HIR_CAST: {
        RHirNode operand;
        if (!r_consteval_node_copy(context, r_consteval_child(context, &node, 0U), &operand) ||
            !r_consteval_cast_allowed(context, operand.type, node.type)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_TYPE, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    }
    case R_HIR_ASSIGN:
    case R_HIR_COMPOUND_ASSIGN:
        if (node.out_argument || node.out_commit || (node.out_place != R_HIR_NODE_ID_INVALID)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    case R_HIR_VARIANT:
    case R_HIR_VARIANT_TAG:
    case R_HIR_VARIANT_PAYLOAD: {
        /* L22.1: a value of an error family is built, tested and read through a borrow. */
        RHirNode operand;
        const RHirNode *tagged = &node;
        if (node.kind != R_HIR_VARIANT) {
            const RSemanticType *borrow;
            if ((node.child_count != 1U) ||
                !r_consteval_node_copy(context, r_consteval_child(context, &node, 0U), &operand) ||
                ((node.kind == R_HIR_VARIANT_PAYLOAD) && (node.operation != R_TOKEN_AMP) &&
                 (node.operation != R_TOKEN_KW_MOVE))) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
            borrow = r_semantic_type(context, r_semantic_value_type(context, operand.type));
            if ((borrow != NULL) && (borrow->kind == R_SEMANTIC_TYPE_BORROW)) {
                operand.type = borrow->base;
            }
            tagged = &operand;
        }
        if (!r_consteval_tagged_type(context, tagged->type)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_TYPE, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    }
    case R_HIR_THROW:
    case R_HIR_RETURN:
        /* L22.1, L22.3: a throw of a constant value or a return; the drops that leave scopes
           release nothing the evaluation observes. */
        for (child_index = 1U; child_index < node.child_count; ++child_index) {
            if (!r_consteval_drops_only(context, r_consteval_child(context, &node, child_index))) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
        }
        if ((node.kind == R_HIR_THROW) && (node.child_count == 0U)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        if (node.child_count > 1U) {
            /* The cleanup children are not evaluated. */
            RHirNode value;
            if (!r_consteval_node_copy(context, r_consteval_child(context, &node, 0U), &value)) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_ILL_FORMED, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
            return r_consteval_check_node(
                context, function_id, r_consteval_child(context, &node, 0U), depth + 1U, verdict);
        }
        break;
    case R_HIR_LOCAL:
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol == NULL) || symbol->is_static || symbol->is_thread_local) {
            r_consteval_block(
                verdict, R_CONSTEVAL_REASON_STATIC_STORAGE, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        if (symbol->is_out_storage || (symbol->out_parameter != R_SYMBOL_ID_INVALID) ||
            (node.child_count != 1U)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    case R_HIR_PLACE:
        symbol = r_consteval_symbol(context, node.symbol);
        if (symbol == NULL) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_ILL_FORMED, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        if ((symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) ||
            (symbol->kind == R_SEMANTIC_SYMBOL_PARAMETER)) {
            if (symbol->is_static || symbol->is_thread_local) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_STATIC_STORAGE, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
        } else if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) {
            bool known = false;
            if (!r_consteval_resolve_module_constant(
                    context, node.symbol, node.span, false, &known)) {
                return false;
            }
            if (!known) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_ILL_FORMED, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
        } else if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
            const RSemanticType *declared = r_semantic_type(context, symbol->type);
            if ((declared == NULL) || (declared->kind != R_SEMANTIC_TYPE_CONST) ||
                symbol->is_thread_local || symbol->is_import ||
                !r_consteval_value_type(context, symbol->type, 0U)) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_STATIC_STORAGE, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
            if (!r_consteval_prepare_module_object(context, node.symbol)) {
                return false;
            }
            symbol = r_consteval_symbol(context, node.symbol);
            if ((symbol == NULL) || symbol->poisoned ||
                (symbol->hir_node == R_HIR_NODE_ID_INVALID)) {
                r_consteval_block(
                    verdict, R_CONSTEVAL_REASON_STATIC_STORAGE, node_id, R_SYMBOL_ID_INVALID);
                return true;
            }
        } else {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    case R_HIR_CALL:
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol == NULL) || node.function_item_receiver ||
            !r_consteval_direct_call(symbol, &node)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        if (!r_consteval_classify(context, node.symbol, depth + 1U)) {
            return false;
        }
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol == NULL) || (symbol->consteval_state != R_CONSTEVAL_EVALUABLE)) {
            r_consteval_block(verdict,
                              (symbol != NULL) && (symbol->consteval_state == R_CONSTEVAL_CHECKING)
                                  ? R_CONSTEVAL_REASON_RECURSION
                                  : R_CONSTEVAL_REASON_CALLEE,
                              node_id,
                              node.symbol);
            return true;
        }
        break;
    case R_HIR_STANDARD_CALL:
        if (!r_consteval_standard_operation_allowed(node.standard_operation) &&
            !r_consteval_math_allowed(context, &node) &&
            !r_consteval_owner_operation(node.standard_operation)) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    case R_HIR_CASE:
        /* L22.3: a variant pattern binds a borrow of the payload or the payload itself. */
        if (((node.symbol != R_SYMBOL_ID_INVALID) &&
             ((node.case_pattern != R_HIR_CASE_PATTERN_VARIANT) ||
              !r_consteval_storage_type(context, node.type))) ||
            ((node.case_pattern != R_HIR_CASE_PATTERN_CONSTANT) &&
             (node.case_pattern != R_HIR_CASE_PATTERN_VARIANT) &&
             (node.case_pattern != R_HIR_CASE_PATTERN_DEFAULT))) {
            r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
            return true;
        }
        break;
    default:
        r_consteval_block(verdict, R_CONSTEVAL_REASON_OPERATION, node_id, R_SYMBOL_ID_INVALID);
        return true;
    }
    if ((node.kind != R_HIR_CASE) && !r_consteval_node_type(context, node.type)) {
        r_consteval_block(verdict, R_CONSTEVAL_REASON_TYPE, node_id, R_SYMBOL_ID_INVALID);
        return true;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (!r_consteval_check_node(context,
                                    function_id,
                                    r_consteval_child(context, &node, child_index),
                                    depth + 1U,
                                    verdict)) {
            return false;
        }
        if (verdict->reason != R_CONSTEVAL_REASON_NONE) {
            return true;
        }
    }
    return true;
}

static void r_consteval_mark(RFrontendContext *context,
                             RSymbolId function_id,
                             const RConstevalVerdict *verdict) {
    RSemanticSymbol *function = r_consteval_symbol(context, function_id);

    if (function == NULL) {
        return;
    }

    function->consteval_state = verdict->reason == R_CONSTEVAL_REASON_NONE
                                    ? (uint8_t)R_CONSTEVAL_EVALUABLE
                                    : (uint8_t)R_CONSTEVAL_RUNTIME;
    function->consteval_reason = (uint8_t)verdict->reason;
    function->consteval_blocker = verdict->blocker;
    function->consteval_callee = verdict->callee;
}

/*
 * R-FUNC-0023: derives the evaluability of one function from its signature and its checked body,
 * following every call. The result is memoized on the symbol. Before function bodies may be built
 * (context->consteval_ready) the function stays unknown.
 */
static bool r_consteval_classify(RFrontendContext *context, RSymbolId function_id, uint32_t depth) {
    RSemanticSymbol *function = r_consteval_symbol(context, function_id);
    RConstevalVerdict verdict = {
        R_CONSTEVAL_REASON_NONE, R_HIR_NODE_ID_INVALID, R_SYMBOL_ID_INVALID};
    RHirNode function_node;
    RHirNodeId body = R_HIR_NODE_ID_INVALID;
    uint32_t parameter_index;

    if ((function == NULL) || (function->kind != R_SEMANTIC_SYMBOL_FUNCTION)) {
        return true;
    }
    if ((function->consteval_state == R_CONSTEVAL_EVALUABLE) ||
        (function->consteval_state == R_CONSTEVAL_RUNTIME) ||
        (function->consteval_state == R_CONSTEVAL_CHECKING) || !context->consteval_ready) {
        return true;
    }
    if (depth > context->options.limits.max_nesting) {
        verdict.reason = R_CONSTEVAL_REASON_RECURSION;
    } else if (function->is_import || function->is_extern_c || function->is_trait_prototype ||
               function->is_synthetic) {
        verdict.reason = R_CONSTEVAL_REASON_NO_BODY;
    } else if (function->is_async) {
        verdict.reason = R_CONSTEVAL_REASON_ASYNC;
    } else if (function->is_unsafe) {
        verdict.reason = R_CONSTEVAL_REASON_UNSAFE;
    } else if ((function->throws_type != R_TYPE_ID_INVALID) &&
               !r_consteval_error_set(context, function->throws_type)) {
        verdict.reason = R_CONSTEVAL_REASON_THROWS;
    } else if (r_consteval_string_recipe(context, function)) {
        /* L22.3: a compiler-made string conversion is evaluated by its recipe. */
        r_consteval_mark(context, function_id, &verdict);
        return true;
    } else if (function->is_callback || (function->closure_aggregate != 0U) ||
               (function->json_operation != 0U) || (function->format_recipe != 0U) ||
               function->is_variadic_slice) {
        verdict.reason = R_CONSTEVAL_REASON_CALLABLE;
    } else if (function->poisoned) {
        verdict.reason = R_CONSTEVAL_REASON_ILL_FORMED;
    } else if ((r_semantic_value_kind(context, function->return_type) != R_SEMANTIC_TYPE_VOID) &&
               !r_consteval_value_type(context, function->return_type, 0U)) {
        verdict.reason = R_CONSTEVAL_REASON_SIGNATURE;
    }
    for (parameter_index = 0U; (verdict.reason == R_CONSTEVAL_REASON_NONE) &&
                               (parameter_index < function->parameter_count);
         ++parameter_index) {
        const size_t position = (size_t)function->first_parameter_type + parameter_index;
        const RTypeId parameter = position < context->semantic_parameter_type_count
                                      ? context->semantic_parameter_types[position]
                                      : R_TYPE_ID_INVALID;
        const RSemanticType *parameter_type = r_semantic_type(context, parameter);
        if ((parameter_type == NULL) ||
            ((parameter_type->flags & (uint32_t)R_SEMANTIC_TYPE_FLAG_OUT) != 0U) ||
            !r_consteval_storage_type(context, parameter)) {
            verdict.reason = R_CONSTEVAL_REASON_SIGNATURE;
        }
    }
    if (verdict.reason != R_CONSTEVAL_REASON_NONE) {
        r_consteval_mark(context, function_id, &verdict);
        return true;
    }
    if (!r_consteval_ensure_body(context, function_id, &body)) {
        return false;
    }
    function = r_consteval_symbol(context, function_id);
    if ((function == NULL) || (body == R_HIR_NODE_ID_INVALID)) {
        /* A body under construction is not memoized: it may become available later. */
        return true;
    }
    if (function->poisoned || !r_consteval_node_copy(context, body, &function_node) ||
        (function_node.kind != R_HIR_FUNCTION) || (function_node.child_count == 0U)) {
        verdict.reason = R_CONSTEVAL_REASON_ILL_FORMED;
        r_consteval_mark(context, function_id, &verdict);
        return true;
    }
    function->consteval_state = (uint8_t)R_CONSTEVAL_CHECKING;
    if (!r_consteval_check_node(
            context,
            function_id,
            r_consteval_child(context, &function_node, function_node.child_count - 1U),
            depth,
            &verdict)) {
        function = r_consteval_symbol(context, function_id);
        if (function != NULL) {
            function->consteval_state = (uint8_t)R_CONSTEVAL_UNKNOWN;
        }
        return false;
    }
    r_consteval_mark(context, function_id, &verdict);
    return true;
}

/* ---- values and evaluation state ---- */

typedef enum RConstKind {
    R_CONST_UNSET = 0,
    R_CONST_SCALAR,
    R_CONST_TEXT,
    R_CONST_AGGREGATE,
    R_CONST_REFERENCE,
    R_CONST_SLICE,
    /* L22.1: a member of an error family: `bits` is its tag and `items` its one payload cell. */
    R_CONST_VARIANT,
    /* L22.3: a standard owner: `count` elements or entries in `bits` reserved `items`. */
    R_CONST_CONTAINER,
    /* L22.3: a cursor over the owner `items` at position `bits`. */
    R_CONST_CURSOR
} RConstKind;

typedef struct RConstCell {
    RConstKind kind;
    RTypeId type;
    /* Scalar representation bits, or the intern id of the bytes of a text value. */
    uint64_t bits;
    /* Byte length of a text value, element count of an aggregate or length of a slice. */
    uint64_t count;
    /* Elements of an aggregate, the referenced cell, or the first element of a slice. */
    struct RConstCell *items;
} RConstCell;

typedef struct RConstBlock {
    struct RConstBlock *next;
    size_t used;
    size_t capacity;
} RConstBlock;

typedef enum RConstFlow {
    R_CONST_FLOW_NORMAL = 0,
    R_CONST_FLOW_BREAK,
    R_CONST_FLOW_BREAK_SWITCH,
    R_CONST_FLOW_CONTINUE,
    R_CONST_FLOW_RETURN
} RConstFlow;

typedef enum RConstFailure {
    R_CONST_FAILURE_NONE = 0,
    R_CONST_FAILURE_PANIC,
    R_CONST_FAILURE_LIMIT,
    R_CONST_FAILURE_UNSUPPORTED,
    R_CONST_FAILURE_RESOURCE,
    /* L22.1: a checked error leaves the evaluated calls; a try statement may catch it. */
    R_CONST_FAILURE_THROW
} RConstFailure;

/* R-META-0003: pairs of a callee named by a generic definition and the callee that one
 * instantiation closed it to (r_consteval_close_calls). */
typedef struct RConstevalCallees {
    RSymbolId *pairs;
    size_t count;
    size_t capacity;
} RConstevalCallees;

typedef struct RConstEval {
    RFrontendContext *context;
    RConstBlock *blocks;
    size_t charged;
    RConstCell **slots;
    size_t slot_count;
    /* Symbols whose slots this evaluation used; the shared slot array is cleared by them. */
    RSymbolId *touched;
    size_t touched_count;
    size_t touched_capacity;
    bool shared_slots;
    uint64_t steps;
    RConstFlow flow;
    uint32_t flow_loops; /* Further loops a labeled BREAK or CONTINUE in flight leaves. */
    RConstCell returned;
    RConstFailure failure;
    /* Panic category spelled as by the run-time panic report, and an explicit message. */
    const char *category;
    RConstCell message;
    /* The error in flight and its exact type while the failure is a throw. */
    RConstCell thrown;
    RTypeId thrown_type;
    /* L22.3: the evaluation created or changed an owner. Allocation, and its failure, are
       observable at run time, so such a call is replaced only in a required constant. */
    bool allocates;
    RSourceSpan span;
    RSymbolId *active;
    uint32_t active_count;
    uint32_t active_capacity;
    RSymbolId *chain;
    uint32_t chain_count;
    /* R-META-0003: the closed arguments of an instantiation that decides a dependent `@if`
       condition of its generic definition; zero outside such a decision. */
    uint32_t schema;
    uint32_t arguments;
    RConstevalCallees callees;
} RConstEval;

static size_t r_consteval_header_size(void) {
    return (sizeof(RConstBlock) + (size_t)R_CONSTEVAL_ALIGNMENT - 1U) &
           ~((size_t)R_CONSTEVAL_ALIGNMENT - 1U);
}

static void r_consteval_capture_chain(RConstEval *eval) {
    if ((eval->active_count == 0U) || (eval->chain != NULL)) {
        return;
    }
    eval->chain = r_context_allocate(eval->context, (size_t)eval->active_count * sizeof(RSymbolId));
    if (eval->chain != NULL) {
        (void)memcpy(eval->chain, eval->active, (size_t)eval->active_count * sizeof(RSymbolId));
        eval->chain_count = eval->active_count;
    }
}

static bool r_consteval_fail(RConstEval *eval, RConstFailure failure, RSourceSpan span) {
    if (eval->failure == R_CONST_FAILURE_NONE) {
        eval->failure = failure;
        eval->span = span;
        r_consteval_capture_chain(eval);
    }
    return false;
}

static bool r_consteval_panic(RConstEval *eval, const char *category, RSourceSpan span) {
    if (eval->failure == R_CONST_FAILURE_NONE) {
        eval->category = category;
    }
    return r_consteval_fail(eval, R_CONST_FAILURE_PANIC, span);
}

static void *r_consteval_allocate(RConstEval *eval, size_t size, RSourceSpan span) {
    const size_t header = r_consteval_header_size();
    RConstBlock *block = eval->blocks;
    unsigned char *storage;

    size = (size + (size_t)R_CONSTEVAL_ALIGNMENT - 1U) & ~((size_t)R_CONSTEVAL_ALIGNMENT - 1U);
    if (size == 0U) {
        size = (size_t)R_CONSTEVAL_ALIGNMENT;
    }
    if ((size > (size_t)R_CONSTEVAL_MEMORY_LIMIT) ||
        (eval->charged > (size_t)R_CONSTEVAL_MEMORY_LIMIT - size)) {
        (void)r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, span);
        return NULL;
    }
    if ((block == NULL) || (block->capacity - block->used < size)) {
        const size_t capacity =
            size > (size_t)R_CONSTEVAL_BLOCK_SIZE ? size : (size_t)R_CONSTEVAL_BLOCK_SIZE;
        block = r_context_allocate(eval->context, header + capacity);
        if (block == NULL) {
            (void)r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
            return NULL;
        }
        block->next = eval->blocks;
        block->used = 0U;
        block->capacity = capacity;
        eval->blocks = block;
    }
    storage = (unsigned char *)block + header + block->used;
    block->used += size;
    eval->charged += size;
    (void)memset(storage, 0, size);
    return storage;
}

static RConstCell *r_consteval_cells(RConstEval *eval, uint64_t count, RSourceSpan span) {
    if (count > (uint64_t)((size_t)R_CONSTEVAL_MEMORY_LIMIT / sizeof(RConstCell))) {
        (void)r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, span);
        return NULL;
    }
    return r_consteval_allocate(eval, (size_t)count * sizeof(RConstCell), span);
}

static void r_consteval_init(RConstEval *eval, RFrontendContext *context) {
    (void)memset(eval, 0, sizeof(*eval));
    eval->context = context;
    if (!context->consteval_slots_in_use) {
        /* One evaluation at a time reuses the context's slot array. */
        context->consteval_slots_in_use = true;
        eval->shared_slots = true;
        eval->slots = (RConstCell **)context->consteval_slots;
        eval->slot_count = context->consteval_slot_count;
    }
}

static void r_consteval_destroy(RConstEval *eval) {
    RFrontendContext *context = eval->context;
    RConstBlock *block = eval->blocks;
    size_t index;

    while (block != NULL) {
        RConstBlock *next = block->next;
        r_context_free(context, block);
        block = next;
    }
    if (eval->shared_slots) {
        for (index = 0U; index < eval->touched_count; ++index) {
            eval->slots[(size_t)eval->touched[index] - 1U] = NULL;
        }
        context->consteval_slots = (void **)eval->slots;
        context->consteval_slot_count = eval->slot_count;
        context->consteval_slots_in_use = false;
    } else {
        r_context_free(context, eval->slots);
    }
    r_context_free(context, eval->touched);
    r_context_free(context, eval->active);
    r_context_free(context, eval->chain);
    r_context_free(context, eval->callees.pairs);
    (void)memset(eval, 0, sizeof(*eval));
}

static bool r_consteval_step(RConstEval *eval, RSourceSpan span) {
    eval->steps += 1U;
    if (eval->steps > (uint64_t)R_CONSTEVAL_STEP_LIMIT) {
        return r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, span);
    }
    return true;
}

static RConstCell **r_consteval_slot(RConstEval *eval, RSymbolId symbol_id, RSourceSpan span) {
    const size_t needed = (size_t)symbol_id;

    if (symbol_id == R_SYMBOL_ID_INVALID) {
        (void)r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        return NULL;
    }
    if (needed > eval->slot_count) {
        size_t capacity = eval->context->semantic_symbol_count;
        RConstCell **slots;
        if (capacity < needed) {
            capacity = needed;
        }
        slots = r_context_allocate(eval->context, capacity * sizeof(*slots));
        if (slots == NULL) {
            (void)r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
            return NULL;
        }
        (void)memset(slots, 0, capacity * sizeof(*slots));
        if (eval->slots != NULL) {
            (void)memcpy(slots, eval->slots, eval->slot_count * sizeof(*slots));
            r_context_free(eval->context, eval->slots);
        }
        eval->slots = slots;
        eval->slot_count = capacity;
    }
    if ((eval->slots[needed - 1U] == NULL) && eval->shared_slots) {
        if (!r_grow_array(eval->context,
                          (void **)&eval->touched,
                          &eval->touched_capacity,
                          sizeof(*eval->touched),
                          eval->touched_count + 1U)) {
            (void)r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
            return NULL;
        }
        eval->touched[eval->touched_count++] = symbol_id;
    }
    return &eval->slots[needed - 1U];
}

/* ---- value construction ---- */

static bool r_consteval_zero(
    RConstEval *eval, RTypeId type_id, RConstCell *cell, RSourceSpan span, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RTypeId value_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_id);
    const RSemanticAggregate *aggregate;
    uint64_t index;

    (void)memset(cell, 0, sizeof(*cell));
    cell->type = value_id;
    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
        cell->kind = R_CONST_TEXT;
        return true;
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        cell->kind = R_CONST_AGGREGATE;
        cell->count = type->length;
        cell->items = r_consteval_cells(eval, cell->count, span);
        if (cell->items == NULL) {
            return false;
        }
        for (index = 0U; index < cell->count; ++index) {
            if (!r_consteval_zero(eval, type->base, &cell->items[index], span, depth + 1U)) {
                return false;
            }
        }
        return true;
    case R_SEMANTIC_TYPE_STRUCT: {
        size_t first_field;
        aggregate = r_semantic_aggregate_for_type(context, value_id);
        if (aggregate == NULL) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        first_field = aggregate->first_field;
        cell->kind = R_CONST_AGGREGATE;
        cell->count = aggregate->field_count;
        cell->items = r_consteval_cells(eval, cell->count, span);
        if ((cell->items == NULL) && (cell->count != 0U)) {
            return false;
        }
        for (index = 0U; index < cell->count; ++index) {
            if (!r_consteval_zero(eval,
                                  context->semantic_fields[first_field + (size_t)index].type,
                                  &cell->items[index],
                                  span,
                                  depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    case R_SEMANTIC_TYPE_OPTION:
        /* L22.3: an option starts as `o::none`. */
        cell->kind = R_CONST_VARIANT;
        return true;
    default:
        if (r_consteval_owner(context, value_id) != R_CONSTEVAL_OWNER_NONE) {
            /* L22.3: an owner starts empty. */
            cell->kind = R_CONST_CONTAINER;
            return true;
        }
        if ((type->kind != R_SEMANTIC_TYPE_BOOL) &&
            (r_consteval_integer_kind(context, value_id) == R_SEMANTIC_TYPE_INVALID) &&
            (r_consteval_float_width(context, value_id) == 0U)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        cell->kind = R_CONST_SCALAR;
        return true;
    }
}

/* A deep copy in fresh storage: the value semantics of a load or an argument. */
static bool r_consteval_clone(RConstEval *eval,
                              const RConstCell *source,
                              RConstCell *target,
                              RSourceSpan span) {
    uint64_t index;

    *target = *source;
    if ((source->kind != R_CONST_AGGREGATE) && (source->kind != R_CONST_VARIANT)) {
        return true;
    }
    target->items = r_consteval_cells(eval, source->count, span);
    if ((target->items == NULL) && (source->count != 0U)) {
        return false;
    }
    for (index = 0U; index < source->count; ++index) {
        if (!r_consteval_clone(eval, &source->items[index], &target->items[index], span)) {
            return false;
        }
    }
    return true;
}

/* A store keeps the element cells of an aggregate in place, so borrows of them stay valid. */
static bool r_consteval_store(RConstEval *eval,
                              RConstCell *target,
                              const RConstCell *source,
                              RSourceSpan span) {
    uint64_t index;

    if ((source->kind == R_CONST_AGGREGATE) && (target->kind == R_CONST_AGGREGATE) &&
        (target->count == source->count) && (target->items != NULL)) {
        for (index = 0U; index < source->count; ++index) {
            if (!r_consteval_store(eval, &target->items[index], &source->items[index], span)) {
                return false;
            }
        }
        return true;
    }
    return r_consteval_clone(eval, source, target, span);
}

static void r_consteval_scalar_value(RConstCell *cell, RTypeId type, uint64_t bits) {
    (void)memset(cell, 0, sizeof(*cell));
    cell->kind = R_CONST_SCALAR;
    cell->type = type;
    cell->bits = bits;
}

/* ---- floating values (R-TYPE-0006, R-EXPR-0025, L22.2) ---- */

#if defined(FLT_EVAL_METHOD) && (FLT_EVAL_METHOD != 0)
#error "translation-time floating evaluation needs operations performed in their own format"
#endif

static float r_consteval_f32(uint64_t bits) {
    const uint32_t narrow = (uint32_t)bits;
    float value;
    (void)memcpy(&value, &narrow, sizeof(value));
    return value;
}

static double r_consteval_f64(uint64_t bits) {
    double value;
    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t r_consteval_f32_bits(float value) {
    uint32_t narrow;
    (void)memcpy(&narrow, &value, sizeof(narrow));
    return (uint64_t)narrow;
}

static uint64_t r_consteval_f64_bits(double value) {
    uint64_t bits;
    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* One IEC 60559 operation in the format of its type, rounded to nearest, ties to even; each
   operation is performed and rounded on its own. */
static bool r_consteval_float_arithmetic(
    RTokenKind operation, uint32_t width, uint64_t left, uint64_t right, uint64_t *result) {
    if (width == 32U) {
        const float a = r_consteval_f32(left);
        const float b = r_consteval_f32(right);
        float value;
        switch (operation) {
        case R_TOKEN_PLUS:
            value = a + b;
            break;
        case R_TOKEN_MINUS:
            value = a - b;
            break;
        case R_TOKEN_STAR:
            value = a * b;
            break;
        case R_TOKEN_SLASH:
            value = a / b;
            break;
        default:
            return false;
        }
        *result = r_consteval_f32_bits(value);
        return true;
    }
    {
        const double a = r_consteval_f64(left);
        const double b = r_consteval_f64(right);
        double value;
        switch (operation) {
        case R_TOKEN_PLUS:
            value = a + b;
            break;
        case R_TOKEN_MINUS:
            value = a - b;
            break;
        case R_TOKEN_STAR:
            value = a * b;
            break;
        case R_TOKEN_SLASH:
            value = a / b;
            break;
        default:
            return false;
        }
        *result = r_consteval_f64_bits(value);
        return true;
    }
}

/* R-EXPR-0009: an ordered or unordered comparison; with a NaN only `!=` holds. */
static bool r_consteval_float_compare(
    RTokenKind operation, uint32_t width, uint64_t left, uint64_t right, bool *result) {
    const double a = width == 32U ? (double)r_consteval_f32(left) : r_consteval_f64(left);
    const double b = width == 32U ? (double)r_consteval_f32(right) : r_consteval_f64(right);
    switch (operation) {
    case R_TOKEN_EQUAL_EQUAL:
        *result = a == b;
        return true;
    case R_TOKEN_BANG_EQUAL:
        *result = a != b;
        return true;
    case R_TOKEN_LESS:
        *result = a < b;
        return true;
    case R_TOKEN_LESS_EQUAL:
        *result = a <= b;
        return true;
    case R_TOKEN_GREATER:
        *result = a > b;
        return true;
    case R_TOKEN_GREATER_EQUAL:
        *result = a >= b;
        return true;
    default:
        return false;
    }
}

/* R-EXPR-0016, R-EXPR-0017: a conversion that involves a floating type. An integer converts to
   the nearest value of the floating format; a floating value converts to an integer by
   truncation, and NaN, an infinity or a value out of the destination range is an invalid
   conversion; between floating formats a value is rounded to nearest. */
static bool r_consteval_float_convert(const RFrontendContext *context,
                                      RTypeId source,
                                      RTypeId target,
                                      uint64_t bits,
                                      bool *invalid,
                                      uint64_t *result) {
    const uint32_t source_width = r_consteval_float_width(context, source);
    const uint32_t target_width = r_consteval_float_width(context, target);
    const RSemanticTypeKind source_integer = r_consteval_integer_kind(context, source);
    const RSemanticTypeKind target_integer = r_consteval_integer_kind(context, target);
    bool is_signed;
    uint32_t width;

    *invalid = false;
    if ((source_width != 0U) && (target_width != 0U)) {
        const double value =
            source_width == 32U ? (double)r_consteval_f32(bits) : r_consteval_f64(bits);
        *result =
            target_width == 32U ? r_consteval_f32_bits((float)value) : r_consteval_f64_bits(value);
        return true;
    }
    if ((source_width == 0U) && (target_width != 0U)) {
        if (!r_semantic_integer_properties(source_integer, &is_signed, &width)) {
            return false;
        }
        bits &= r_constant_integer_mask(width);
        if (is_signed && (width < 64U) && ((bits & (UINT64_C(1) << (width - 1U))) != 0U)) {
            bits |= ~r_constant_integer_mask(width);
        }
        if (target_width == 32U) {
            *result = r_consteval_f32_bits(is_signed ? (float)(int64_t)bits : (float)bits);
        } else {
            *result = r_consteval_f64_bits(is_signed ? (double)(int64_t)bits : (double)bits);
        }
        return true;
    }
    if ((source_width == 0U) ||
        !r_semantic_integer_properties(target_integer, &is_signed, &width)) {
        return false;
    }
    {
        const double value =
            source_width == 32U ? (double)r_consteval_f32(bits) : r_consteval_f64(bits);
        const double whole = trunc(value);
        /* The destination range as exact binary64 bounds: [-2^(w-1), 2^(w-1)) or [0, 2^w). */
        const double upper = ldexp(1.0, is_signed ? (int)width - 1 : (int)width);
        const double lower = is_signed ? -upper : 0.0;
        if (isnan(value) || isinf(value) || (whole < lower) || (whole >= upper)) {
            *invalid = true;
            return true;
        }
        *result = (is_signed ? (uint64_t)(int64_t)whole : (uint64_t)whole) &
                  r_constant_integer_mask(width);
        return true;
    }
}

/* The base of a std.math operation name `base_S` for a floating suffix S (Library
   R-SLIB-MATH-0002). */
static bool r_consteval_math_base(const char *name, char *base, size_t capacity) {
    static const char *const suffixes[] = {
        "_f32", "_f64", "_c_float", "_c_double", "_c_long_double"};
    const size_t length = strlen(name);

    for (size_t index = 0U; index < sizeof(suffixes) / sizeof(suffixes[0]); ++index) {
        const size_t suffix = strlen(suffixes[index]);
        if ((length > suffix) && (length - suffix < capacity) &&
            (strcmp(name + length - suffix, suffixes[index]) == 0)) {
            (void)memcpy(base, name, length - suffix);
            base[length - suffix] = '\0';
            return true;
        }
    }
    return false;
}

/* Library R-SLIB-MATH-0003: the fallible operations whose IEC 60559 result is correctly rounded,
   `sqrt` and `remainder`; their domain errors are thrown during translation. */
static bool r_consteval_math_checked(const char *name) {
    char base[16];

    return r_consteval_math_base(name, base, sizeof(base)) &&
           ((strcmp(base, "sqrt") == 0) || (strcmp(base, "remainder") == 0));
}

/* Library R-SLIB-MATH-0002: the exact non-failing operations of std.math. */
static bool r_consteval_math_exact(const char *name) {
    static const char *const names[] = {"abs",
                                        "floor",
                                        "ceil",
                                        "trunc",
                                        "round",
                                        "copy_sign",
                                        "min",
                                        "max",
                                        "next_after",
                                        "is_finite",
                                        "is_infinite",
                                        "is_nan",
                                        "is_normal",
                                        "sign_bit"};
    char base[16];

    if (!r_consteval_math_base(name, base, sizeof(base))) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(names[index], base) == 0) {
            return true;
        }
    }
    return false;
}

static double r_consteval_float_value(uint32_t width, uint64_t bits) {
    return width == 32U ? (double)r_consteval_f32(bits) : r_consteval_f64(bits);
}

static uint64_t r_consteval_float_bits(uint32_t width, double value) {
    return width == 32U ? r_consteval_f32_bits((float)value) : r_consteval_f64_bits(value);
}

/* Evaluates one exact std.math operation; `width` is the format of its operands. */
static bool r_consteval_math_apply(const char *name,
                                   uint32_t width,
                                   uint64_t left,
                                   uint64_t right,
                                   bool *boolean,
                                   bool *domain,
                                   uint64_t *result) {
    const double a = r_consteval_float_value(width, left);
    const double b = r_consteval_float_value(width, right);
    char base[16];

    if (!r_consteval_math_base(name, base, sizeof(base))) {
        return false;
    }
    *boolean = false;
    *domain = false;
    if (strcmp(base, "sqrt") == 0) {
        /* R-SLIB-MATH-0003: a negative input other than negative zero is a domain error. */
        if (!isnan(a) && (signbit(a) != 0) && (a != 0.0)) {
            *domain = true;
            return true;
        }
        *result = width == 32U ? r_consteval_f32_bits(sqrtf(r_consteval_f32(left)))
                               : r_consteval_f64_bits(sqrt(a));
        return true;
    }
    if (strcmp(base, "remainder") == 0) {
        /* A zero divisor or an infinite dividend is a domain error. */
        if ((b == 0.0) || (isinf(a) != 0)) {
            *domain = true;
            return true;
        }
        *result =
            width == 32U
                ? r_consteval_f32_bits(remainderf(r_consteval_f32(left), r_consteval_f32(right)))
                : r_consteval_f64_bits(remainder(a, b));
        return true;
    }
    if (strcmp(base, "is_finite") == 0 || strcmp(base, "is_infinite") == 0 ||
        strcmp(base, "is_nan") == 0 || strcmp(base, "is_normal") == 0 ||
        strcmp(base, "sign_bit") == 0) {
        bool answer;
        if (strcmp(base, "is_finite") == 0) {
            answer = isfinite(a) != 0;
        } else if (strcmp(base, "is_infinite") == 0) {
            answer = isinf(a) != 0;
        } else if (strcmp(base, "is_nan") == 0) {
            answer = isnan(a) != 0;
        } else if (strcmp(base, "is_normal") == 0) {
            answer = width == 32U ? isnormal(r_consteval_f32(left)) != 0 : isnormal(a) != 0;
        } else {
            answer = signbit(a) != 0;
        }
        *boolean = true;
        *result = answer ? UINT64_C(1) : UINT64_C(0);
        return true;
    }
    if (strcmp(base, "abs") == 0) {
        *result = left & ~(UINT64_C(1) << (width - 1U));
        return true;
    }
    if (strcmp(base, "copy_sign") == 0) {
        const uint64_t sign = UINT64_C(1) << (width - 1U);
        *result = (left & ~sign) | (right & sign);
        return true;
    }
    if (strcmp(base, "min") == 0 || strcmp(base, "max") == 0) {
        const bool minimum = strcmp(base, "min") == 0;
        if (isnan(a)) {
            *result = right;
        } else if (isnan(b)) {
            *result = left;
        } else if ((a == b) && (signbit(a) != signbit(b))) {
            *result = (signbit(a) != 0) == minimum ? left : right;
        } else {
            *result = (minimum ? a < b : a > b) ? left : right;
        }
        return true;
    }
    if (strcmp(base, "next_after") == 0) {
        *result =
            width == 32U
                ? r_consteval_f32_bits(nextafterf(r_consteval_f32(left), r_consteval_f32(right)))
                : r_consteval_f64_bits(nextafter(a, b));
        return true;
    }
    if (strcmp(base, "floor") == 0) {
        *result = r_consteval_float_bits(width, floor(a));
    } else if (strcmp(base, "ceil") == 0) {
        *result = r_consteval_float_bits(width, ceil(a));
    } else if (strcmp(base, "trunc") == 0) {
        *result = r_consteval_float_bits(width, trunc(a));
    } else if (strcmp(base, "round") == 0) {
        *result = r_consteval_float_bits(width, round(a));
    } else {
        return false;
    }
    return true;
}

/* ---- numeric rules of the abstract machine ---- */

/* Converts integer representation bits; `checked` mirrors an explicit `as` or a narrowing into a
 * signed destination, where an unrepresentable value panics. */
static bool r_consteval_convert_integer(uint64_t bits,
                                        RSemanticTypeKind source,
                                        RSemanticTypeKind target,
                                        bool checked,
                                        uint64_t *result) {
    bool source_signed;
    bool target_signed;
    uint32_t source_width;
    uint32_t target_width;
    uint64_t source_mask;
    uint64_t target_mask;
    uint64_t magnitude;
    uint64_t limit;
    bool negative;

    if (!r_semantic_integer_properties(source, &source_signed, &source_width) ||
        !r_semantic_integer_properties(target, &target_signed, &target_width)) {
        return false;
    }
    source_mask = r_constant_integer_mask(source_width);
    target_mask = r_constant_integer_mask(target_width);
    bits &= source_mask;
    negative = source_signed && ((bits & (UINT64_C(1) << (source_width - 1U))) != 0U);
    magnitude = negative ? (UINT64_C(0) - bits) & source_mask : bits;
    limit =
        target_signed ? (target_mask >> 1U) + (negative ? UINT64_C(1) : UINT64_C(0)) : target_mask;
    if (checked && ((negative && !target_signed) || (magnitude > limit))) {
        return false;
    }
    *result = negative ? (UINT64_C(0) - magnitude) & target_mask : magnitude & target_mask;
    return true;
}

static const char *
r_consteval_arithmetic_category(RTokenKind operation, RSemanticTypeKind kind, uint64_t right) {
    bool is_signed;
    uint32_t width;

    if (((operation == R_TOKEN_SLASH) || (operation == R_TOKEN_PERCENT)) && (right == 0U)) {
        return "division_by_zero";
    }
    if (((operation == R_TOKEN_LESS_LESS) || (operation == R_TOKEN_GREATER_GREATER)) &&
        r_semantic_integer_properties(kind, &is_signed, &width) && (right >= (uint64_t)width)) {
        return "invalid_shift";
    }
    return "integer_overflow";
}

static RTokenKind r_consteval_compound_operator(RTokenKind operation) {
    switch (operation) {
    case R_TOKEN_PLUS_EQUAL:
        return R_TOKEN_PLUS;
    case R_TOKEN_MINUS_EQUAL:
        return R_TOKEN_MINUS;
    case R_TOKEN_STAR_EQUAL:
        return R_TOKEN_STAR;
    case R_TOKEN_SLASH_EQUAL:
        return R_TOKEN_SLASH;
    case R_TOKEN_PERCENT_EQUAL:
        return R_TOKEN_PERCENT;
    case R_TOKEN_AMP_EQUAL:
        return R_TOKEN_AMP;
    case R_TOKEN_PIPE_EQUAL:
        return R_TOKEN_PIPE;
    case R_TOKEN_CARET_EQUAL:
        return R_TOKEN_CARET;
    case R_TOKEN_LESS_LESS_EQUAL:
        return R_TOKEN_LESS_LESS;
    case R_TOKEN_GREATER_GREATER_EQUAL:
        return R_TOKEN_GREATER_GREATER;
    default:
        return operation;
    }
}

/* Integer and bool operators with the run-time traps of R-EXPR-0005..0007. */
static bool r_consteval_arithmetic(RConstEval *eval,
                                   RTokenKind operation,
                                   RTypeId type,
                                   uint64_t left,
                                   uint64_t right,
                                   RSourceSpan span,
                                   uint64_t *result) {
    const RSemanticTypeKind kind = r_consteval_integer_kind(eval->context, type);
    const uint32_t float_width = r_consteval_float_width(eval->context, type);

    if (float_width != 0U) {
        /* R-EXPR-0025: floating division by zero yields an infinity or NaN, never a panic. */
        return r_consteval_float_arithmetic(operation, float_width, left, right, result)
                   ? true
                   : r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    if (r_semantic_value_kind(eval->context, type) == R_SEMANTIC_TYPE_BOOL) {
        switch (operation) {
        case R_TOKEN_AMP:
            *result = left & right;
            return true;
        case R_TOKEN_PIPE:
            *result = left | right;
            return true;
        case R_TOKEN_CARET:
            *result = left ^ right;
            return true;
        default:
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
    }
    if (kind == R_SEMANTIC_TYPE_INVALID) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    if (!r_constant_integer_binary(operation, kind, left, right, result)) {
        if ((operation != R_TOKEN_PLUS) && (operation != R_TOKEN_MINUS) &&
            (operation != R_TOKEN_STAR) && (operation != R_TOKEN_SLASH) &&
            (operation != R_TOKEN_PERCENT) && (operation != R_TOKEN_AMP) &&
            (operation != R_TOKEN_PIPE) && (operation != R_TOKEN_CARET) &&
            (operation != R_TOKEN_LESS_LESS) && (operation != R_TOKEN_GREATER_GREATER)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        return r_consteval_panic(
            eval, r_consteval_arithmetic_category(operation, kind, right), span);
    }
    return true;
}

static bool r_consteval_compare(RConstEval *eval,
                                RTokenKind operation,
                                const RConstCell *left,
                                const RConstCell *right,
                                RSourceSpan span,
                                bool *result) {
    uint64_t a = left->bits;
    uint64_t b = right->bits;
    const RSemanticTypeKind kind = r_consteval_integer_kind(eval->context, left->type);
    bool is_signed = false;
    uint32_t width = 0U;

    if ((left->kind != R_CONST_SCALAR) || (right->kind != R_CONST_SCALAR)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    if (r_consteval_float_width(eval->context, left->type) != 0U) {
        return r_consteval_float_compare(
                   operation, r_consteval_float_width(eval->context, left->type), a, b, result)
                   ? true
                   : r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    if (r_semantic_value_kind(eval->context, left->type) == R_SEMANTIC_TYPE_BOOL) {
        if ((operation != R_TOKEN_EQUAL_EQUAL) && (operation != R_TOKEN_BANG_EQUAL)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
    } else if (!r_semantic_integer_properties(kind, &is_signed, &width)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    } else if (is_signed) {
        const uint64_t sign = UINT64_C(1) << (width - 1U);
        a ^= sign;
        b ^= sign;
    }
    switch (operation) {
    case R_TOKEN_EQUAL_EQUAL:
        *result = a == b;
        return true;
    case R_TOKEN_BANG_EQUAL:
        *result = a != b;
        return true;
    case R_TOKEN_LESS:
        *result = a < b;
        return true;
    case R_TOKEN_LESS_EQUAL:
        *result = a <= b;
        return true;
    case R_TOKEN_GREATER:
        *result = a > b;
        return true;
    case R_TOKEN_GREATER_EQUAL:
        *result = a >= b;
        return true;
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
}

/* core::wrapping_* and core::saturating_*: never panic (Library R-LIB-0001). */
static bool r_consteval_core_integer(RConstEval *eval,
                                     RStandardCallOperation operation,
                                     RTypeId type,
                                     uint64_t left,
                                     uint64_t right,
                                     RSourceSpan span,
                                     uint64_t *result) {
    const RSemanticTypeKind kind = r_consteval_integer_kind(eval->context, type);
    bool is_signed;
    uint32_t width;
    uint64_t mask;
    uint64_t sign;
    uint64_t maximum;
    uint64_t minimum;
    RTokenKind arithmetic;

    if (!r_semantic_integer_properties(kind, &is_signed, &width)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    mask = r_constant_integer_mask(width);
    sign = UINT64_C(1) << (width - 1U);
    maximum = is_signed ? sign - 1U : mask;
    minimum = is_signed ? sign : 0U;
    switch (operation) {
    case R_STANDARD_CALL_CORE_WRAPPING_ADD:
    case R_STANDARD_CALL_CORE_SATURATING_ADD:
        arithmetic = R_TOKEN_PLUS;
        break;
    case R_STANDARD_CALL_CORE_WRAPPING_SUB:
    case R_STANDARD_CALL_CORE_SATURATING_SUB:
        arithmetic = R_TOKEN_MINUS;
        break;
    default:
        arithmetic = R_TOKEN_STAR;
        break;
    }
    if ((operation == R_STANDARD_CALL_CORE_WRAPPING_ADD) ||
        (operation == R_STANDARD_CALL_CORE_WRAPPING_SUB) ||
        (operation == R_STANDARD_CALL_CORE_WRAPPING_MUL)) {
        *result = arithmetic == R_TOKEN_PLUS    ? (left + right) & mask
                  : arithmetic == R_TOKEN_MINUS ? (left - right) & mask
                                                : (left * right) & mask;
        return true;
    }
    if (is_signed) {
        if (r_constant_integer_binary(arithmetic, kind, left, right, result)) {
            return true;
        }
        /* The exact result lies beyond one bound; its sign selects which. A sum or difference
           overflows only away from the sign of its left operand. */
        if (arithmetic == R_TOKEN_STAR) {
            *result = (((left ^ right) & sign) != 0U) ? minimum : maximum;
        } else {
            *result = ((left & sign) != 0U) ? minimum : maximum;
        }
        return true;
    }
    if (arithmetic == R_TOKEN_PLUS) {
        *result = left > maximum - right ? maximum : left + right;
    } else if (arithmetic == R_TOKEN_MINUS) {
        *result = left < right ? minimum : left - right;
    } else {
        *result = (right != 0U) && (left > maximum / right) ? maximum : left * right;
    }
    return true;
}

/* ---- evaluation ---- */

static bool
r_consteval_expression(RConstEval *eval, RHirNodeId node_id, RConstCell *result, uint32_t depth);
static bool
r_consteval_frozen(RConstEval *eval, const RHirNode *node, RConstCell *result, uint32_t depth);
static bool r_consteval_statement(RConstEval *eval, RHirNodeId node_id, uint32_t depth);
static bool r_consteval_call(RConstEval *eval,
                             RSymbolId function_id,
                             RConstCell *arguments,
                             uint32_t argument_count,
                             RSourceSpan span,
                             RConstCell *result,
                             uint32_t depth);

/* R-META-0003: a call in a generic definition whose callee depends on the definition's
 * parameters: a generic function or trait prototype with dependent generic arguments. */
static bool r_consteval_open_call(const RFrontendContext *context, const RHirNode *node) {
    const RSemanticSymbol *callee;

    if ((node->kind != R_HIR_CALL) || (node->symbol == R_SYMBOL_ID_INVALID) ||
        ((size_t)node->symbol > context->semantic_symbol_count)) {
        return false;
    }
    callee = &context->semantic_symbols[(size_t)node->symbol - 1U];
    return (callee->generic_origin != R_SYMBOL_ID_INVALID) &&
           r_generic_function_is_open(context, callee);
}

/* The callee an instantiation deciding a dependent condition closed for a call of its generic
 * definition; any other callee is itself. */
static RSymbolId r_consteval_closed_callee(const RConstevalCallees *callees, RSymbolId callee) {
    size_t index;

    for (index = 0U; index + 1U < callees->count; index += 2U) {
        if (callees->pairs[index] == callee) {
            return callees->pairs[index + 1U];
        }
    }
    return callee;
}

/* The storage of a module constant or constant module object, evaluated on first use. */
static bool r_consteval_module_cell(
    RConstEval *eval, RSymbolId symbol_id, RSourceSpan span, RConstCell **cell, uint32_t depth) {
    RFrontendContext *context = eval->context;
    RConstCell **slot = r_consteval_slot(eval, symbol_id, span);
    RSemanticSymbol *symbol;
    RConstCell *value;

    if (slot == NULL) {
        return false;
    }
    if (*slot != NULL) {
        *cell = *slot;
        return true;
    }
    symbol = r_consteval_symbol(context, symbol_id);
    if (symbol == NULL) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    value = r_consteval_cells(eval, 1U, span);
    if (value == NULL) {
        return false;
    }
    if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) {
        if (symbol->consteval_pending) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        r_consteval_scalar_value(
            value, r_semantic_value_type(context, symbol->type), symbol->integer_value);
    } else if ((symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) &&
               (symbol->hir_node != R_HIR_NODE_ID_INVALID) && !symbol->is_thread_local &&
               !symbol->is_import && r_semantic_type_is_const(context, symbol->type)) {
        /* Only a constant module object has a translation-time value (R-EXPR-0022). */
        const RHirNodeId initializer = symbol->hir_node;
        if (!r_consteval_expression(eval, initializer, value, depth + 1U)) {
            return false;
        }
    } else {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    slot = r_consteval_slot(eval, symbol_id, span);
    if (slot == NULL) {
        return false;
    }
    *slot = value;
    *cell = value;
    return true;
}

/* A local outside the evaluated call: a const local of the enclosing function whose initializer
 * is itself a constant expression (R-EXPR-0022). */
static bool r_consteval_outer_local(
    RConstEval *eval, RSymbolId symbol_id, RSourceSpan span, RConstCell **cell, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RSemanticSymbol *symbol = r_consteval_symbol(context, symbol_id);
    RHirNode local;
    RConstCell *value;
    RConstCell **slot;

    if ((symbol == NULL) || (symbol->kind != R_SEMANTIC_SYMBOL_LOCAL) ||
        !r_semantic_type_is_const(context, symbol->type) ||
        !r_consteval_node_copy(context, symbol->hir_node, &local) || (local.kind != R_HIR_LOCAL) ||
        (local.child_count != 1U)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    value = r_consteval_cells(eval, 1U, span);
    if ((value == NULL) ||
        !r_consteval_expression(eval, r_consteval_child(context, &local, 0U), value, depth + 1U)) {
        return false;
    }
    slot = r_consteval_slot(eval, symbol_id, span);
    if (slot == NULL) {
        return false;
    }
    *slot = value;
    *cell = value;
    return true;
}

/* A field place or initializer names a program-wide field id; its position in the struct
 * value is relative to the struct's first field. */
static bool r_consteval_field_index(const RFrontendContext *context,
                                    RTypeId struct_type,
                                    uint32_t member,
                                    uint64_t count,
                                    uint64_t *index) {
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, struct_type));

    if ((aggregate == NULL) || (member <= aggregate->first_field) ||
        ((uint64_t)(member - aggregate->first_field - 1U) >= count)) {
        return false;
    }
    *index = (uint64_t)(member - aggregate->first_field - 1U);
    return true;
}

static bool
r_consteval_place(RConstEval *eval, RHirNodeId node_id, RConstCell **cell, uint32_t depth) {
    RFrontendContext *context = eval->context;
    RHirNode node;
    RConstCell *base = NULL;
    RConstCell index_value;
    RConstCell *temporary;
    const RSemanticSymbol *symbol;

    if (!r_consteval_node_copy(context, node_id, &node)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, eval->span);
    }
    if (!r_consteval_step(eval, node.span)) {
        return false;
    }
    switch (node.kind) {
    case R_HIR_PLACE: {
        RConstCell **slot;
        symbol = r_consteval_symbol(context, node.symbol);
        if (symbol == NULL) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        if ((symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) ||
            (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT)) {
            return r_consteval_module_cell(eval, node.symbol, node.span, cell, depth);
        }
        slot = r_consteval_slot(eval, node.symbol, node.span);
        if (slot == NULL) {
            return false;
        }
        if (*slot == NULL) {
            return r_consteval_outer_local(eval, node.symbol, node.span, cell, depth);
        }
        *cell = *slot;
        return true;
    }
    case R_HIR_FIELD_PLACE: {
        const RHirNodeId operand = r_consteval_child(context, &node, 0U);
        RHirNode operand_node;
        uint64_t field_index;
        if (!r_consteval_node_copy(context, operand, &operand_node) ||
            !r_consteval_place(eval, operand, &base, depth + 1U)) {
            return eval->failure == R_CONST_FAILURE_NONE
                       ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span)
                       : false;
        }
        /* The field is counted in the operand's type: a member of an error family read as its
           ancestor keeps the ancestor's fields as its prefix (R-AGG-0011). */
        if ((base->kind != R_CONST_AGGREGATE) ||
            !r_consteval_field_index(
                context, operand_node.type, node.aggregate_member, base->count, &field_index)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        *cell = &base->items[field_index];
        return true;
    }
    case R_HIR_INDEX_PLACE: {
        RConstCell *elements;
        uint64_t count;
        if (!r_consteval_place(eval, r_consteval_child(context, &node, 0U), &base, depth + 1U) ||
            !r_consteval_expression(
                eval, r_consteval_child(context, &node, 1U), &index_value, depth + 1U)) {
            return false;
        }
        if ((base->kind == R_CONST_AGGREGATE) || (base->kind == R_CONST_CONTAINER)) {
            elements = base->items;
            count = base->count;
        } else if (base->kind == R_CONST_SLICE) {
            elements = base->items;
            count = base->count;
        } else if (base->kind == R_CONST_TEXT) {
            const RInternEntry *entry;
            if (index_value.bits >= base->count) {
                return r_consteval_panic(eval, "bounds", node.span);
            }
            if ((base->bits == 0U) || (base->bits > (uint64_t)context->intern_count)) {
                return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
            }
            entry = &context->intern_entries[(size_t)base->bits - 1U];
            temporary = r_consteval_cells(eval, 1U, node.span);
            if (temporary == NULL) {
                return false;
            }
            r_consteval_scalar_value(temporary,
                                     r_semantic_value_type(context, node.type),
                                     (uint64_t)(unsigned char)entry->bytes[index_value.bits]);
            *cell = temporary;
            return true;
        } else {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        if (index_value.bits >= count) {
            return r_consteval_panic(eval, "bounds", node.span);
        }
        *cell = &elements[index_value.bits];
        return true;
    }
    case R_HIR_DEREF_PLACE: {
        const RHirNodeId operand = r_consteval_child(context, &node, 0U);
        RHirNode operand_node;
        RConstCell reference;
        if (!r_consteval_node_copy(context, operand, &operand_node)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        if ((operand_node.kind == R_HIR_PLACE) || (operand_node.kind == R_HIR_FIELD_PLACE) ||
            (operand_node.kind == R_HIR_INDEX_PLACE) || (operand_node.kind == R_HIR_DEREF_PLACE)) {
            if (!r_consteval_place(eval, operand, &base, depth + 1U)) {
                return false;
            }
        } else {
            if (!r_consteval_expression(eval, operand, &reference, depth + 1U)) {
                return false;
            }
            base = &reference;
        }
        if ((base->kind != R_CONST_REFERENCE) || (base->items == NULL)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        *cell = base->items;
        return true;
    }
    default:
        temporary = r_consteval_cells(eval, 1U, node.span);
        if ((temporary == NULL) || !r_consteval_expression(eval, node_id, temporary, depth)) {
            return false;
        }
        *cell = temporary;
        return true;
    }
}

static bool r_consteval_cast(RConstEval *eval,
                             const RHirNode *node,
                             const RConstCell *operand,
                             RConstCell *result) {
    RFrontendContext *context = eval->context;
    const RTypeId target = r_semantic_value_type(context, node->type);
    const RSemanticTypeKind target_kind = r_semantic_value_kind(context, target);
    const RSemanticTypeKind source_kind = r_semantic_value_kind(context, operand->type);
    const bool explicit_cast = node->operation == R_TOKEN_KW_AS;
    RSemanticTypeKind source_integer;
    RSemanticTypeKind target_integer;
    uint64_t bits;

    if (operand->kind == R_CONST_TEXT) {
        *result = *operand;
        result->type = target;
        return true;
    }
    if (operand->kind != R_CONST_SCALAR) {
        if (r_semantic_value_type(context, operand->type) == target) {
            *result = *operand;
            return true;
        }
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    if (r_semantic_value_type(context, operand->type) == target) {
        *result = *operand;
        return true;
    }
    if ((r_consteval_float_width(context, operand->type) != 0U) ||
        (r_consteval_float_width(context, target) != 0U)) {
        bool invalid = false;
        if (!r_consteval_float_convert(
                context, operand->type, target, operand->bits, &invalid, &bits)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        if (invalid) {
            return r_consteval_panic(eval, "invalid_conversion", node->span);
        }
        r_consteval_scalar_value(result, target, bits);
        return true;
    }
    target_integer = r_consteval_integer_kind(context, target);
    if (source_kind == R_SEMANTIC_TYPE_BOOL) {
        bits = operand->bits;
        if (target_integer == R_SEMANTIC_TYPE_INVALID) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
    } else {
        source_integer = r_consteval_integer_kind(context, operand->type);
        if ((source_integer == R_SEMANTIC_TYPE_INVALID) ||
            (target_integer == R_SEMANTIC_TYPE_INVALID)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        if (!r_consteval_convert_integer(
                operand->bits, source_integer, target_integer, explicit_cast, &bits)) {
            return r_consteval_panic(eval, "invalid_conversion", node->span);
        }
    }
    if (explicit_cast && (target_kind == R_SEMANTIC_TYPE_CHAR) &&
        ((bits > UINT64_C(0x10FFFF)) || ((bits - UINT64_C(0xD800)) < UINT64_C(0x800)))) {
        return r_consteval_panic(eval, "invalid_conversion", node->span);
    }
    if (target_kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_consteval_fieldless_enum(context, target);
        uint32_t variant_index;
        bool declared = false;
        if (aggregate == NULL) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        for (variant_index = 0U; variant_index < aggregate->variant_count; ++variant_index) {
            if (context->semantic_variants[(size_t)aggregate->first_variant + variant_index]
                    .value == bits) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            return r_consteval_panic(eval, "invalid_conversion", node->span);
        }
    }
    r_consteval_scalar_value(result, target, bits);
    return true;
}

/* R-REFL-0001..0003, R-META-0003: a reflection constant over a type parameter, answered for
 * the instantiation being decided. */
static bool r_consteval_reflection(RConstEval *eval, const RHirNode *source, RConstCell *result) {
    RFrontendContext *context = eval->context;
    RHirNode node = *source;

    if ((eval->schema != 0U) &&
        (!r_generic_substitute_type(
             context, node.auxiliary_type, eval->schema, eval->arguments, &node.auxiliary_type) ||
         !r_generic_substitute_type(
             context, node.type, eval->schema, eval->arguments, &node.type))) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
    }
    if (!r_reflection_fold_clone(context, &node)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, node.span);
    }
    switch (node.kind) {
    case R_HIR_LITERAL:
    case R_HIR_ENUM_CONSTANT:
        r_consteval_scalar_value(
            result, r_semantic_value_type(context, node.type), node.integer_value);
        return true;
    case R_HIR_STRING_LITERAL:
        (void)memset(result, 0, sizeof(*result));
        result->kind = R_CONST_TEXT;
        result->type = r_semantic_value_type(context, node.type);
        result->bits = node.aggregate_member;
        result->count = node.integer_value;
        return true;
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
    }
}

#include "consteval_containers.inc"

static bool r_consteval_standard_call(RConstEval *eval,
                                      const RHirNode *node,
                                      RConstCell *result,
                                      uint32_t depth) {
    RFrontendContext *context = eval->context;
    RConstCell left;
    RConstCell right;
    const RSemanticAggregate *aggregate;
    uint32_t variant_index;
    uint64_t bits;

    if (r_consteval_owner_operation(node->standard_operation)) {
        return r_consteval_owner_call(eval, node, result, depth);
    }
    if (r_consteval_math_allowed(context, node)) {
        const char *name = r_consteval_math_name(node);
        bool boolean = false;
        bool domain = false;
        right = (RConstCell){0};
        if (!r_consteval_expression(
                eval, r_consteval_child(context, node, 0U), &left, depth + 1U) ||
            ((node->child_count == 2U) &&
             !r_consteval_expression(
                 eval, r_consteval_child(context, node, 1U), &right, depth + 1U))) {
            return false;
        }
        if (!r_consteval_math_apply(name,
                                    r_consteval_float_width(context, left.type),
                                    left.bits,
                                    right.bits,
                                    &boolean,
                                    &domain,
                                    &bits)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        if (domain) {
            return r_consteval_throw_standard(eval, "std.math::math_error", node->span);
        }
        (void)boolean;
        r_consteval_scalar_value(result, r_semantic_value_type(context, node->type), bits);
        return true;
    }
    if (!r_consteval_standard_operation_allowed(node->standard_operation)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    if (node->standard_operation == R_STANDARD_CALL_CORE_PANIC) {
        if ((node->child_count == 1U) &&
            r_consteval_expression(eval, r_consteval_child(context, node, 0U), &left, depth + 1U) &&
            (left.kind == R_CONST_TEXT)) {
            eval->message = left;
        }
        if (eval->failure != R_CONST_FAILURE_NONE) {
            return false;
        }
        return r_consteval_panic(eval, "explicit", node->span);
    }
    if ((node->child_count == 0U) ||
        !r_consteval_expression(eval, r_consteval_child(context, node, 0U), &left, depth + 1U)) {
        return false;
    }
    switch (node->standard_operation) {
    case R_STANDARD_CALL_CORE_ENUM_NAME:
    case R_STANDARD_CALL_CORE_ENUM_ORDINAL:
        aggregate = r_consteval_fieldless_enum(context, left.type);
        if (aggregate == NULL) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        for (variant_index = 0U; variant_index < aggregate->variant_count; ++variant_index) {
            const RSemanticVariant *variant =
                &context->semantic_variants[(size_t)aggregate->first_variant + variant_index];
            const RSource *source;
            uint32_t intern_id = 0U;
            if (variant->value != left.bits) {
                continue;
            }
            if (node->standard_operation == R_STANDARD_CALL_CORE_ENUM_ORDINAL) {
                r_consteval_scalar_value(result,
                                         r_semantic_value_type(context, node->type),
                                         (uint64_t)variant->declaration_index);
                return true;
            }
            source = r_get_source_const(context, variant->name_span.source);
            if ((source == NULL) || (variant->name_span.end < variant->name_span.start) ||
                !r_intern_bytes(context,
                                source->bytes + variant->name_span.start,
                                (size_t)(variant->name_span.end - variant->name_span.start),
                                &intern_id)) {
                return r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, node->span);
            }
            (void)memset(result, 0, sizeof(*result));
            result->kind = R_CONST_TEXT;
            result->type = r_semantic_value_type(context, node->type);
            result->bits = intern_id;
            result->count = (uint64_t)(variant->name_span.end - variant->name_span.start);
            return true;
        }
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    default:
        if ((node->child_count != 2U) ||
            !r_consteval_expression(
                eval, r_consteval_child(context, node, 1U), &right, depth + 1U)) {
            return false;
        }
        if (!r_consteval_core_integer(eval,
                                      node->standard_operation,
                                      node->type,
                                      left.bits,
                                      right.bits,
                                      node->span,
                                      &bits)) {
            return false;
        }
        r_consteval_scalar_value(result, r_semantic_value_type(context, node->type), bits);
        return true;
    }
}

static bool r_consteval_assign(RConstEval *eval, const RHirNode *node, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RHirNodeId place_id = r_consteval_child(context, node, 0U);
    RHirNode place_node;
    RConstCell *place = NULL;
    RConstCell value;
    RTypeId calculation;
    RSemanticTypeKind calculation_kind;
    RSemanticTypeKind place_kind;
    uint64_t old_bits;
    uint64_t new_bits;
    bool is_signed;
    uint32_t width;

    if (!r_consteval_node_copy(context, place_id, &place_node) ||
        !r_consteval_place(eval, place_id, &place, depth + 1U)) {
        return eval->failure == R_CONST_FAILURE_NONE
                   ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                   : false;
    }
    if (node->kind == R_HIR_ASSIGN) {
        if (!r_consteval_expression(
                eval, r_consteval_child(context, node, 1U), &value, depth + 1U)) {
            return false;
        }
        return r_consteval_store(eval, place, &value, node->span);
    }
    /* Compound assignment: the old value is read before the right operand, computed in the
       calculation type and narrowed; a signed destination traps, an unsigned one wraps. */
    if (place->kind != R_CONST_SCALAR) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    calculation =
        node->auxiliary_type != R_TYPE_ID_INVALID ? node->auxiliary_type : place_node.type;
    calculation_kind = r_consteval_integer_kind(context, calculation);
    place_kind = r_consteval_integer_kind(context, place_node.type);
    old_bits = place->bits;
    if ((r_semantic_value_kind(context, calculation) != R_SEMANTIC_TYPE_BOOL) &&
        (calculation_kind != place_kind)) {
        if ((calculation_kind == R_SEMANTIC_TYPE_INVALID) ||
            (place_kind == R_SEMANTIC_TYPE_INVALID) ||
            !r_consteval_convert_integer(
                old_bits, place_kind, calculation_kind, false, &old_bits)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
    }
    if (!r_consteval_expression(eval, r_consteval_child(context, node, 1U), &value, depth + 1U)) {
        return false;
    }
    if (!r_consteval_arithmetic(eval,
                                r_consteval_compound_operator(node->operation),
                                calculation,
                                old_bits,
                                value.bits,
                                node->span,
                                &new_bits)) {
        return false;
    }
    if ((r_semantic_value_kind(context, calculation) != R_SEMANTIC_TYPE_BOOL) &&
        (calculation_kind != place_kind)) {
        if (!r_semantic_integer_properties(place_kind, &is_signed, &width)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        if (!r_consteval_convert_integer(
                new_bits, calculation_kind, place_kind, is_signed, &new_bits)) {
            return r_consteval_panic(eval, "integer_overflow", node->span);
        }
    }
    place->bits = new_bits;
    return true;
}

static bool
r_consteval_expression(RConstEval *eval, RHirNodeId node_id, RConstCell *result, uint32_t depth) {
    RFrontendContext *context = eval->context;
    RHirNode node;
    RConstCell left;
    RConstCell right;
    RConstCell *place = NULL;
    uint64_t size;
    uint64_t alignment;
    uint64_t bits;
    uint32_t child_index;
    bool comparison;

    (void)memset(result, 0, sizeof(*result));
    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, eval->span);
    }
    if (!r_consteval_step(eval, node.span)) {
        return false;
    }
    switch (node.kind) {
    case R_HIR_LITERAL:
    case R_HIR_ENUM_CONSTANT:
        r_consteval_scalar_value(
            result, r_semantic_value_type(context, node.type), node.integer_value);
        return true;
    case R_HIR_STRING_LITERAL:
        result->kind = R_CONST_TEXT;
        result->type = r_semantic_value_type(context, node.type);
        result->bits = node.aggregate_member;
        result->count = node.integer_value;
        return true;
    case R_HIR_TYPE_QUERY: {
        RTypeId queried = node.auxiliary_type;
        if (((eval->schema != 0U) &&
             !r_generic_substitute_type(
                 context, queried, eval->schema, eval->arguments, &queried)) ||
            !r_generic_layout(context, queried, &size, &alignment, 0U)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        r_consteval_scalar_value(result,
                                 r_semantic_value_type(context, node.type),
                                 node.operation == R_TOKEN_KW_SIZEOF ? size : alignment);
        return true;
    }
    case R_HIR_GENERIC_CONSTANT: {
        /* A constant generic parameter of the instantiation being decided (R-META-0003). */
        RTypeId constant = node.auxiliary_type;
        if ((eval->schema == 0U) ||
            !r_generic_substitute_type(
                context, constant, eval->schema, eval->arguments, &constant) ||
            r_generic_type_is_dependent(context, constant, 0U) ||
            !r_generic_constant_evaluate(context, constant, &bits, 0U)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        r_consteval_scalar_value(result, r_semantic_value_type(context, node.type), bits);
        return true;
    }
    case R_HIR_DEFAULT_VALUE:
        return r_consteval_zero(eval, node.type, result, node.span, 0U);
    case R_HIR_ARRAY_INIT:
        if (r_consteval_owner(context, node.type) != R_CONSTEVAL_OWNER_NONE) {
            return r_consteval_frozen(eval, &node, result, depth);
        }
        if (!r_consteval_zero(eval, node.type, result, node.span, 0U)) {
            return false;
        }
        if ((result->kind != R_CONST_AGGREGATE) || ((uint64_t)node.child_count > result->count)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        for (child_index = 0U; child_index < node.child_count; ++child_index) {
            if (!r_consteval_expression(
                    eval, r_consteval_child(context, &node, child_index), &left, depth + 1U) ||
                !r_consteval_store(eval, &result->items[child_index], &left, node.span)) {
                return false;
            }
        }
        return true;
    case R_HIR_AGGREGATE_INIT:
        if (!r_consteval_zero(eval, node.type, result, node.span, 0U)) {
            return false;
        }
        for (child_index = 0U; child_index < node.child_count; ++child_index) {
            RHirNode field;
            uint64_t field_index;
            if (!r_consteval_node_copy(
                    context, r_consteval_child(context, &node, child_index), &field) ||
                (field.kind != R_HIR_FIELD_INIT) || (field.child_count != 1U) ||
                !r_consteval_field_index(
                    context, node.type, field.aggregate_member, result->count, &field_index)) {
                return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
            }
            if (!r_consteval_expression(
                    eval, r_consteval_child(context, &field, 0U), &left, depth + 1U) ||
                !r_consteval_store(eval, &result->items[field_index], &left, field.span)) {
                return false;
            }
        }
        return true;
    case R_HIR_PLACE:
    case R_HIR_FIELD_PLACE:
    case R_HIR_INDEX_PLACE:
    case R_HIR_DEREF_PLACE:
    case R_HIR_LOAD:
    case R_HIR_MOVE:
        if (!r_consteval_place(eval,
                               (node.kind == R_HIR_LOAD) || (node.kind == R_HIR_MOVE)
                                   ? r_consteval_child(context, &node, 0U)
                                   : node_id,
                               &place,
                               depth + 1U)) {
            return false;
        }
        if (place->kind == R_CONST_UNSET) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        return r_consteval_clone(eval, place, result, node.span);
    case R_HIR_BORROW:
        if (!r_consteval_place(eval, r_consteval_child(context, &node, 0U), &place, depth + 1U)) {
            return false;
        }
        result->kind = R_CONST_REFERENCE;
        result->type = r_semantic_value_type(context, node.type);
        result->items = place;
        return true;
    case R_HIR_SLICE:
        if (!r_consteval_place(eval, r_consteval_child(context, &node, 0U), &place, depth + 1U)) {
            return false;
        }
        if ((place->kind == R_CONST_REFERENCE) && (place->items != NULL) &&
            (place->items->kind == R_CONST_CONTAINER)) {
            /* L22.3: the elements of an array owner (`source=std.array`). */
            place = place->items;
        }
        if ((place->kind != R_CONST_AGGREGATE) && (place->kind != R_CONST_SLICE) &&
            (place->kind != R_CONST_CONTAINER)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        result->kind = R_CONST_SLICE;
        result->type = r_semantic_value_type(context, node.type);
        result->items = place->items;
        result->count = place->count;
        return true;
    case R_HIR_LENGTH:
        if (node.integer_value != UINT64_MAX) {
            r_consteval_scalar_value(
                result, r_semantic_value_type(context, node.type), node.integer_value);
            return true;
        }
        if (!r_consteval_place(eval, r_consteval_child(context, &node, 0U), &place, depth + 1U)) {
            return false;
        }
        if ((place->kind != R_CONST_AGGREGATE) && (place->kind != R_CONST_SLICE) &&
            (place->kind != R_CONST_TEXT) && (place->kind != R_CONST_CONTAINER)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        r_consteval_scalar_value(result, r_semantic_value_type(context, node.type), place->count);
        return true;
    case R_HIR_UNARY: {
        const RSemanticTypeKind kind = r_consteval_integer_kind(context, node.type);
        bool is_signed;
        uint32_t width;
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        if ((node.operation == R_TOKEN_BANG) &&
            (r_semantic_value_kind(context, node.type) == R_SEMANTIC_TYPE_BOOL)) {
            r_consteval_scalar_value(result,
                                     r_semantic_value_type(context, node.type),
                                     left.bits == 0U ? UINT64_C(1) : UINT64_C(0));
            return true;
        }
        if (r_consteval_float_width(context, node.type) != 0U) {
            /* R-EXPR-0025: negation flips the sign, also of a zero or NaN. */
            const uint32_t float_width = r_consteval_float_width(context, node.type);
            if ((node.operation != R_TOKEN_PLUS) && (node.operation != R_TOKEN_MINUS)) {
                return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
            }
            r_consteval_scalar_value(result,
                                     r_semantic_value_type(context, node.type),
                                     node.operation == R_TOKEN_MINUS
                                         ? left.bits ^ (UINT64_C(1) << (float_width - 1U))
                                         : left.bits);
            return true;
        }
        if (!r_semantic_integer_properties(kind, &is_signed, &width)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        switch (node.operation) {
        case R_TOKEN_PLUS:
            bits = left.bits;
            break;
        case R_TOKEN_MINUS:
            if (is_signed && (left.bits == (UINT64_C(1) << (width - 1U)))) {
                return r_consteval_panic(eval, "integer_overflow", node.span);
            }
            bits = (UINT64_C(0) - left.bits) & r_constant_integer_mask(width);
            break;
        case R_TOKEN_TILDE:
            bits = (~left.bits) & r_constant_integer_mask(width);
            break;
        default:
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        r_consteval_scalar_value(result, r_semantic_value_type(context, node.type), bits);
        return true;
    }
    case R_HIR_BINARY:
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        if ((node.operation == R_TOKEN_AMP_AMP) || (node.operation == R_TOKEN_PIPE_PIPE)) {
            if ((node.operation == R_TOKEN_AMP_AMP) == (left.bits == 0U)) {
                r_consteval_scalar_value(
                    result, r_semantic_value_type(context, node.type), left.bits);
                return true;
            }
            return r_consteval_expression(
                eval, r_consteval_child(context, &node, 1U), result, depth + 1U);
        }
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 1U), &right, depth + 1U)) {
            return false;
        }
        if ((node.operation == R_TOKEN_EQUAL_EQUAL) || (node.operation == R_TOKEN_BANG_EQUAL) ||
            (node.operation == R_TOKEN_LESS) || (node.operation == R_TOKEN_LESS_EQUAL) ||
            (node.operation == R_TOKEN_GREATER) || (node.operation == R_TOKEN_GREATER_EQUAL)) {
            if (!r_consteval_compare(eval, node.operation, &left, &right, node.span, &comparison)) {
                return false;
            }
            r_consteval_scalar_value(result,
                                     r_semantic_value_type(context, node.type),
                                     comparison ? UINT64_C(1) : UINT64_C(0));
            return true;
        }
        if (!r_consteval_arithmetic(
                eval, node.operation, node.type, left.bits, right.bits, node.span, &bits)) {
            return false;
        }
        r_consteval_scalar_value(result, r_semantic_value_type(context, node.type), bits);
        return true;
    case R_HIR_CONDITIONAL:
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        return r_consteval_expression(
            eval, r_consteval_child(context, &node, left.bits != 0U ? 1U : 2U), result, depth + 1U);
    case R_HIR_CAST:
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        return r_consteval_cast(eval, &node, &left, result);
    case R_HIR_CALL: {
        RConstCell *arguments = NULL;
        if (node.child_count != 0U) {
            arguments = r_consteval_cells(eval, node.child_count, node.span);
            if (arguments == NULL) {
                return false;
            }
        }
        for (child_index = 0U; child_index < node.child_count; ++child_index) {
            if (!r_consteval_expression(eval,
                                        r_consteval_child(context, &node, child_index),
                                        &arguments[child_index],
                                        depth + 1U)) {
                return false;
            }
        }
        return r_consteval_call(eval,
                                r_consteval_closed_callee(&eval->callees, node.symbol),
                                arguments,
                                node.child_count,
                                node.span,
                                result,
                                depth + 1U);
    }
    case R_HIR_STANDARD_CALL:
        if (r_reflection_is_operation(node.standard_operation) && (node.child_count == 0U)) {
            return r_consteval_reflection(eval, &node, result);
        }
        return r_consteval_standard_call(eval, &node, result, depth);
    case R_HIR_VALUE_SCOPE:
        for (child_index = 0U; child_index < node.child_count; ++child_index) {
            const RHirNodeId child = r_consteval_child(context, &node, child_index);
            if ((uint64_t)child_index == node.integer_value) {
                continue;
            }
            if (!r_consteval_statement(eval, child, depth + 1U)) {
                return false;
            }
            if (eval->flow != R_CONST_FLOW_NORMAL) {
                return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
            }
        }
        return r_consteval_expression(
            eval,
            r_consteval_child(context, &node, (uint32_t)node.integer_value),
            result,
            depth + 1U);
    case R_HIR_DISCARD:
        return r_consteval_expression(
            eval, r_consteval_child(context, &node, 0U), &left, depth + 1U);
    case R_HIR_ASSIGN:
    case R_HIR_COMPOUND_ASSIGN:
        return r_consteval_assign(eval, &node, depth);
    case R_HIR_DROP:
        /* L22.3: the evaluation arena is released as a whole. */
        return true;
    case R_HIR_VARIANT:
        /* L22.1, L22.3: a variant of a tagged enumeration or an option, or an error that becomes
           a value of its family; `count` is the number of payload cells. */
        result->kind = R_CONST_VARIANT;
        result->type = r_semantic_value_type(context, node.type);
        result->bits = node.integer_value;
        result->count = node.child_count;
        if (node.child_count == 0U) {
            return true;
        }
        result->items = r_consteval_cells(eval, 1U, node.span);
        if ((result->items == NULL) || (node.child_count != 1U)) {
            return (result->items == NULL)
                       ? false
                       : r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        return r_consteval_store(eval, result->items, &left, node.span);
    case R_HIR_VARIANT_TAG:
    case R_HIR_VARIANT_PAYLOAD:
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        place = left.kind == R_CONST_REFERENCE ? left.items : &left;
        if ((place == NULL) || (place->kind != R_CONST_VARIANT)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        if (node.kind == R_HIR_VARIANT_TAG) {
            r_consteval_scalar_value(
                result, r_semantic_value_type(context, node.type), place->bits);
            return true;
        }
        /* Variant zero of an error family reads the fields of its error in any member. */
        if ((place->count != 1U) ||
            ((node.integer_value != place->bits) &&
             ((node.integer_value != 0U) ||
              (r_semantic_tagged_enum(context, place->type) == NULL) ||
              (r_semantic_tagged_enum(context, place->type)->error_family_of == 0U)))) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        if (node.operation == R_TOKEN_KW_MOVE) {
            return r_consteval_clone(eval, place->items, result, node.span);
        }
        if (left.kind != R_CONST_REFERENCE) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
        }
        result->kind = R_CONST_REFERENCE;
        result->type = r_semantic_value_type(context, node.type);
        result->items = place->items;
        return true;
    case R_HIR_THROW:
        /* L22.1 (R-ERR-0002): the error leaves every call up to the try that catches it. */
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &left, depth + 1U)) {
            return false;
        }
        if (eval->failure == R_CONST_FAILURE_NONE) {
            eval->thrown = left;
            eval->thrown_type = r_semantic_value_type(context, node.auxiliary_type);
        }
        return r_consteval_fail(eval, R_CONST_FAILURE_THROW, node.span);
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node.span);
    }
}

/* L22.3: a variant pattern binds a borrow of the payload, or a copy or move of it. */
static bool
r_consteval_bind_payload(RConstEval *eval, const RHirNode *clause, const RConstCell *tagged) {
    RConstCell bound;
    RConstCell **slot;

    if ((tagged->count != 1U) || (tagged->items == NULL)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, clause->span);
    }
    (void)memset(&bound, 0, sizeof(bound));
    if (r_semantic_value_kind(eval->context, clause->type) == R_SEMANTIC_TYPE_BORROW) {
        bound.kind = R_CONST_REFERENCE;
        bound.type = r_semantic_value_type(eval->context, clause->type);
        bound.items = tagged->items;
    } else if (!r_consteval_clone(eval, tagged->items, &bound, clause->span)) {
        return false;
    }
    slot = r_consteval_slot(eval, clause->symbol, clause->span);
    if (slot == NULL) {
        return false;
    }
    if (*slot == NULL) {
        *slot = r_consteval_cells(eval, 1U, clause->span);
        if (*slot == NULL) {
            return false;
        }
    }
    (*slot)->kind = R_CONST_UNSET;
    return r_consteval_store(eval, *slot, &bound, clause->span);
}

static bool r_consteval_switch(RConstEval *eval, const RHirNode *node, uint32_t depth) {
    RFrontendContext *context = eval->context;
    RConstCell scrutinee;
    const RConstCell *tagged;
    uint32_t case_index;
    uint32_t selected = 0U;

    if (!r_consteval_expression(
            eval, r_consteval_child(context, node, 0U), &scrutinee, depth + 1U)) {
        return false;
    }
    /* A switch over a tagged value reads it through a borrow (R-STMT-0010). */
    tagged = scrutinee.kind == R_CONST_REFERENCE ? scrutinee.items : &scrutinee;
    if ((tagged == NULL) || (tagged->kind != R_CONST_VARIANT)) {
        tagged = NULL;
    }
    for (case_index = 1U; case_index < node->child_count; ++case_index) {
        RHirNode clause;
        if (!r_consteval_node_copy(
                context, r_consteval_child(context, node, case_index), &clause)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        if (((clause.case_pattern == R_HIR_CASE_PATTERN_CONSTANT) && (tagged == NULL) &&
             (clause.integer_value == scrutinee.bits)) ||
            ((clause.case_pattern == R_HIR_CASE_PATTERN_VARIANT) && (tagged != NULL) &&
             (clause.integer_value == tagged->bits))) {
            if ((clause.symbol != R_SYMBOL_ID_INVALID) &&
                !r_consteval_bind_payload(eval, &clause, tagged)) {
                return false;
            }
            selected = case_index;
            break;
        }
    }
    if (selected == 0U) {
        for (case_index = 1U; case_index < node->child_count; ++case_index) {
            RHirNode clause;
            if (r_consteval_node_copy(
                    context, r_consteval_child(context, node, case_index), &clause) &&
                (clause.case_pattern == R_HIR_CASE_PATTERN_DEFAULT)) {
                selected = case_index;
                break;
            }
        }
    }
    for (case_index = selected; (case_index != 0U) && (case_index < node->child_count);
         ++case_index) {
        RHirNode clause;
        if (!r_consteval_node_copy(
                context, r_consteval_child(context, node, case_index), &clause) ||
            (clause.child_count == 0U) ||
            !r_consteval_statement(eval, r_consteval_child(context, &clause, 0U), depth + 1U)) {
            return eval->failure == R_CONST_FAILURE_NONE
                       ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                       : false;
        }
        if (eval->flow == R_CONST_FLOW_BREAK_SWITCH) {
            eval->flow = R_CONST_FLOW_NORMAL;
            return true;
        }
        if ((eval->flow != R_CONST_FLOW_NORMAL) || (clause.operation != R_TOKEN_KW_FALLTHROUGH)) {
            return true;
        }
    }
    return true;
}

static bool r_consteval_loop(RConstEval *eval, const RHirNode *node, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const bool is_for = node->kind == R_HIR_FOR;
    RConstCell condition;

    for (;;) {
        if (!r_consteval_step(eval, node->span) ||
            !r_consteval_expression(
                eval, r_consteval_child(context, node, 0U), &condition, depth + 1U)) {
            return false;
        }
        if (condition.bits == 0U) {
            return true;
        }
        if (!r_consteval_statement(eval, r_consteval_child(context, node, 1U), depth + 1U)) {
            return false;
        }
        /* R-STMT-0004: a labeled jump to an outer loop leaves this one as well. */
        if (((eval->flow == R_CONST_FLOW_BREAK) || (eval->flow == R_CONST_FLOW_CONTINUE)) &&
            (eval->flow_loops != 0U)) {
            eval->flow_loops -= 1U;
            return true;
        }
        if (eval->flow == R_CONST_FLOW_BREAK) {
            eval->flow = R_CONST_FLOW_NORMAL;
            return true;
        }
        if (eval->flow == R_CONST_FLOW_CONTINUE) {
            eval->flow = R_CONST_FLOW_NORMAL;
        }
        if (eval->flow != R_CONST_FLOW_NORMAL) {
            return true;
        }
        if (is_for && (node->child_count > 2U) &&
            !r_consteval_statement(eval, r_consteval_child(context, node, 2U), depth + 1U)) {
            return false;
        }
    }
}

/* Ends the throw in flight: a catch clause received the error or a finally clause runs. */
static void r_consteval_clear_throw(RConstEval *eval, uint32_t active_count) {
    r_context_free(eval->context, eval->chain);
    eval->chain = NULL;
    eval->chain_count = 0U;
    eval->failure = R_CONST_FAILURE_NONE;
    (void)memset(&eval->thrown, 0, sizeof(eval->thrown));
    eval->thrown_type = R_TYPE_ID_INVALID;
    eval->active_count = active_count;
    eval->flow = R_CONST_FLOW_NORMAL;
}

/*
 * L22.1 (R-ERR-0003): a try statement. A throw from its block selects the clause of the nearest
 * ancestor of the error's type; a finally clause runs after the block or clause on every path,
 * and a throw, return or jump in flight continues after it. A panic or an exceeded limit ends
 * the whole evaluation and runs no clause.
 */
static bool r_consteval_try(RConstEval *eval, const RHirNode *node, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const uint32_t catch_count = (uint32_t)node->integer_value;
    const uint32_t active_count = eval->active_count;
    bool completed;

    if ((node->child_count == 0U) || ((uint64_t)node->child_count < node->integer_value + 1U)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    completed = r_consteval_statement(eval, r_consteval_child(context, node, 0U), depth + 1U);
    if (!completed && (eval->failure == R_CONST_FAILURE_THROW)) {
        uint32_t nearest = UINT32_MAX;
        uint32_t selected = 0U;
        uint32_t clause_index;
        RHirNode clause;

        for (clause_index = 1U; clause_index <= catch_count; ++clause_index) {
            uint32_t distance;
            if (!r_consteval_node_copy(
                    context, r_consteval_child(context, node, clause_index), &clause) ||
                (clause.kind != R_HIR_CATCH)) {
                return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
            }
            distance = r_semantic_error_catch_distance(
                context, r_semantic_value_type(context, clause.type), eval->thrown_type);
            if (distance < nearest) {
                nearest = distance;
                selected = clause_index;
            }
        }
        if (selected != 0U) {
            const RConstCell thrown = eval->thrown;
            const RTypeId thrown_type = eval->thrown_type;
            RConstCell received = thrown;
            RConstCell **slot;
            RTypeId clause_type;

            if (!r_consteval_node_copy(
                    context, r_consteval_child(context, node, selected), &clause) ||
                (clause.child_count != 1U)) {
                return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
            }
            clause_type = r_semantic_value_type(context, clause.type);
            r_consteval_clear_throw(eval, active_count);
            if (clause_type != thrown_type) {
                /* A clause of an ancestor receives the member of its family (R-AGG-0011). */
                const uint32_t tag = r_semantic_error_family_tag(context, clause_type, thrown_type);
                if (tag == UINT32_MAX) {
                    return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, clause.span);
                }
                (void)memset(&received, 0, sizeof(received));
                received.kind = R_CONST_VARIANT;
                received.type = clause_type;
                received.bits = tag;
                received.count = 1U;
                received.items = r_consteval_cells(eval, 1U, clause.span);
                if ((received.items == NULL) ||
                    !r_consteval_store(eval, received.items, &thrown, clause.span)) {
                    return false;
                }
            }
            slot = r_consteval_slot(eval, clause.symbol, clause.span);
            if (slot == NULL) {
                return false;
            }
            if (*slot == NULL) {
                *slot = r_consteval_cells(eval, 1U, clause.span);
                if (*slot == NULL) {
                    return false;
                }
            }
            (*slot)->kind = R_CONST_UNSET;
            completed =
                r_consteval_store(eval, *slot, &received, clause.span) &&
                r_consteval_statement(eval, r_consteval_child(context, &clause, 0U), depth + 1U);
        }
    }
    if ((uint64_t)node->child_count > node->integer_value + 1U) {
        const bool throwing = !completed;
        const RConstCell thrown = eval->thrown;
        const RTypeId thrown_type = eval->thrown_type;
        const RSourceSpan span = eval->span;
        const RConstFlow flow = eval->flow;
        const uint32_t flow_loops = eval->flow_loops;
        const RConstCell returned = eval->returned;
        RSymbolId *chain = NULL;
        uint32_t chain_count = 0U;
        RHirNode clause;

        if (throwing && (eval->failure != R_CONST_FAILURE_THROW)) {
            return false;
        }
        if (throwing) {
            /* The report names the calls that were active where the error was thrown. */
            chain = eval->chain;
            chain_count = eval->chain_count;
            eval->chain = NULL;
            eval->chain_count = 0U;
            r_consteval_clear_throw(eval, active_count);
        }
        eval->flow = R_CONST_FLOW_NORMAL;
        if (!r_consteval_node_copy(
                context, r_consteval_child(context, node, node->child_count - 1U), &clause) ||
            (clause.kind != R_HIR_FINALLY) || (clause.child_count != 1U) ||
            !r_consteval_statement(eval, r_consteval_child(context, &clause, 0U), depth + 1U)) {
            r_context_free(context, chain);
            return eval->failure == R_CONST_FAILURE_NONE
                       ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                       : false;
        }
        eval->flow = flow;
        eval->flow_loops = flow_loops;
        eval->returned = returned;
        if (throwing) {
            eval->failure = R_CONST_FAILURE_THROW;
            eval->thrown = thrown;
            eval->thrown_type = thrown_type;
            eval->span = span;
            r_context_free(context, eval->chain);
            eval->chain = chain;
            eval->chain_count = chain_count;
            return false;
        }
    }
    return completed;
}

static bool r_consteval_statement(RConstEval *eval, RHirNodeId node_id, uint32_t depth) {
    RFrontendContext *context = eval->context;
    RHirNode node;
    RConstCell value;
    RConstCell **slot;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, eval->span);
    }
    if (!r_consteval_step(eval, node.span)) {
        return false;
    }
    switch (node.kind) {
    case R_HIR_BLOCK:
        for (child_index = 0U; child_index < node.child_count; ++child_index) {
            if (!r_consteval_statement(
                    eval, r_consteval_child(context, &node, child_index), depth + 1U)) {
                return false;
            }
            if (eval->flow != R_CONST_FLOW_NORMAL) {
                return true;
            }
        }
        return true;
    case R_HIR_LOCAL:
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &value, depth + 1U)) {
            return false;
        }
        slot = r_consteval_slot(eval, node.symbol, node.span);
        if (slot == NULL) {
            return false;
        }
        if (*slot == NULL) {
            *slot = r_consteval_cells(eval, 1U, node.span);
            if (*slot == NULL) {
                return false;
            }
        }
        (*slot)->kind = R_CONST_UNSET;
        return r_consteval_store(eval, *slot, &value, node.span);
    case R_HIR_EXPRESSION_STATEMENT:
        return (node.child_count == 0U) ||
               r_consteval_expression(
                   eval, r_consteval_child(context, &node, 0U), &value, depth + 1U);
    case R_HIR_IF:
        if (!r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &value, depth + 1U)) {
            return false;
        }
        if (value.bits != 0U) {
            return r_consteval_statement(eval, r_consteval_child(context, &node, 1U), depth + 1U);
        }
        return (node.child_count < 3U) ||
               r_consteval_statement(eval, r_consteval_child(context, &node, 2U), depth + 1U);
    case R_HIR_WHILE:
    case R_HIR_FOR:
        return r_consteval_loop(eval, &node, depth);
    case R_HIR_BREAK:
        eval->flow =
            node.integer_value == UINT64_C(1) ? R_CONST_FLOW_BREAK_SWITCH : R_CONST_FLOW_BREAK;
        eval->flow_loops = node.loop_target > 1U ? node.loop_target - 1U : 0U;
        return true;
    case R_HIR_CONTINUE:
        eval->flow = R_CONST_FLOW_CONTINUE;
        eval->flow_loops = node.loop_target > 1U ? node.loop_target - 1U : 0U;
        return true;
    case R_HIR_RETURN:
        (void)memset(&value, 0, sizeof(value));
        if ((node.child_count != 0U) &&
            !r_consteval_expression(
                eval, r_consteval_child(context, &node, 0U), &value, depth + 1U)) {
            return false;
        }
        eval->returned = value;
        eval->flow = R_CONST_FLOW_RETURN;
        return true;
    case R_HIR_SWITCH:
        return r_consteval_switch(eval, &node, depth);
    case R_HIR_TRY:
        return r_consteval_try(eval, &node, depth);
    default:
        return r_consteval_expression(eval, node_id, &value, depth);
    }
}

static bool r_consteval_call(RConstEval *eval,
                             RSymbolId function_id,
                             RConstCell *arguments,
                             uint32_t argument_count,
                             RSourceSpan span,
                             RConstCell *result,
                             uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RSemanticSymbol *function = r_consteval_symbol(context, function_id);
    RHirNode function_node;
    RConstFlow saved_flow = eval->flow;
    uint32_t parameter_index;
    uint32_t active_index;

    if ((function != NULL) && (function->consteval_state == R_CONSTEVAL_EVALUABLE) &&
        r_consteval_string_recipe(context, function)) {
        return r_consteval_recipe_call(eval, function, arguments, argument_count, span, result);
    }
    if ((function == NULL) || (function->consteval_state != R_CONSTEVAL_EVALUABLE) ||
        !r_consteval_node_copy(context, function->hir_node, &function_node) ||
        (function_node.kind != R_HIR_FUNCTION) || (function_node.child_count == 0U) ||
        (function_node.child_count - 1U != argument_count) ||
        (depth > context->options.limits.max_nesting)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    for (active_index = 0U; active_index < eval->active_count; ++active_index) {
        if (eval->active[active_index] == function_id) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
    }
    if (eval->active_count == eval->active_capacity) {
        const uint32_t capacity = eval->active_capacity == 0U ? 8U : eval->active_capacity * 2U;
        RSymbolId *active = r_context_allocate(context, (size_t)capacity * sizeof(*active));
        if (active == NULL) {
            return r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
        }
        if (eval->active != NULL) {
            (void)memcpy(active, eval->active, (size_t)eval->active_count * sizeof(*active));
            r_context_free(context, eval->active);
        }
        eval->active = active;
        eval->active_capacity = capacity;
    }
    eval->active[eval->active_count++] = function_id;
    for (parameter_index = 0U; parameter_index < argument_count; ++parameter_index) {
        RHirNode parameter;
        RConstCell **slot;
        if (!r_consteval_node_copy(
                context, r_consteval_child(context, &function_node, parameter_index), &parameter) ||
            (parameter.kind != R_HIR_PARAMETER)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        slot = r_consteval_slot(eval, parameter.symbol, span);
        if (slot == NULL) {
            return false;
        }
        if (*slot == NULL) {
            *slot = r_consteval_cells(eval, 1U, span);
            if (*slot == NULL) {
                return false;
            }
        }
        (*slot)->kind = R_CONST_UNSET;
        if (!r_consteval_store(eval, *slot, &arguments[parameter_index], span)) {
            return false;
        }
    }
    eval->flow = R_CONST_FLOW_NORMAL;
    (void)memset(&eval->returned, 0, sizeof(eval->returned));
    if (!r_consteval_statement(
            eval, r_consteval_child(context, &function_node, argument_count), depth + 1U)) {
        return false;
    }
    function = r_consteval_symbol(context, function_id);
    if ((eval->flow != R_CONST_FLOW_RETURN) && (function != NULL) &&
        (r_semantic_value_kind(context, function->return_type) != R_SEMANTIC_TYPE_VOID)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    {
        const RConstCell returned = eval->returned;
        (void)memset(&eval->returned, 0, sizeof(eval->returned));
        *result = returned;
    }
    eval->flow = saved_flow;
    eval->active_count -= 1U;
    return true;
}

/* ---- values as checked HIR ---- */

static bool r_consteval_append(RFrontendContext *context,
                               RHirKind kind,
                               RSourceSpan span,
                               RTypeId type,
                               uint64_t value,
                               uint32_t member,
                               RHirNodeId *node_id) {
    RHirNode node;

    (void)memset(&node, 0, sizeof(node));
    node.kind = kind;
    node.span = span;
    node.type = type;
    node.integer_value = value;
    node.aggregate_member = member;
    return r_hir_append_node(context, node, node_id);
}

/* Scalar cells of a value; a larger value is not expanded into a function body. */
static uint64_t r_consteval_cell_count(const RConstCell *cell, uint64_t limit) {
    uint64_t total = 0U;
    uint64_t index;

    if ((cell->kind != R_CONST_AGGREGATE) && (cell->kind != R_CONST_VARIANT)) {
        return 1U;
    }
    for (index = 0U; (index < cell->count) && (total <= limit); ++index) {
        total += r_consteval_cell_count(&cell->items[index], limit);
    }
    return total;
}

static bool r_consteval_is_zero(const RConstCell *cell) {
    uint64_t index;

    if (cell->kind == R_CONST_SCALAR) {
        return cell->bits == 0U;
    }
    if (cell->kind == R_CONST_TEXT) {
        return cell->bits == 0U;
    }
    if (cell->kind != R_CONST_AGGREGATE) {
        return false;
    }
    for (index = 0U; index < cell->count; ++index) {
        if (!r_consteval_is_zero(&cell->items[index])) {
            return false;
        }
    }
    return true;
}

/* Rebuilds a value as literal, string, enumerator, array and aggregate initializer nodes; a value
 * outside those forms (an enumeration value that names no variant) is not representable. */
static bool r_consteval_to_hir(RFrontendContext *context,
                               const RConstCell *cell,
                               RTypeId type,
                               RSourceSpan span,
                               uint32_t depth,
                               RHirNodeId *node_id,
                               bool *representable) {
    const RTypeId value_type = r_semantic_value_type(context, type);
    const RSemanticType *description = r_semantic_type(context, value_type);
    RHirVector children = {0};
    uint64_t index;
    uint64_t emitted;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    if ((description == NULL) || (depth > r_frontend_tree_depth_limit(context))) {
        *representable = false;
        return true;
    }
    if (cell->kind == R_CONST_SCALAR) {
        const RSemanticAggregate *enumeration = r_consteval_fieldless_enum(context, value_type);
        if (enumeration != NULL) {
            uint32_t variant_index;
            for (variant_index = 0U; variant_index < enumeration->variant_count; ++variant_index) {
                const uint32_t global = enumeration->first_variant + variant_index;
                if (context->semantic_variants[global].value == cell->bits) {
                    return r_consteval_append(context,
                                              R_HIR_ENUM_CONSTANT,
                                              span,
                                              value_type,
                                              cell->bits,
                                              global + 1U,
                                              node_id);
                }
            }
            *representable = false;
            return true;
        }
        if ((description->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) &&
            (r_consteval_float_width(context, value_type) != 0U)) {
            /* L22.2: a C floating value is the R literal of its format viewed as the C type, as
               `1.5 as c_double` is written. */
            RTypeId format;
            RHirNodeId literal;
            if (!r_semantic_type_from_token(context,
                                            r_consteval_float_width(context, value_type) == 32U
                                                ? R_TOKEN_KW_F32
                                                : R_TOKEN_KW_F64,
                                            &format) ||
                !r_consteval_append(
                    context, R_HIR_LITERAL, span, format, cell->bits, 0U, &literal)) {
                return false;
            }
            success = r_consteval_append(context, R_HIR_CAST, span, value_type, 0U, 0U, node_id) &&
                      r_hir_vector_push(context, &children, literal) &&
                      r_hir_commit_children(context, *node_id, &children);
            if (success) {
                context->hir_nodes[(size_t)*node_id - 1U].operation = R_TOKEN_KW_AS;
            }
            r_context_free(context, children.items);
            return success;
        }
        return r_consteval_append(
            context, R_HIR_LITERAL, span, value_type, cell->bits, 0U, node_id);
    }
    if (cell->kind == R_CONST_TEXT) {
        RTypeId literal_type;
        RHirNodeId literal;
        uint32_t intern_id = (uint32_t)cell->bits;
        if ((intern_id == 0U) && !r_intern_bytes(context, (const uint8_t *)"", 0U, &intern_id)) {
            return false;
        }
        if (!r_semantic_intern_type(
                context, R_SEMANTIC_TYPE_CONSTEXPR_STR, R_TYPE_ID_INVALID, &literal_type) ||
            !r_consteval_append(context,
                                R_HIR_STRING_LITERAL,
                                span,
                                literal_type,
                                cell->count,
                                intern_id,
                                &literal)) {
            return false;
        }
        if (value_type == literal_type) {
            *node_id = literal;
            return true;
        }
        /* The implicit view of a literal as str (R-EXPR-0015). */
        success = r_consteval_append(context, R_HIR_CAST, span, value_type, 0U, 0U, node_id) &&
                  r_hir_vector_push(context, &children, literal) &&
                  r_hir_commit_children(context, *node_id, &children);
        r_context_free(context, children.items);
        return success;
    }
    if ((cell->kind == R_CONST_VARIANT) &&
        ((r_consteval_tagged_enum(context, value_type, 0U) != NULL) ||
         r_consteval_option_type(context, value_type, false, 0U))) {
        /* L22.1, L22.3: a variant of a tagged enumeration, an option or an error family. */
        const RSemanticVariant *variant =
            r_semantic_tagged_variant(context, value_type, cell->bits);
        const RTypeId payload_type =
            variant != NULL ? variant->payload_type
                            : (cell->bits == 1U ? description->base : R_TYPE_ID_INVALID);
        RHirNodeId payload;
        if ((cell->count > 1U) || ((cell->count == 1U) != (payload_type != R_TYPE_ID_INVALID))) {
            *representable = false;
            return true;
        }
        if (!r_consteval_append(
                context, R_HIR_VARIANT, span, value_type, cell->bits, 0U, node_id)) {
            return false;
        }
        if (cell->count == 0U) {
            return true;
        }
        if (!r_consteval_to_hir(
                context, cell->items, payload_type, span, depth + 1U, &payload, representable)) {
            return false;
        }
        if (!*representable) {
            return true;
        }
        success = r_hir_vector_push(context, &children, payload) &&
                  r_hir_commit_children(context, *node_id, &children);
        r_context_free(context, children.items);
        return success;
    }
    if ((cell->kind == R_CONST_CONTAINER) && context->consteval_freeze &&
        (r_consteval_owner(context, value_type) != R_CONSTEVAL_OWNER_NONE)) {
        /* L22.4: an owner frozen into the program image, as an array initializer of the owner
           type: the elements of an array or list, the bytes of a string as one literal, or the
           keys and values of a dictionary in insertion order. */
        const RConstevalOwner owner = r_consteval_owner(context, value_type);
        const uint64_t width = owner == R_CONSTEVAL_OWNER_DICT ? 2U : 1U;
        if ((owner == R_CONSTEVAL_OWNER_DICT) &&
            ((r_consteval_float_width(context, description->base) != 0U) ||
             (r_consteval_owner(context, description->base) != R_CONSTEVAL_OWNER_NONE))) {
            /* A frozen dictionary indexes its keys by the emitted key contract; floating and
               owned string keys are not frozen. */
            *representable = false;
            return true;
        }
        if (!r_consteval_append(
                context, R_HIR_ARRAY_INIT, span, value_type, cell->count, 0U, node_id)) {
            return false;
        }
        if (owner == R_CONSTEVAL_OWNER_STRING) {
            RTypeId literal_type;
            RHirNodeId literal;
            RConstCell text;
            RConstEval scratch;
            bool viewed;
            r_consteval_init(&scratch, context);
            viewed = r_consteval_string_view(&scratch, cell, value_type, span, &text);
            r_consteval_destroy(&scratch);
            if (!viewed ||
                !r_semantic_intern_type(
                    context, R_SEMANTIC_TYPE_CONSTEXPR_STR, R_TYPE_ID_INVALID, &literal_type) ||
                !r_consteval_append(context,
                                    R_HIR_STRING_LITERAL,
                                    span,
                                    literal_type,
                                    text.count,
                                    (uint32_t)text.bits,
                                    &literal) ||
                !r_hir_vector_push(context, &children, literal)) {
                goto cleanup;
            }
        } else {
            for (index = 0U; index < cell->count * width; ++index) {
                const RTypeId element_type =
                    owner == R_CONSTEVAL_OWNER_DICT
                        ? ((index % 2U) == 0U ? description->base : description->second)
                        : description->base;
                const RConstCell *element = owner == R_CONSTEVAL_OWNER_LIST
                                                ? cell->items[index].items
                                                : &cell->items[index];
                RHirNodeId value;
                if ((element == NULL) ||
                    !r_consteval_to_hir(
                        context, element, element_type, span, depth + 1U, &value, representable)) {
                    goto cleanup;
                }
                if (!*representable) {
                    success = true;
                    goto cleanup;
                }
                if (!r_hir_vector_push(context, &children, value)) {
                    goto cleanup;
                }
            }
        }
        success = r_hir_commit_children(context, *node_id, &children);
        goto cleanup;
    }
    if (cell->kind != R_CONST_AGGREGATE) {
        *representable = false;
        return true;
    }
    if (description->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        const RTypeId element_type = description->base;
        /* Trailing zero elements are left to the zero fill of the initializer. */
        emitted = cell->count;
        while ((emitted != 0U) && r_consteval_is_zero(&cell->items[emitted - 1U])) {
            emitted -= 1U;
        }
        if (!r_consteval_append(
                context, R_HIR_ARRAY_INIT, span, value_type, cell->count, 0U, node_id)) {
            return false;
        }
        for (index = 0U; index < emitted; ++index) {
            RHirNodeId element;
            if (!r_consteval_to_hir(context,
                                    &cell->items[index],
                                    element_type,
                                    span,
                                    depth + 1U,
                                    &element,
                                    representable)) {
                goto cleanup;
            }
            if (!*representable) {
                success = true;
                goto cleanup;
            }
            if (!r_hir_vector_push(context, &children, element)) {
                goto cleanup;
            }
        }
    } else {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, value_type);
        size_t first_field;
        if ((aggregate == NULL) || (aggregate->field_count != cell->count)) {
            *representable = false;
            return true;
        }
        first_field = aggregate->first_field;
        if (!r_consteval_append(context, R_HIR_AGGREGATE_INIT, span, value_type, 0U, 0U, node_id)) {
            return false;
        }
        for (index = 0U; index < cell->count; ++index) {
            const RTypeId field_type = context->semantic_fields[first_field + (size_t)index].type;
            RHirNodeId field;
            RHirNodeId value;
            RHirVector field_children = {0};
            if (!r_consteval_to_hir(context,
                                    &cell->items[index],
                                    field_type,
                                    span,
                                    depth + 1U,
                                    &value,
                                    representable)) {
                goto cleanup;
            }
            if (!*representable) {
                success = true;
                goto cleanup;
            }
            if (!r_consteval_append(context,
                                    R_HIR_FIELD_INIT,
                                    span,
                                    r_semantic_value_type(context, field_type),
                                    0U,
                                    (uint32_t)(first_field + (size_t)index) + 1U,
                                    &field) ||
                !r_hir_vector_push(context, &field_children, value) ||
                !r_hir_commit_children(context, field, &field_children)) {
                r_context_free(context, field_children.items);
                goto cleanup;
            }
            r_context_free(context, field_children.items);
            if (!r_hir_vector_push(context, &children, field)) {
                goto cleanup;
            }
        }
    }
    success = r_hir_commit_children(context, *node_id, &children);

cleanup:
    r_context_free(context, children.items);
    return success;
}

/* ---- diagnostics ---- */

static bool r_consteval_text_c(RGenericText *text, const char *value) {
    return r_generic_text(text, value, strlen(value));
}

/* `<arguments>` of a generic instance; a constant argument is spelled as in source (`4usize`). */
static bool
r_consteval_text_arguments(RGenericText *text, uint32_t schema, uint32_t first_argument) {
    RFrontendContext *context = text->context;
    const RGenericSchema *generic = &context->generic_schemas[(size_t)schema - 1U];
    uint32_t index;
    bool success = r_consteval_text_c(text, "<");

    for (index = 0U; success && (index < generic->parameter_count); ++index) {
        const RTypeId argument = context->generic_arguments[first_argument + index];
        const RSemanticType *type = r_semantic_type(context, argument);
        uint64_t constant = 0U;
        char digits[32];
        success = (index == 0U) || r_consteval_text_c(text, ", ");
        if (success && (type != NULL) && (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR) &&
            (type->base == 0U) && (type->second == 0U)) {
            const char *keyword;
            constant = 0U;
            success = r_generic_constant_spelling(type, &keyword, digits, sizeof(digits)) &&
                      r_consteval_text_c(text, digits);
            if (success && (type->flags != (uint32_t)R_TOKEN_KW_BOOL)) {
                success = r_consteval_text_c(text, keyword);
            }
        } else if (success && (type != NULL) && (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR) &&
                   r_generic_constant_evaluate(context, argument, &constant, 0U)) {
            (void)snprintf(digits, sizeof(digits), "%llu", (unsigned long long)constant);
            success = r_consteval_text_c(text, digits) && r_consteval_text_c(text, "usize");
        } else {
            success = success && r_generic_type_name(text, argument, 0U);
        }
    }
    return success && r_consteval_text_c(text, ">");
}

static bool r_consteval_text_name(RGenericText *text, RSymbolId symbol_id) {
    const RSemanticSymbol *symbol = r_consteval_symbol(text->context, symbol_id);
    const RSemanticSymbol *origin = NULL;
    const RInternEntry *name;

    if ((symbol == NULL) || (symbol->name_intern_id == 0U) ||
        ((size_t)symbol->name_intern_id > text->context->intern_count)) {
        return r_generic_text(text, "`?`", 3U);
    }
    if ((symbol->generic_origin != R_SYMBOL_ID_INVALID) && (symbol->generic_schema != 0U)) {
        origin = r_consteval_symbol(text->context, symbol->generic_origin);
    }
    if ((origin != NULL) && (origin->name_intern_id != 0U)) {
        /* An instance: the name of its definition and its closed arguments. */
        name = &text->context->intern_entries[(size_t)origin->name_intern_id - 1U];
        return r_generic_text(text, "`", 1U) && r_generic_text(text, name->bytes, name->length) &&
               r_consteval_text_arguments(
                   text, symbol->generic_schema, symbol->first_generic_argument) &&
               r_generic_text(text, "`", 1U);
    }
    name = &text->context->intern_entries[(size_t)symbol->name_intern_id - 1U];
    return r_generic_text(text, "`", 1U) && r_generic_text(text, name->bytes, name->length) &&
           r_generic_text(text, "`", 1U);
}

static bool
r_consteval_emit(RGenericText *text, const char *code, const char *rule, RSourceSpan span) {
    uint32_t message = 0U;

    if (!r_generic_text(text, "", 0U) ||
        !r_intern_bytes(text->context, text->bytes, text->length, &message)) {
        return false;
    }
    return r_semantic_add_diagnostic(
        text->context, code, rule, text->context->intern_entries[(size_t)message - 1U].bytes, span);
}

/* Why a function is a run-time function: the chain of calls down to its own first reason. */
static bool r_consteval_explain(RGenericText *text, RSymbolId function_id) {
    RSymbolId current = function_id;
    uint32_t count = 0U;
    const RSemanticSymbol *symbol;

    if (!r_consteval_text_name(text, current)) {
        return false;
    }
    for (;;) {
        symbol = r_consteval_symbol(text->context, current);
        if ((symbol == NULL) || (symbol->consteval_reason != R_CONSTEVAL_REASON_CALLEE) ||
            (symbol->consteval_callee == R_SYMBOL_ID_INVALID) ||
            (count >= text->context->options.limits.max_nesting)) {
            break;
        }
        current = symbol->consteval_callee;
        count += 1U;
        if (!r_consteval_text_c(text, " calls ") || !r_consteval_text_name(text, current)) {
            return false;
        }
    }
    return r_consteval_text_c(text, count == 0U ? " " : ", which ") &&
           r_consteval_text_c(text,
                              symbol == NULL ? r_consteval_reason_text(R_CONSTEVAL_REASON_NONE)
                                             : r_consteval_reason_text(
                                                   (RConstevalReason)symbol->consteval_reason));
}

static bool r_consteval_report_callee(RFrontendContext *context,
                                      RSymbolId function_id,
                                      RSourceSpan span,
                                      const RGenericText *instance) {
    RGenericText text = {context, NULL, 0U, 0U, true};
    bool success = r_consteval_text_c(&text, "call is not a constant expression: ") &&
                   r_consteval_explain(&text, function_id) &&
                   ((instance == NULL) ||
                    r_generic_text(&text, (const char *)instance->bytes, instance->length)) &&
                   r_consteval_emit(&text, "R-DIAG-CONST-002", "R-FUNC-0023", span);

    r_context_free(context, text.bytes);
    return success;
}

static bool r_consteval_text_span(RGenericText *text, RSourceSpan span) {
    const RSource *source = r_get_source_const(text->context, span.source);

    if ((source == NULL) || (span.end < span.start)) {
        return r_consteval_text_c(text, "?");
    }
    return r_generic_text(text, source->bytes + span.start, (size_t)(span.end - span.start));
}

/* L22.1: a thrown value in a diagnostic, spelled like an initializer: `name {.field = 2}`. */
static bool r_consteval_text_value(RGenericText *text, const RConstCell *cell, uint32_t depth) {
    RFrontendContext *context = text->context;
    const RTypeId type = r_semantic_value_type(context, cell->type);
    const RSemanticType *semantic = r_semantic_type(context, type);
    char digits[64];
    uint64_t index;

    if (depth > 4U) {
        return r_consteval_text_c(text, "...");
    }
    if (cell->kind == R_CONST_TEXT) {
        const RInternEntry *entry =
            ((cell->bits != 0U) && ((size_t)cell->bits <= context->intern_count))
                ? &context->intern_entries[(size_t)cell->bits - 1U]
                : NULL;
        const size_t length = entry == NULL ? 0U : (entry->length > 40U ? 40U : entry->length);
        return r_consteval_text_c(text, "\"") &&
               ((length == 0U) || r_generic_text(text, entry->bytes, length)) &&
               r_consteval_text_c(text, (entry != NULL) && (entry->length > 40U) ? "...\"" : "\"");
    }
    if ((cell->kind == R_CONST_AGGREGATE) && (semantic != NULL) &&
        (semantic->kind == R_SEMANTIC_TYPE_FIXED_ARRAY)) {
        if (!r_consteval_text_c(text, "{")) {
            return false;
        }
        for (index = 0U; index < cell->count; ++index) {
            if (index == 8U) {
                return r_consteval_text_c(text, ", ...}");
            }
            if (((index != 0U) && !r_consteval_text_c(text, ", ")) ||
                !r_consteval_text_value(text, &cell->items[index], depth + 1U)) {
                return false;
            }
        }
        return r_consteval_text_c(text, "}");
    }
    if (cell->kind == R_CONST_AGGREGATE) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, type);
        if (aggregate == NULL) {
            return r_consteval_text_c(text, "{...}");
        }
        if (!r_consteval_text_c(text, "{")) {
            return false;
        }
        for (index = 0U; (index < cell->count) && (index < aggregate->field_count); ++index) {
            const RSemanticField *field =
                &context->semantic_fields[(size_t)aggregate->first_field + (size_t)index];
            if (((index != 0U) && !r_consteval_text_c(text, ", ")) ||
                !r_consteval_text_c(text, ".") || !r_consteval_text_span(text, field->name_span) ||
                !r_consteval_text_c(text, " = ") ||
                !r_consteval_text_value(text, &cell->items[index], depth + 1U)) {
                return false;
            }
        }
        return r_consteval_text_c(text, "}");
    }
    if (cell->kind != R_CONST_SCALAR) {
        return r_consteval_text_c(text, "?");
    }
    if (r_semantic_value_kind(context, type) == R_SEMANTIC_TYPE_BOOL) {
        return r_consteval_text_c(text, cell->bits != 0U ? "true" : "false");
    }
    {
        const RSemanticAggregate *enumeration = r_consteval_fieldless_enum(context, type);
        const uint32_t float_width = r_consteval_float_width(context, type);
        bool is_signed = false;
        uint32_t width = 64U;

        if (enumeration != NULL) {
            uint32_t variant_index;
            for (variant_index = 0U; variant_index < enumeration->variant_count; ++variant_index) {
                const RSemanticVariant *variant =
                    &context->semantic_variants[(size_t)enumeration->first_variant + variant_index];
                if (variant->value == cell->bits) {
                    return r_consteval_text_span(text, variant->name_span);
                }
            }
        }
        if (float_width != 0U) {
            (void)snprintf(
                digits, sizeof(digits), "%.17g", r_consteval_float_value(float_width, cell->bits));
        } else if (r_semantic_value_kind(context, type) == R_SEMANTIC_TYPE_CHAR) {
            (void)snprintf(digits, sizeof(digits), "U+%04llX", (unsigned long long)cell->bits);
        } else if (r_semantic_integer_properties(
                       r_consteval_integer_kind(context, type), &is_signed, &width) &&
                   is_signed && (width != 0U) && (width <= 64U) &&
                   ((cell->bits & (UINT64_C(1) << (width - 1U))) != 0U)) {
            const uint64_t magnitude = (UINT64_C(0) - cell->bits) & r_constant_integer_mask(width);
            (void)snprintf(digits, sizeof(digits), "-%llu", (unsigned long long)magnitude);
        } else {
            (void)snprintf(digits, sizeof(digits), "%llu", (unsigned long long)cell->bits);
        }
        return r_consteval_text_c(text, digits);
    }
}

/* `name {.field = value}` of a thrown error, or `name::variant` of a fieldless one. */
static bool r_consteval_text_error(RGenericText *text, const RConstCell *cell, RTypeId type) {
    RFrontendContext *context = text->context;
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));

    if ((aggregate == NULL) || (aggregate->name_intern_id == 0U) ||
        ((size_t)aggregate->name_intern_id > context->intern_count)) {
        /* A standard error of an owner operation is named by its type. */
        return r_consteval_text_c(text, "`") &&
               r_generic_type_name(text, r_semantic_value_type(context, type), 0U) &&
               r_consteval_text_c(text, "`");
    }
    {
        const RInternEntry *name = &context->intern_entries[(size_t)aggregate->name_intern_id - 1U];
        if (!r_consteval_text_c(text, "`") || !r_generic_text(text, name->bytes, name->length)) {
            return false;
        }
    }
    if (cell->kind == R_CONST_SCALAR) {
        return r_consteval_text_c(text, "::") && r_consteval_text_value(text, cell, 0U) &&
               r_consteval_text_c(text, "`");
    }
    return r_consteval_text_c(text, "` ") && r_consteval_text_value(text, cell, 0U);
}

static bool r_consteval_report_failure(RFrontendContext *context,
                                       const RConstEval *eval,
                                       RSourceSpan span,
                                       bool required,
                                       const RGenericText *instance) {
    RGenericText text = {context, NULL, 0U, 0U, true};
    const char *code = "R-DIAG-CONST-003";
    const char *rule = "R-EXPR-0032";
    uint32_t index;
    bool success = false;

    if (eval->failure == R_CONST_FAILURE_RESOURCE) {
        context->resource_status = R_FRONTEND_OUT_OF_MEMORY;
        return false;
    }
    if (!required) {
        /* Outside a required constant, a call that panics or exceeds the budget has an
           observable effect or cost at run time; it stays a run-time call (R-EXPR-0032). */
        return true;
    }
    if (eval->failure == R_CONST_FAILURE_PANIC) {
        if (!r_consteval_text_c(&text, "translation-time evaluation panics (") ||
            !r_consteval_text_c(&text, eval->category == NULL ? "explicit" : eval->category)) {
            goto cleanup;
        }
        if ((eval->message.kind == R_CONST_TEXT) && (eval->message.bits != 0U) &&
            ((size_t)eval->message.bits <= context->intern_count)) {
            const RInternEntry *entry = &context->intern_entries[(size_t)eval->message.bits - 1U];
            const size_t length = entry->length > 160U ? 160U : entry->length;
            if (!r_consteval_text_c(&text, ": \"") ||
                !r_generic_text(&text, entry->bytes, length) ||
                !r_consteval_text_c(&text, entry->length > 160U ? "...\"" : "\"")) {
                goto cleanup;
            }
        }
        if (!r_consteval_text_c(&text, ")")) {
            goto cleanup;
        }
    } else if (eval->failure == R_CONST_FAILURE_THROW) {
        /* L22.1: an error that no try of the evaluated calls catches. */
        if (!r_consteval_text_c(&text, "translation-time evaluation throws ") ||
            !r_consteval_text_error(&text, &eval->thrown, eval->thrown_type)) {
            goto cleanup;
        }
    } else if (eval->failure == R_CONST_FAILURE_LIMIT) {
        code = "R-DIAG-LIMIT-001";
        rule = "R-LIMIT-0003";
        if (!r_consteval_text_c(&text,
                                "translation-time evaluation exceeds the limit of 4000000 steps "
                                "or 64 MiB of values")) {
            goto cleanup;
        }
    } else {
        code = "R-DIAG-CONST-002";
        rule = "R-EXPR-0022";
        if (!r_consteval_text_c(&text, "expression is not a translation-time constant")) {
            goto cleanup;
        }
    }
    for (index = eval->chain_count; index != 0U; --index) {
        if (!r_consteval_text_c(&text, index == eval->chain_count ? " in " : " <- ") ||
            !r_consteval_text_name(&text, eval->chain[index - 1U])) {
            goto cleanup;
        }
    }
    if ((instance != NULL) &&
        !r_generic_text(&text, (const char *)instance->bytes, instance->length)) {
        goto cleanup;
    }
    success = r_consteval_emit(&text, code, rule, span);

cleanup:
    r_context_free(context, text.bytes);
    return success;
}

/* ---- constant expressions with calls ---- */

static bool r_consteval_static_check(
    RFrontendContext *context, RHirNodeId node_id, bool generic, RHirNodeId scope, uint32_t depth);

/* The value scope declares the hidden local that holds the subject of a membership test. */
static bool
r_consteval_scope_declares(RFrontendContext *context, RHirNodeId scope, RSymbolId symbol_id) {
    RHirNode node;
    RHirNode local;
    uint32_t child_index;

    if ((scope == R_HIR_NODE_ID_INVALID) || !r_consteval_node_copy(context, scope, &node)) {
        return false;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (((uint64_t)child_index != node.integer_value) &&
            r_consteval_node_copy(
                context, r_consteval_child(context, &node, child_index), &local) &&
            (local.kind == R_HIR_LOCAL) && (local.symbol == symbol_id)) {
            return true;
        }
    }
    return false;
}

/* A place whose value is fixed before the program runs (R-EXPR-0022): a module constant, a
 * constant module object, or a const local initialized by a constant expression. */
static bool r_consteval_static_place(
    RFrontendContext *context, RHirNodeId node_id, bool generic, RHirNodeId scope, uint32_t depth) {
    RHirNode node;
    RSemanticSymbol *symbol;
    RHirNode local;
    bool known = false;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return false;
    }
    switch (node.kind) {
    case R_HIR_PLACE:
        symbol = r_consteval_symbol(context, node.symbol);
        if (symbol == NULL) {
            return false;
        }
        if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) {
            return r_consteval_resolve_module_constant(
                       context, node.symbol, node.span, false, &known) &&
                   known;
        }
        if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
            const RSemanticType *declared = r_semantic_type(context, symbol->type);
            if ((declared == NULL) || (declared->kind != R_SEMANTIC_TYPE_CONST) ||
                symbol->is_thread_local || symbol->is_import ||
                !r_consteval_value_type(context, symbol->type, 0U) ||
                !r_consteval_prepare_module_object(context, node.symbol)) {
                return false;
            }
            symbol = r_consteval_symbol(context, node.symbol);
            return (symbol != NULL) && !symbol->poisoned &&
                   (symbol->hir_node != R_HIR_NODE_ID_INVALID);
        }
        if ((symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) && symbol->is_hidden_local &&
            r_consteval_scope_declares(context, scope, node.symbol)) {
            /* Checked with the value scope that initializes it. */
            return true;
        }
        if ((symbol->kind != R_SEMANTIC_SYMBOL_LOCAL) || symbol->is_static ||
            symbol->is_thread_local || !r_semantic_type_is_const(context, symbol->type) ||
            !r_consteval_value_type(context, symbol->type, 0U) ||
            !r_consteval_node_copy(context, symbol->hir_node, &local) ||
            (local.kind != R_HIR_LOCAL) || (local.child_count != 1U)) {
            return false;
        }
        return r_consteval_static_check(context,
                                        r_consteval_child(context, &local, 0U),
                                        generic,
                                        R_HIR_NODE_ID_INVALID,
                                        depth + 1U);
    case R_HIR_FIELD_PLACE:
        return r_consteval_static_place(
            context, r_consteval_child(context, &node, 0U), generic, scope, depth + 1U);
    case R_HIR_INDEX_PLACE:
        return r_consteval_static_place(
                   context, r_consteval_child(context, &node, 0U), generic, scope, depth + 1U) &&
               r_consteval_static_check(
                   context, r_consteval_child(context, &node, 1U), generic, scope, depth + 1U);
    default:
        return false;
    }
}

/* A constant expression of R-EXPR-0022 extended by translation-time calls (R-EXPR-0032). With
 * `generic`, constant generic parameters and layouts of type parameters count as constants: the
 * condition of a static `@if` in a generic definition is decided per instantiation. */
static bool r_consteval_static_check(
    RFrontendContext *context, RHirNodeId node_id, bool generic, RHirNodeId scope, uint32_t depth) {
    RHirNode node;
    RSemanticSymbol *symbol;
    uint32_t child_index;
    uint64_t size;
    uint64_t alignment;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context)) ||
        ((node.effect_exit_count != 0U) && (node.kind != R_HIR_CALL))) {
        return false;
    }
    switch (node.kind) {
    case R_HIR_LITERAL:
        return (r_semantic_value_kind(context, node.type) == R_SEMANTIC_TYPE_BOOL) ||
               (r_semantic_value_kind(context, node.type) == R_SEMANTIC_TYPE_CHAR) ||
               (r_consteval_integer_kind(context, node.type) != R_SEMANTIC_TYPE_INVALID) ||
               (r_consteval_float_width(context, node.type) != 0U);
    case R_HIR_STRING_LITERAL:
        return true;
    case R_HIR_ENUM_CONSTANT:
        return r_consteval_fieldless_enum(context, node.type) != NULL;
    case R_HIR_TYPE_QUERY:
        return r_generic_layout(context, node.auxiliary_type, &size, &alignment, 0U) ||
               (generic && r_generic_type_is_dependent(context, node.auxiliary_type, 0U));
    case R_HIR_GENERIC_CONSTANT:
        return generic;
    case R_HIR_STANDARD_CALL:
        if (r_consteval_math_allowed(context, &node)) {
            /* L22.2: an exact std.math operation of constant operands. */
            break;
        }
        /* A reflection constant over a type parameter is answered per instantiation. */
        return generic && (node.child_count == 0U) &&
               r_reflection_is_operation(node.standard_operation) &&
               r_reflection_type_is_dependent(
                   context, r_semantic_value_type(context, node.auxiliary_type), 0U);
    case R_HIR_DEFAULT_VALUE:
        return r_consteval_value_type(context, node.type, 0U);
    case R_HIR_LENGTH:
        return node.integer_value != UINT64_MAX;
    case R_HIR_LOAD:
    case R_HIR_MOVE:
        return r_consteval_value_type(context, node.type, 0U) &&
               r_consteval_static_place(
                   context, r_consteval_child(context, &node, 0U), generic, scope, depth + 1U);
    case R_HIR_BORROW:
        return r_consteval_storage_type(context, node.type) &&
               r_consteval_static_place(
                   context, r_consteval_child(context, &node, 0U), generic, scope, depth + 1U);
    case R_HIR_SLICE:
        /* A shared borrow of a const array passed as a slice (R-EXPR-0032). */
        return (node.child_count == 1U) && (node.operation != R_TOKEN_KW_ARRAY) &&
               r_consteval_storage_type(context, node.type) &&
               r_consteval_static_place(
                   context, r_consteval_child(context, &node, 0U), generic, scope, depth + 1U);
    case R_HIR_VALUE_SCOPE:
        /* A membership test keeps its subject in a hidden local of the scope (R-EXPR-0029). */
        for (child_index = 0U; child_index < node.child_count; ++child_index) {
            const RHirNodeId child = r_consteval_child(context, &node, child_index);
            RHirNode local;
            if ((uint64_t)child_index == node.integer_value) {
                if (!r_consteval_static_check(context, child, generic, node_id, depth + 1U)) {
                    return false;
                }
                continue;
            }
            symbol = r_consteval_node_copy(context, child, &local)
                         ? r_consteval_symbol(context, local.symbol)
                         : NULL;
            if ((symbol == NULL) || (local.kind != R_HIR_LOCAL) || (local.child_count != 1U) ||
                !symbol->is_hidden_local ||
                !r_consteval_static_check(
                    context, r_consteval_child(context, &local, 0U), generic, scope, depth + 1U)) {
                return false;
            }
        }
        return true;
    case R_HIR_CAST: {
        RHirNode operand;
        if (!r_consteval_node_copy(context, r_consteval_child(context, &node, 0U), &operand) ||
            !r_consteval_cast_allowed(context, operand.type, node.type)) {
            return false;
        }
        break;
    }
    case R_HIR_UNARY:
    case R_HIR_BINARY:
    case R_HIR_CONDITIONAL:
    case R_HIR_ARRAY_INIT:
    case R_HIR_AGGREGATE_INIT:
    case R_HIR_FIELD_INIT:
    case R_HIR_VARIANT:
        if (!r_consteval_value_type(context, node.type, 0U)) {
            return false;
        }
        break;
    case R_HIR_CALL:
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol == NULL) || node.function_item_receiver ||
            !r_consteval_direct_call(symbol, &node)) {
            return false;
        }
        if (generic && r_consteval_open_call(context, &node)) {
            /* Each instantiation closes the callee and proves it evaluable (R-META-0003). */
            break;
        }
        if (!r_consteval_classify(context, node.symbol, 0U)) {
            return false;
        }
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol == NULL) || (symbol->consteval_state != R_CONSTEVAL_EVALUABLE)) {
            return false;
        }
        break;
    default:
        return false;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (!r_consteval_static_check(context,
                                      r_consteval_child(context, &node, child_index),
                                      generic,
                                      scope,
                                      depth + 1U)) {
            return false;
        }
    }
    return true;
}

static bool r_consteval_static(RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    return r_consteval_static_check(context, node_id, false, R_HIR_NODE_ID_INVALID, depth);
}

/* The first call inside a non-constant expression whose callee is not evaluable, if any. */
static RHirNodeId
r_consteval_blocking_call(RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    RHirNode node;
    RSemanticSymbol *symbol;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return R_HIR_NODE_ID_INVALID;
    }
    if (node.kind == R_HIR_CALL) {
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol != NULL) && (symbol->consteval_state == R_CONSTEVAL_RUNTIME)) {
            return node_id;
        }
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        const RHirNodeId found = r_consteval_blocking_call(
            context, r_consteval_child(context, &node, child_index), depth + 1U);
        if (found != R_HIR_NODE_ID_INVALID) {
            return found;
        }
    }
    return R_HIR_NODE_ID_INVALID;
}

static bool
r_consteval_report_not_constant(RFrontendContext *context, RHirNodeId node_id, RSourceSpan span) {
    const RHirNodeId blocking = r_consteval_blocking_call(context, node_id, 0U);
    RHirNode call;

    if ((blocking != R_HIR_NODE_ID_INVALID) && r_consteval_node_copy(context, blocking, &call)) {
        return r_consteval_report_callee(context, call.symbol, call.span, NULL);
    }
    return r_semantic_add_diagnostic(context,
                                     "R-DIAG-CONST-002",
                                     "R-EXPR-0022",
                                     "expression required to be constant is not constant",
                                     span);
}

/*
 * R-EXPR-0032: evaluates the constant expression `node_id` and, when `rewrite` is set, replaces
 * the node by the value in place. A required constant reports every reason why it has no value;
 * otherwise only a panic is diagnosed and an evaluation over the translation budget stays a
 * run-time evaluation. `value` receives scalar bits when requested.
 */
static bool r_consteval_evaluate_node(RFrontendContext *context,
                                      RHirNodeId node_id,
                                      bool required,
                                      bool rewrite,
                                      bool *folded,
                                      uint64_t *bits) {
    RConstEval eval;
    RConstCell value;
    RHirNode node;
    RHirNodeId replacement = R_HIR_NODE_ID_INVALID;
    bool representable = true;
    bool success = true;

    *folded = false;
    if (!r_consteval_node_copy(context, node_id, &node)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_consteval_static(context, node_id, 0U)) {
        if (context->resource_status != R_FRONTEND_OK) {
            return false;
        }
        return !required || r_consteval_report_not_constant(context, node_id, node.span);
    }
    r_consteval_init(&eval, context);
    if (!r_consteval_expression(&eval, node_id, &value, 0U)) {
        success = r_consteval_report_failure(context, &eval, node.span, required, NULL) &&
                  (context->resource_status == R_FRONTEND_OK);
        r_consteval_destroy(&eval);
        return success;
    }
    if (bits != NULL) {
        if (value.kind != R_CONST_SCALAR) {
            r_consteval_destroy(&eval);
            return !required || r_consteval_report_not_constant(context, node_id, node.span);
        }
        *bits = value.bits;
    }
    if (!required && eval.allocates) {
        r_consteval_destroy(&eval);
        return true;
    }
    if (!required && (r_consteval_cell_count(&value, R_CONSTEVAL_INLINE_CELLS) >
                      (uint64_t)R_CONSTEVAL_INLINE_CELLS)) {
        /* A large value stays a run-time call inside a function; a module constant holds it
           as one static initializer instead. */
        r_consteval_destroy(&eval);
        return true;
    }
    if (rewrite && (r_semantic_value_kind(context, node.type) != R_SEMANTIC_TYPE_VOID)) {
        if (!r_consteval_to_hir(
                context, &value, node.type, node.span, 0U, &replacement, &representable)) {
            r_consteval_destroy(&eval);
            return false;
        }
        if (representable && (replacement != R_HIR_NODE_ID_INVALID)) {
            RHirNode rebuilt = context->hir_nodes[(size_t)replacement - 1U];
            rebuilt.span = node.span;
            context->hir_nodes[(size_t)node_id - 1U] = rebuilt;
        }
    }
    *folded = representable;
    context->consteval_evaluations += 1U;
    r_consteval_destroy(&eval);
    return true;
}

/* Replaces every translation-time call of a subtree by its value, innermost first. */
static bool r_consteval_fold_tree(RFrontendContext *context,
                                  RHirNodeId node_id,
                                  bool required,
                                  uint32_t depth) {
    RHirNode node;
    uint32_t child_index;
    bool folded = false;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return true;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (!r_consteval_node_copy(context, node_id, &node) ||
            !r_consteval_fold_tree(
                context, r_consteval_child(context, &node, child_index), required, depth + 1U)) {
            return false;
        }
    }
    if (!r_consteval_node_copy(context, node_id, &node) || (node.kind != R_HIR_CALL)) {
        return true;
    }
    if (!r_consteval_classify(context, node.symbol, 0U)) {
        return false;
    }
    {
        const RSemanticSymbol *callee = r_consteval_symbol(context, node.symbol);
        if ((callee == NULL) || (callee->consteval_state != R_CONSTEVAL_EVALUABLE)) {
            return !required || (callee == NULL) ||
                   r_consteval_report_callee(context, node.symbol, node.span, NULL);
        }
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (!r_consteval_static(context, r_consteval_child(context, &node, child_index), 0U)) {
            return (context->resource_status == R_FRONTEND_OK) &&
                   (!required ||
                    r_consteval_report_not_constant(
                        context, r_consteval_child(context, &node, child_index), node.span));
        }
    }
    if (!required && (r_semantic_value_kind(context, node.type) == R_SEMANTIC_TYPE_VOID)) {
        /* A void call has no value to substitute; outside a required constant it runs. */
        return true;
    }
    return r_consteval_evaluate_node(context, node_id, required, true, &folded, NULL);
}

/* Required scalar constants: array bounds, generic arguments, labels and module constants. */
static bool r_consteval_required_scalar(RFrontendContext *context,
                                        RHirNodeId node_id,
                                        uint64_t *bits,
                                        bool *valid) {
    bool folded = false;

    *valid = false;
    if (!r_consteval_evaluate_node(context, node_id, true, false, &folded, bits)) {
        return false;
    }
    *valid = folded;
    return true;
}

/* ---- constant conditions of static `@if` (R-META-0002, R-META-0003) ---- */

/* The expression reads a local of a function body; hidden locals the expression declares for
 * its own operands do not count. */
static bool
r_consteval_reads_locals(const RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    RHirNode node;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return false;
    }
    if ((node.kind == R_HIR_PLACE) && (node.symbol != R_SYMBOL_ID_INVALID) &&
        ((size_t)node.symbol <= context->semantic_symbol_count) &&
        (context->semantic_symbols[(size_t)node.symbol - 1U].kind == R_SEMANTIC_SYMBOL_LOCAL) &&
        !context->semantic_symbols[(size_t)node.symbol - 1U].is_hidden_local) {
        return true;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (r_consteval_reads_locals(
                context, r_consteval_child(context, &node, child_index), depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* R-FUNC-0020: the locals a constant expression reads during translation are used, although
 * no executable read of them may remain. */
static void r_consteval_mark_reads(RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    RHirNode node;
    RSemanticSymbol *symbol;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return;
    }
    if (node.kind == R_HIR_PLACE) {
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol != NULL) && (symbol->kind == R_SEMANTIC_SYMBOL_LOCAL)) {
            symbol->translation_time_read = true;
        }
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        r_consteval_mark_reads(context, r_consteval_child(context, &node, child_index), depth + 1U);
    }
}

/* The condition names a constant generic parameter, the layout of a type parameter or a callee
 * that depends on the parameters, directly or through the initializer of a local. */
static bool r_consteval_dependent(RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    RHirNode node;
    RHirNode local;
    const RSemanticSymbol *symbol;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return false;
    }
    switch (node.kind) {
    case R_HIR_GENERIC_CONSTANT:
        return true;
    case R_HIR_CALL:
        if (r_consteval_open_call(context, &node)) {
            return true;
        }
        break;
    case R_HIR_TYPE_QUERY:
        return r_generic_type_is_dependent(context, node.auxiliary_type, 0U);
    case R_HIR_STANDARD_CALL:
        if (r_reflection_is_operation(node.standard_operation) &&
            r_reflection_type_is_dependent(
                context, r_semantic_value_type(context, node.auxiliary_type), 0U)) {
            return true;
        }
        break;
    case R_HIR_PLACE:
        symbol = r_consteval_symbol(context, node.symbol);
        return (symbol != NULL) && (symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) &&
               r_consteval_node_copy(context, symbol->hir_node, &local) &&
               (local.kind == R_HIR_LOCAL) && (local.child_count == 1U) &&
               r_consteval_dependent(context, r_consteval_child(context, &local, 0U), depth + 1U);
    default:
        break;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (r_consteval_dependent(
                context, r_consteval_child(context, &node, child_index), depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* Reports the innermost operand that keeps a dependent expression from being constant. */
static bool
r_consteval_report_operand(RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    RHirNode node;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (child_index = 0U;
         (depth < context->options.limits.max_nesting) && (child_index < node.child_count);
         ++child_index) {
        const RHirNodeId child = r_consteval_child(context, &node, child_index);
        if (!r_consteval_static_check(context, child, true, R_HIR_NODE_ID_INVALID, depth + 1U)) {
            return (context->resource_status == R_FRONTEND_OK) &&
                   r_consteval_report_operand(context, child, depth + 1U);
        }
    }
    return r_consteval_report_not_constant(context, node_id, node.span);
}

/* R-NAME-0008, R-META-0003: a static initializer in a generic definition that depends on its
 * parameters is a constant of each instantiation, folded when the instance is closed. Such an
 * initializer sets `dependent`; one that is not constant for the definition is reported. */
static bool
r_consteval_dependent_initializer(RFrontendContext *context, RHirNodeId node_id, bool *dependent) {
    *dependent = r_consteval_dependent(context, node_id, 0U);
    if (!*dependent ||
        r_consteval_static_check(context, node_id, true, R_HIR_NODE_ID_INVALID, 0U)) {
        return context->resource_status == R_FRONTEND_OK;
    }
    return (context->resource_status == R_FRONTEND_OK) &&
           r_consteval_report_operand(context, node_id, 0U);
}

/*
 * R-META-0002: the checked condition leaf of a static `@if` in a function body. A leaf over
 * constant generic parameters or layouts of type parameters stays `dependent` and is decided per
 * instantiation (R-META-0003); any other leaf is a required constant. `valid` is false after a
 * diagnostic.
 */
static bool r_consteval_condition(
    RFrontendContext *context, RHirNodeId node_id, bool *dependent, bool *valid, bool *value) {
    RHirNode node;
    uint64_t bits = 0U;

    *dependent = false;
    *valid = false;
    *value = false;
    if (!r_consteval_node_copy(context, node_id, &node)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (r_consteval_dependent(context, node_id, 0U)) {
        if (!r_consteval_static_check(context, node_id, true, R_HIR_NODE_ID_INVALID, 0U)) {
            return (context->resource_status == R_FRONTEND_OK) &&
                   r_consteval_report_not_constant(context, node_id, node.span);
        }
        *dependent = true;
        *valid = true;
        return true;
    }
    if (!r_consteval_required_scalar(context, node_id, &bits, valid)) {
        return false;
    }
    *value = bits != 0U;
    return true;
}

/* " for `name<arguments>`": the instantiation that decides a condition spelled once in its
 * generic definition. */
static bool r_consteval_instance_text(RFrontendContext *context,
                                      uint32_t schema,
                                      uint32_t arguments,
                                      RGenericText *text) {
    const RGenericSchema *generic;
    const RSemanticSymbol *function;
    uint32_t name_id;
    const RInternEntry *name;

    if (schema == 0U) {
        return true; /* a closed declaration names no instance */
    }
    generic = &context->generic_schemas[(size_t)schema - 1U];
    function = r_consteval_symbol(context, generic->function);
    name_id = (function == NULL) ? 0U : function->name_intern_id;

    if (!r_consteval_text_c(text, " for ")) {
        return false;
    }
    if ((name_id == 0U) && (generic->aggregate != 0U) &&
        ((size_t)generic->aggregate <= context->semantic_aggregate_count)) {
        name_id = context->semantic_aggregates[(size_t)generic->aggregate - 1U].name_intern_id;
    }
    if (name_id == 0U) {
        return r_consteval_text_c(text, "this instantiation");
    }
    name = &context->intern_entries[(size_t)name_id - 1U];
    return r_consteval_text_c(text, "`") && r_generic_text(text, name->bytes, name->length) &&
           r_consteval_text_arguments(text, schema, arguments) && r_consteval_text_c(text, "`");
}

/*
 * R-META-0003: closes every call of a dependent condition whose callee depends on the generic
 * definition, including calls in the initializers of the locals the condition reads, and proves
 * each closed callee evaluable; building its body may close further instances. `blocked`
 * receives the first such call whose closed callee is not evaluable, and `blocker` that callee.
 */
static bool r_consteval_close_calls(RFrontendContext *context,
                                    uint32_t schema,
                                    uint32_t arguments,
                                    RHirNodeId node_id,
                                    uint32_t depth,
                                    RConstevalCallees *callees,
                                    RHirNodeId *blocked,
                                    RSymbolId *blocker) {
    RHirNode node;
    RHirNode local;
    const RSemanticSymbol *symbol;
    RSymbolId closed = R_SYMBOL_ID_INVALID;
    RTypeId self_type = R_TYPE_ID_INVALID;
    uint32_t child_index;

    if ((*blocked != R_HIR_NODE_ID_INVALID) || !r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return true;
    }
    if (node.kind == R_HIR_PLACE) {
        symbol = r_consteval_symbol(context, node.symbol);
        if ((symbol == NULL) || (symbol->kind != R_SEMANTIC_SYMBOL_LOCAL) ||
            !r_consteval_node_copy(context, symbol->hir_node, &local) ||
            (local.kind != R_HIR_LOCAL) || (local.child_count != 1U)) {
            return true;
        }
        return r_consteval_close_calls(context,
                                       schema,
                                       arguments,
                                       r_consteval_child(context, &local, 0U),
                                       depth + 1U,
                                       callees,
                                       blocked,
                                       blocker);
    }
    if (r_consteval_open_call(context, &node) &&
        (r_consteval_closed_callee(callees, node.symbol) == node.symbol)) {
        if (!r_generic_close_callee(context, node.symbol, schema, arguments, &closed, &self_type) ||
            !r_consteval_classify(context, closed, 0U)) {
            if (context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            *blocked = node_id;
            return true;
        }
        symbol = r_consteval_symbol(context, closed);
        if ((symbol == NULL) || (symbol->consteval_state != R_CONSTEVAL_EVALUABLE)) {
            *blocked = node_id;
            *blocker = closed;
            return true;
        }
        if (!r_grow_array(context,
                          (void **)&callees->pairs,
                          &callees->capacity,
                          sizeof(*callees->pairs),
                          callees->count + 2U)) {
            return false;
        }
        callees->pairs[callees->count++] = node.symbol;
        callees->pairs[callees->count++] = closed;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (!r_consteval_close_calls(context,
                                     schema,
                                     arguments,
                                     r_consteval_child(context, &node, child_index),
                                     depth + 1U,
                                     callees,
                                     blocked,
                                     blocker)) {
            return false;
        }
    }
    return true;
}

/*
 * R-META-0003, R-TYPE-0047: evaluates the checked HIR `node_id` of a generic definition for the
 * closed arguments of one instance. Callees over the definition's parameters are closed first; a
 * failure is reported naming the instance and leaves `valid` false.
 */
static bool r_consteval_instance_value(RFrontendContext *context,
                                       RHirNodeId node_id,
                                       uint32_t schema,
                                       uint32_t arguments,
                                       RSourceSpan span,
                                       uint64_t *bits,
                                       bool *valid) {
    RConstevalCallees callees = {NULL, 0U, 0U};
    RGenericText instance = {context, NULL, 0U, 0U, true};
    RConstEval eval;
    RConstCell value;
    RHirNodeId blocked = R_HIR_NODE_ID_INVALID;
    RSymbolId blocker = R_SYMBOL_ID_INVALID;
    RHirNode call;
    size_t diagnostics;
    bool success = true;

    *bits = 0U;
    *valid = false;
    /* Callee bodies are built before the evaluation starts. */
    if (!r_consteval_close_calls(
            context, schema, arguments, node_id, 0U, &callees, &blocked, &blocker)) {
        r_context_free(context, callees.pairs);
        return false;
    }
    r_consteval_init(&eval, context);
    eval.schema = schema;
    eval.arguments = arguments;
    eval.callees = callees;
    diagnostics = context->diagnostic_count;
    if ((blocked == R_HIR_NODE_ID_INVALID) && r_consteval_expression(&eval, node_id, &value, 0U) &&
        (value.kind == R_CONST_SCALAR)) {
        *bits = value.bits;
        *valid = true;
        context->consteval_evaluations += 1U;
    } else if ((blocked == R_HIR_NODE_ID_INVALID) && (context->diagnostic_count != diagnostics)) {
        /* A nested translation-time value has already reported why there is no value. */
    } else {
        success = r_consteval_instance_text(context, schema, arguments, &instance);
        if ((blocker != R_SYMBOL_ID_INVALID) && r_consteval_node_copy(context, blocked, &call)) {
            success = success && r_consteval_report_callee(context, blocker, call.span, &instance);
        } else {
            if (eval.failure == R_CONST_FAILURE_NONE) {
                eval.failure = R_CONST_FAILURE_UNSUPPORTED;
            }
            success = success && r_consteval_report_failure(context, &eval, span, true, &instance);
        }
        r_context_free(context, instance.bytes);
    }
    r_consteval_destroy(&eval);
    return success;
}

/* L25.2 (R-STMT-0017): the capacity of a task group spelled over the constant generic parameters
 * of its function, for the closed arguments of one instance. A capacity outside 1..65536 is
 * reported naming the instance unless `report` is false; `capacity` is then 1, so the instance
 * still closes. */
static bool r_consteval_group_capacity(RFrontendContext *context,
                                       RHirNodeId node_id,
                                       uint32_t schema,
                                       uint32_t arguments,
                                       RSourceSpan span,
                                       bool report,
                                       uint64_t *capacity) {
    RGenericText text = {context, NULL, 0U, 0U, true};
    uint64_t bits = 0U;
    bool valid = false;
    bool success;

    *capacity = 1U;
    if (!r_consteval_instance_value(context, node_id, schema, arguments, span, &bits, &valid)) {
        return false;
    }
    if (!valid) {
        return true;
    }
    if ((bits != 0U) && (bits <= 65536U)) {
        *capacity = bits;
        return true;
    }
    if (!report) {
        return true;
    }
    success = r_consteval_text_c(&text, "task group capacity shall be a constant in 1..65536") &&
              r_consteval_instance_text(context, schema, arguments, &text) &&
              r_consteval_emit(&text, "R-DIAG-ASYNC-001", "R-STMT-0017", span);
    r_context_free(context, text.bytes);
    return success;
}

/* R-META-0003: decides a dependent condition for the closed arguments of one instantiation. A
 * condition that has no value is reported once; the instantiation then takes `@else`. */
static bool r_consteval_static_choice(RFrontendContext *context,
                                      uint32_t predicate_id,
                                      uint32_t schema,
                                      uint32_t arguments,
                                      int *truth) {
    const RStaticPredicate predicate = context->static_predicates[(size_t)predicate_id - 1U];
    RStaticChoice choice;
    uint64_t bits = 0U;
    bool valid = false;
    size_t index;

    *truth = -1;
    for (index = 0U; index < context->static_choice_count; ++index) {
        const RStaticChoice *known = &context->static_choices[index];
        if ((known->predicate == predicate_id) && (known->schema == schema) &&
            (known->arguments == arguments)) {
            *truth = known->truth;
            return true;
        }
    }
    if (!r_consteval_instance_value(
            context, predicate.condition, schema, arguments, predicate.span, &bits, &valid) ||
        !r_grow_array(context,
                      (void **)&context->static_choices,
                      &context->static_choice_capacity,
                      sizeof(*context->static_choices),
                      context->static_choice_count + 1U)) {
        return false;
    }
    choice.predicate = predicate_id;
    choice.schema = schema;
    choice.arguments = arguments;
    choice.truth = (valid && (bits != 0U)) ? 1 : 0;
    context->static_choices[context->static_choice_count++] = choice;
    *truth = choice.truth;
    return true;
}

/* ---- module-scope translation-time values ---- */

/* A value keyed by its expression and, for a computed bound, by its instance's arguments. */
static RConstevalValue *r_consteval_injected_instance(RFrontendContext *context,
                                                      RSourceId source,
                                                      RAstNodeId node,
                                                      uint32_t instance) {
    size_t index;

    for (index = 0U; index < context->consteval_value_count; ++index) {
        RConstevalValue *value = &context->consteval_values[index];
        if ((value->source == source) && (value->node == node) && (value->instance == instance)) {
            return value;
        }
    }
    return NULL;
}

static RConstevalValue *
r_consteval_injected(RFrontendContext *context, RSourceId source, RAstNodeId node) {
    return r_consteval_injected_instance(context, source, node, 0U);
}

/* The diagnostic a discovery pass recorded for a module-scope value it could not compute. */
static bool r_consteval_report_recorded(RFrontendContext *context,
                                        const RConstevalValue *value,
                                        RSourceSpan span) {
    if ((value->code == 0U) || (value->rule == 0U) || (value->message == 0U) ||
        ((size_t)value->code > context->intern_count) ||
        ((size_t)value->rule > context->intern_count) ||
        ((size_t)value->message > context->intern_count)) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-CONST-002",
                                         "R-EXPR-0032",
                                         "module-scope constant has no translation-time value",
                                         span);
    }
    return r_semantic_add_diagnostic(context,
                                     context->intern_entries[(size_t)value->code - 1U].bytes,
                                     context->intern_entries[(size_t)value->rule - 1U].bytes,
                                     context->intern_entries[(size_t)value->message - 1U].bytes,
                                     value->span.end >= value->span.start &&
                                             value->span.source != R_SOURCE_ID_INVALID
                                         ? value->span
                                         : span);
}

static bool r_consteval_report_injected(RFrontendContext *context,
                                        const RConstevalValue *value,
                                        RSourceSpan span) {
    if ((value->code != 0U) && ((size_t)value->code <= context->intern_count)) {
        const char *code = context->intern_entries[(size_t)value->code - 1U].bytes;
        /* An ordinary source error behind the value is reported by this analysis itself. */
        if ((strcmp(code, "R-DIAG-CONST-002") != 0) && (strcmp(code, "R-DIAG-CONST-003") != 0) &&
            (strcmp(code, "R-DIAG-LIMIT-001") != 0)) {
            return true;
        }
    }
    return r_consteval_report_recorded(context, value, span);
}

static bool r_consteval_prepare_module_object(RFrontendContext *context, RSymbolId symbol_id) {
    RSemanticSymbol *symbol = r_consteval_symbol(context, symbol_id);
    bool success;

    if ((symbol == NULL) || (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT) || symbol->poisoned ||
        symbol->is_import || (symbol->hir_node != R_HIR_NODE_ID_INVALID) ||
        !context->consteval_ready) {
        return true;
    }
    if (symbol->consteval_resolving) {
        symbol->poisoned = true;
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-CONST-002",
                                         "R-EXPR-0022",
                                         "cyclic reference between translation-time constants",
                                         symbol->name_span);
    }
    symbol->consteval_resolving = true;
    context->consteval_builds += 1U;
    success = r_hir_build_module_object_initializer(context, symbol_id);
    symbol = r_consteval_symbol(context, symbol_id);
    if (symbol != NULL) {
        symbol->consteval_resolving = false;
    }
    return success;
}

/*
 * R-EXPR-0032: a module constant whose initializer is not an operator expression over literals is
 * evaluated on first use. A discovery pass supplies the value needed by declarations that are
 * checked before function bodies exist; afterwards the initializer is lowered and evaluated here.
 */
static bool r_consteval_resolve_module_constant(RFrontendContext *context,
                                                RSymbolId symbol_id,
                                                RSourceSpan use_span,
                                                bool report,
                                                bool *known) {
    RSemanticSymbol *symbol = r_consteval_symbol(context, symbol_id);
    RConstevalValue *injected;
    RAstRef declaration;
    RAstRef initializer;
    RBodyContext body;
    RExpressionResult value;
    RTypeId expected;
    size_t first_diagnostic;
    uint64_t bits = 0U;
    bool valid = false;
    bool success = true;

    *known = false;
    if ((symbol == NULL) || (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_CONSTANT)) {
        return true;
    }
    if (!symbol->consteval_pending) {
        *known = !symbol->poisoned;
        return true;
    }
    injected = r_consteval_injected(context, symbol->module_source, symbol->declaration_ast);
    if ((injected != NULL) && injected->resolved) {
        symbol->integer_value = injected->bits;
        symbol->consteval_pending = false;
        context->consteval_evaluations += 1U;
        *known = true;
        return true;
    }
    if (!context->consteval_ready) {
        if (report && !context->consteval_discovery) {
            symbol->poisoned = true;
            symbol->consteval_pending = false;
            symbol->consteval_state = (uint8_t)R_CONSTEVAL_RUNTIME;
            return injected != NULL
                       ? r_consteval_report_injected(context, injected, use_span)
                       : r_semantic_add_diagnostic(
                             context,
                             "R-DIAG-CONST-002",
                             "R-EXPR-0032",
                             "module constant has no translation-time value at this point",
                             use_span);
        }
        return true;
    }
    if (symbol->consteval_resolving) {
        symbol->poisoned = true;
        symbol->consteval_pending = false;
        symbol->consteval_state = (uint8_t)R_CONSTEVAL_RUNTIME;
        return !report ||
               r_semantic_add_diagnostic(context,
                                         "R-DIAG-CONST-002",
                                         "R-EXPR-0022",
                                         "cyclic reference between translation-time constants",
                                         symbol->name_span);
    }
    symbol->consteval_resolving = true;
    first_diagnostic = context->diagnostic_count;
    declaration.source = symbol->module_source;
    declaration.node = symbol->declaration_ast;
    expected = r_semantic_value_type(context, symbol->type);
    if (!r_ast_find_direct_child(context, declaration, R_SYNTAX_INITIALIZER, &initializer)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    (void)memset(&body, 0, sizeof(body));
    body.frontend = context;
    body.source = declaration.source;
    body.translation_effects = true;
    context->consteval_builds += 1U;
    if (!r_body_lower_value(&body, initializer, expected, &value)) {
        r_context_free(context, body.active_symbols.items);
        return false;
    }
    if (value.valid && !body.poisoned && (value.type != expected)) {
        success = r_body_diagnostic(&body,
                                    "R-DIAG-TYPE-001",
                                    "R-INIT-0002",
                                    "module constant initializer type does not match its type",
                                    r_body_hir_node(&body, value.node)->span);
        value.valid = false;
    }
    if (success && value.valid && !body.poisoned) {
        success = r_consteval_required_scalar(context, value.node, &bits, &valid);
    }
    r_context_free(context, body.active_symbols.items);
    symbol = r_consteval_symbol(context, symbol_id);
    if (symbol == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    symbol->consteval_resolving = false;
    symbol->consteval_pending = false;
    if (valid) {
        symbol->integer_value = bits;
        symbol->hir_node = value.node;
        context->consteval_evaluations += 1U;
        *known = true;
    } else {
        /* A failed translation-time constant: later uses cascade instead of reporting again. */
        const RSourceSpan name_span = symbol->name_span;
        symbol->poisoned = true;
        symbol->consteval_state = (uint8_t)R_CONSTEVAL_RUNTIME;
        if (success && context->consteval_discovery) {
            success = r_consteval_record_failure(context,
                                                 declaration.source,
                                                 declaration.node,
                                                 0U,
                                                 first_diagnostic,
                                                 name_span,
                                                 NULL);
        }
    }
    return success;
}

/*
 * A static, thread-local or module initializer shall be a constant expression (R-NAME-0006,
 * R-NAME-0008). Its translation-time calls are replaced by their values; when the lowering keeps
 * hidden temporaries for evaluation order, the whole initializer is then evaluated once.
 */
static bool r_consteval_fold_initializer(RFrontendContext *context, RHirNodeId initializer) {
    const size_t diagnostic_count = context->diagnostic_count;
    RConstEval eval;
    RConstCell value;
    RHirNode node;
    RHirNodeId replacement = R_HIR_NODE_ID_INVALID;
    bool representable = true;
    bool success = true;

    if (!context->consteval_ready || !r_consteval_fold_tree(context, initializer, true, 0U)) {
        return !context->consteval_ready;
    }
    if ((context->diagnostic_count != diagnostic_count) ||
        r_generic_constant_initializer(context, initializer, 0U) ||
        !r_consteval_node_copy(context, initializer, &node) ||
        !r_consteval_value_type(context, node.type, 0U)) {
        return true;
    }
    r_consteval_init(&eval, context);
    if (!r_consteval_expression(&eval, initializer, &value, 0U)) {
        success =
            (((eval.failure != R_CONST_FAILURE_PANIC) && (eval.failure != R_CONST_FAILURE_THROW)) ||
             r_consteval_report_failure(context, &eval, node.span, true, NULL)) &&
            (context->resource_status == R_FRONTEND_OK);
        r_consteval_destroy(&eval);
        return success;
    }
    if (!r_consteval_to_hir(
            context, &value, node.type, node.span, 0U, &replacement, &representable)) {
        r_consteval_destroy(&eval);
        return false;
    }
    if (representable && (replacement != R_HIR_NODE_ID_INVALID)) {
        RHirNode rebuilt = context->hir_nodes[(size_t)replacement - 1U];
        rebuilt.span = node.span;
        context->hir_nodes[(size_t)initializer - 1U] = rebuilt;
        context->consteval_evaluations += 1U;
    }
    r_consteval_destroy(&eval);
    return true;
}

/* R-EXPR-0032: after checking, every call with constant arguments to an evaluable function is
 * replaced by its value. */
static bool r_consteval_fold_program(RFrontendContext *context) {
    size_t symbol_index;

    if (r_semantic_has_errors(context)) {
        return true;
    }
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];
        RHirNode function_node;
        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || symbol->is_synthetic ||
            (symbol->hir_node == R_HIR_NODE_ID_INVALID) || symbol->poisoned ||
            r_generic_function_is_open(context, symbol) ||
            !r_consteval_node_copy(context, symbol->hir_node, &function_node) ||
            (function_node.kind != R_HIR_FUNCTION) || (function_node.child_count == 0U)) {
            continue;
        }
        /* Every function gets its evaluability: the interface exports it (R-MOD-0006). */
        if (!r_consteval_classify(context, (RSymbolId)(symbol_index + 1U), 0U) ||
            !r_consteval_fold_tree(
                context,
                r_consteval_child(context, &function_node, function_node.child_count - 1U),
                false,
                0U)) {
            return false;
        }
        if (context->resource_status != R_FRONTEND_OK) {
            return false;
        }
    }
    return true;
}

static bool
r_consteval_contains_call(const RFrontendContext *context, RHirNodeId node_id, uint32_t depth) {
    RHirNode node;
    uint32_t child_index;

    if (!r_consteval_node_copy(context, node_id, &node) ||
        (depth > r_frontend_tree_depth_limit(context))) {
        return false;
    }
    if (node.kind == R_HIR_CALL) {
        return true;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        if (r_consteval_contains_call(
                context, r_consteval_child(context, &node, child_index), depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* A required scalar constant written with calls (R-EXPR-0032). Without a call the expression is
 * left to the caller's own constant rules and diagnostics (`handled` stays false). */
static bool r_consteval_required_call_scalar(
    RFrontendContext *context, RHirNodeId node_id, uint64_t *bits, bool *valid, bool *handled) {
    *valid = false;
    *handled = context->consteval_ready && r_consteval_contains_call(context, node_id, 0U);
    return !*handled || r_consteval_required_scalar(context, node_id, bits, valid);
}

/* ---- discovery of module-scope values (R-EXPR-0032) ---- */

/* R-LIMIT-0003: the documented number of discovery passes (section 25.1). */
enum {
    R_CONSTEVAL_DISCOVERY_ROUNDS = 32
};

static bool r_consteval_record(RFrontendContext *context, const RConstevalValue *value) {
    RConstevalValue *existing =
        r_consteval_injected_instance(context, value->source, value->node, value->instance);

    if (existing != NULL) {
        *existing = *value;
        return true;
    }
    if (!r_grow_array(context,
                      (void **)&context->consteval_values,
                      &context->consteval_value_capacity,
                      sizeof(*context->consteval_values),
                      context->consteval_value_count + 1U)) {
        return false;
    }
    context->consteval_values[context->consteval_value_count] = *value;
    context->consteval_value_count += 1U;
    return true;
}

static bool r_consteval_intern_text(RFrontendContext *context, const char *text, uint32_t *id) {
    return r_intern_bytes(context, (const uint8_t *)text, strlen(text), id);
}

/* A discovery pass remembers why a module-scope value has none: the first diagnostic reported
 * since `first_diagnostic`, or a general reason. */
static bool r_consteval_record_failure(RFrontendContext *context,
                                       RSourceId source,
                                       RAstNodeId node,
                                       uint32_t instance,
                                       size_t first_diagnostic,
                                       RSourceSpan span,
                                       const char *reason) {
    RConstevalValue value;
    const RConstevalValue *existing =
        r_consteval_injected_instance(context, source, node, instance);

    if ((existing != NULL) && existing->resolved) {
        return true;
    }
    (void)memset(&value, 0, sizeof(value));
    value.source = source;
    value.node = node;
    value.instance = instance;
    value.span = span;
    if (existing != NULL) {
        value.request = existing->request;
        value.expected = existing->expected;
        value.condition = existing->condition;
        value.deferred = existing->deferred;
    }
    if ((reason == NULL) && (first_diagnostic < context->diagnostic_count)) {
        const RDiagnostic *diagnostic = &context->diagnostics[first_diagnostic];
        value.span = diagnostic->primary_span;
        if (!r_consteval_intern_text(context, diagnostic->code, &value.code) ||
            !r_consteval_intern_text(context, diagnostic->rule_id, &value.rule) ||
            !r_consteval_intern_text(context, diagnostic->message, &value.message)) {
            return false;
        }
    } else if (!r_consteval_intern_text(context, "R-DIAG-CONST-002", &value.code) ||
               !r_consteval_intern_text(context, "R-EXPR-0032", &value.rule) ||
               !r_consteval_intern_text(
                   context,
                   reason != NULL ? reason : "module-scope constant has no translation-time value",
                   &value.message)) {
        return false;
    }
    return r_consteval_record(context, &value);
}

static bool r_consteval_ast_has_call(const RFrontendContext *context, RAstRef ref, uint32_t depth) {
    RAstNodeView view;
    uint32_t index;

    if ((depth > r_frontend_tree_depth_limit(context)) || !r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    if ((view.kind == R_SYNTAX_CALL_SUFFIX) || (view.kind == R_SYNTAX_STANDARD_TYPE_CALL)) {
        return true;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) &&
            r_consteval_ast_has_call(context, child, depth + 1U)) {
            return true;
        }
    }
    return false;
}

/*
 * R-EXPR-0032: a module-scope constant expression with a call (an array bound, a constant generic
 * argument or an enumerator value in a declaration) is checked before function bodies exist, so
 * its value comes from a discovery pass. `handled` stays false when the expression has no call or
 * bodies are already available; the caller then evaluates it itself.
 */
/* R-TYPE-0050: whether `ref` names an associated constant of a closed owner, such as
 * `Point::SIZE`; `T::SIZE` over a generic parameter stays a formula. */
static bool
r_consteval_ast_associated(const RFrontendContext *context, RAstRef ref, uint32_t depth) {
    RAstNodeView view;
    uint32_t index;

    if ((depth > r_frontend_tree_depth_limit(context)) || !r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    if (view.kind == R_SYNTAX_QUALIFIED_NAME) {
        RAstTokenView first = {0};
        RAstTokenView last = {0};
        RAstRef applied;
        RTypeId parameter;
        size_t constant;
        bool named = false;
        for (index = 0U; index < view.child_count; ++index) {
            RAstRef child;
            RAstTokenView token;
            if (r_ast_ref_child(context, ref, index, &child) &&
                r_ast_ref_token(context, child, &token) &&
                ((token.kind == R_TOKEN_IDENTIFIER) || (token.kind == R_TOKEN_KW_SELF))) {
                if (first.kind == R_TOKEN_INVALID) {
                    first = token;
                }
                last = token;
            }
        }
        for (constant = 0U; constant < context->semantic_associated_constant_count; ++constant) {
            named = named || (context->semantic_associated_constants[constant].name_intern_id ==
                              last.intern_id);
        }
        if (!named || (last.kind != R_TOKEN_IDENTIFIER)) {
            return false;
        }
        if (r_ast_find_direct_child(context, ref, R_SYNTAX_TYPE, &applied)) {
            return r_generic_schema_at(context, view.span) == 0U;
        }
        return (first.kind == R_TOKEN_IDENTIFIER) && (first.span.start != last.span.start) &&
               !r_generic_resolve_parameter(context, &first, &parameter);
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) &&
            r_consteval_ast_associated(context, child, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static bool r_consteval_module_scope_value(RFrontendContext *context,
                                           RAstRef expression,
                                           RTypeId expected,
                                           RSourceSpan span,
                                           uint64_t *bits,
                                           bool *valid,
                                           bool *handled) {
    RConstevalValue *injected;
    RConstevalValue request;

    *valid = false;
    *handled = false;
    /* A call needs function bodies; an associated constant needs the implementations. */
    if (context->consteval_ready ||
        !(r_consteval_ast_has_call(context, expression, 0U) ||
          (!context->impls_ready && r_consteval_ast_associated(context, expression, 0U)))) {
        return true;
    }
    *handled = true;
    injected = r_consteval_injected(context, expression.source, expression.node);
    if ((injected != NULL) && injected->resolved) {
        *bits = injected->bits;
        *valid = true;
        context->consteval_evaluations += 1U;
        return true;
    }
    if (context->consteval_discovery) {
        if (injected != NULL) {
            return true;
        }
        (void)memset(&request, 0, sizeof(request));
        request.source = expression.source;
        request.node = expression.node;
        request.span = span;
        request.request = true;
        request.expected = expected;
        return r_consteval_record(context, &request);
    }
    return injected != NULL
               ? r_consteval_report_injected(context, injected, span)
               : r_semantic_add_diagnostic(
                     context,
                     "R-DIAG-CONST-002",
                     "R-EXPR-0032",
                     "module-scope translation-time value is not available at this point",
                     span);
}

/*
 * R-META-0002: a module `@if` condition selects declarations before any declaration is collected,
 * so the discovery pass computes it and the analysis that follows reads the value. `truth` stays
 * -1 while a discovery pass has not decided the condition; `reported` follows a diagnostic.
 */
static bool r_consteval_module_condition(
    RFrontendContext *context, RAstRef expression, RSourceSpan span, int *truth, bool *reported) {
    RConstevalValue *injected = r_consteval_injected(context, expression.source, expression.node);
    RConstevalValue request;

    *truth = -1;
    *reported = false;
    if ((injected != NULL) && injected->resolved) {
        *truth = injected->bits != 0U ? 1 : 0;
        context->consteval_evaluations += 1U;
        return true;
    }
    if (context->consteval_discovery) {
        if (injected != NULL) {
            return true;
        }
        (void)memset(&request, 0, sizeof(request));
        request.source = expression.source;
        request.node = expression.node;
        request.span = span;
        request.request = true;
        request.condition = true;
        return r_consteval_record(context, &request);
    }
    *reported = true;
    /* Only the discovery pass checks the condition, so any diagnostic it recorded is reported. */
    return injected != NULL
               ? r_consteval_report_recorded(context, injected, span)
               : r_semantic_add_diagnostic(context,
                                           "R-DIAG-CONST-002",
                                           "R-META-0002",
                                           "module @if condition has no translation-time value",
                                           span);
}

/* ---- computed bounds of generic declarations (R-TYPE-0047) ---- */

static bool
r_consteval_computed_entry(const RFrontendContext *context, uint32_t key, size_t *index) {
    for (*index = 0U; *index < context->computed_bound_count; ++*index) {
        if (context->computed_bounds[*index].key == key) {
            return true;
        }
    }
    return false;
}

/* The identity of a computed bound: its module and its tokens, with each parameter of generic
 * declaration `schema` spelled by position. An expression that reads locals of a function body
 * is also identified by its position, since its names are not module-wide. */
static bool r_consteval_computed_key(RFrontendContext *context,
                                     RAstRef expression,
                                     uint32_t schema,
                                     bool located,
                                     RTypeId type,
                                     uint32_t *key) {
    const RSource *source = r_get_source_const(context, expression.source);
    RGenericSchema generic;
    RGenericText text = {context, NULL, 0U, 0U, false};
    RAstNodeView view;
    char spelling[32];
    size_t index;
    uint32_t parameter;
    bool success;

    if ((source == NULL) || !r_ast_ref_view(context, expression, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    /* A formula outside any generic declaration has no parameters (R-TYPE-0050). */
    (void)memset(&generic, 0, sizeof(generic));
    if (schema != 0U) {
        generic = context->generic_schemas[(size_t)schema - 1U];
    }
    success = r_generic_text(&text,
                             source->module_name == NULL ? "" : source->module_name,
                             source->module_name == NULL ? 0U : strlen(source->module_name)) &&
              r_generic_text(&text, ":", 1U) && r_generic_type_name(&text, type, 0U) &&
              r_generic_text(&text, ":", 1U);
    if (success && located) {
        (void)snprintf(spelling, sizeof(spelling), "@%lu:", (unsigned long)view.span.start);
        success = r_consteval_text_c(&text, spelling);
    }
    for (index = 0U; success && (index < source->token_count); ++index) {
        const RToken *token = &source->tokens[index];
        if ((token->span.start < view.span.start) || (token->span.end > view.span.end) ||
            r_token_is_trivia(token->kind)) {
            continue;
        }
        parameter = generic.parameter_count;
        if (token->kind == R_TOKEN_IDENTIFIER) {
            for (parameter = 0U; parameter < generic.parameter_count; ++parameter) {
                if (context->generic_parameters[generic.first_parameter + parameter]
                        .name_intern_id == token->intern_id) {
                    break;
                }
            }
        }
        success = r_generic_text(&text, " ", 1U);
        if (success && (parameter < generic.parameter_count)) {
            (void)snprintf(spelling, sizeof(spelling), "$%lu", (unsigned long)parameter);
            success = r_consteval_text_c(&text, spelling);
        } else if (success) {
            success = r_generic_text(
                &text, source->bytes + token->span.start, token->span.end - token->span.start);
        }
    }
    success = success && r_intern_bytes(context, text.bytes, text.length, key);
    r_context_free(context, text.bytes);
    return success;
}

/*
 * The dependent formula of a bound or constant argument that calls a function over the parameters
 * of generic declaration `schema`: the expression and the declaration's own arguments, which
 * substitution closes. `hir` is its checked HIR inside a function body; at module scope it is
 * lowered once function signatures exist (r_consteval_computed_hir).
 */
static bool r_consteval_computed_bound(RFrontendContext *context,
                                       RAstRef expression,
                                       uint32_t schema,
                                       RHirNodeId hir,
                                       bool located,
                                       RTypeId type,
                                       RTypeId *result) {
    const uint32_t first_parameter =
        schema == 0U ? 0U : context->generic_schemas[(size_t)schema - 1U].first_parameter;
    uint32_t parameter =
        schema == 0U ? 0U : context->generic_schemas[(size_t)schema - 1U].parameter_count;
    RTypeId chain = R_TYPE_ID_INVALID;
    uint32_t key = 0U;
    size_t index;

    if (!r_consteval_computed_key(context, expression, schema, located, type, &key)) {
        return false;
    }
    /* An equal formula already registered keeps the declaration it is evaluated in. */
    if (!r_consteval_computed_entry(context, key, &index)) {
        RComputedBound bound;
        if (!r_grow_array(context,
                          (void **)&context->computed_bounds,
                          &context->computed_bound_capacity,
                          sizeof(*context->computed_bounds),
                          context->computed_bound_count + 1U)) {
            return false;
        }
        (void)memset(&bound, 0, sizeof(bound));
        bound.key = key;
        bound.source = expression.source;
        bound.node = expression.node;
        bound.schema = schema;
        bound.type = type;
        bound.hir = hir;
        context->computed_bounds[context->computed_bound_count++] = bound;
    }
    for (; parameter != 0U; --parameter) {
        if (!r_semantic_intern_derived_type(
                context,
                R_SEMANTIC_TYPE_CONSTANT_EXPR,
                context->generic_parameters[first_parameter + parameter - 1U].type,
                chain,
                0U,
                (uint32_t)R_TOKEN_COMMA,
                &chain)) {
            return false;
        }
    }
    return r_semantic_intern_derived_type(context,
                                          R_SEMANTIC_TYPE_CONSTANT_EXPR,
                                          chain,
                                          R_TYPE_ID_INVALID,
                                          key,
                                          (uint32_t)R_TOKEN_LPAREN,
                                          result);
}

/* The value type of the computed bound `expression`. */
static RTypeId r_consteval_computed_type(const RFrontendContext *context, RTypeId expression) {
    const RSemanticType *type = r_semantic_type(context, expression);
    size_t index;

    return ((type != NULL) && r_consteval_computed_entry(context, (uint32_t)type->length, &index))
               ? context->computed_bounds[index].type
               : R_TYPE_ID_INVALID;
}

/* The checked HIR of a computed bound. A module-scope bound is lowered in the scope of its
 * declaration once, and checked like a dependent `@if` condition (R-META-0003). */
static bool r_consteval_computed_hir(RFrontendContext *context, size_t index, RHirNodeId *hir) {
    const RComputedBound bound = context->computed_bounds[index];
    RBodyContext body;
    RExpressionResult result;
    RAstRef expression;
    RAstNodeView view;
    size_t diagnostics;
    bool lowered;

    *hir = bound.hir;
    expression.source = bound.source;
    expression.node = bound.node;
    /* Before function signatures exist only a formula without a call can be lowered. */
    if (bound.failed || (bound.hir != R_HIR_NODE_ID_INVALID) ||
        (!context->consteval_ready && r_consteval_ast_has_call(context, expression, 0U))) {
        return true;
    }
    if (!r_ast_ref_view(context, expression, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (bound.lowering) {
        context->computed_bounds[index].failed = true;
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-CONST-002",
                                         "R-EXPR-0032",
                                         "translation-time value depends on itself",
                                         view.span);
    }
    context->computed_bounds[index].lowering = true;
    (void)memset(&body, 0, sizeof(body));
    body.frontend = context;
    body.source = bound.source;
    body.checking_usize_constant = true;
    body.translation_effects = true;
    diagnostics = context->diagnostic_count;
    lowered = r_body_lower_value(&body, expression, bound.type, &result);
    r_context_free(context, body.active_symbols.items);
    context->computed_bounds[index].lowering = false;
    if (!lowered) {
        return false;
    }
    if (!result.valid || body.poisoned || (result.type != bound.type)) {
        context->computed_bounds[index].failed = true;
        /* A constant formula has exactly its declared type (R-TYPE-0047, R-TYPE-0050). */
        return !result.valid || body.poisoned || (context->diagnostic_count != diagnostics) ||
               r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-INIT-0002",
                                         "translation-time value type does not match its declared "
                                         "type",
                                         view.span);
    }
    if (!r_consteval_static_check(context, result.node, true, R_HIR_NODE_ID_INVALID, 0U)) {
        context->computed_bounds[index].failed = true;
        return (context->resource_status == R_FRONTEND_OK) &&
               r_consteval_report_not_constant(context, result.node, view.span);
    }
    context->computed_bounds[index].hir = result.node;
    *hir = result.node;
    /* The formula's HIR is kept: an enclosing constant lowering must not roll it back. */
    context->consteval_builds += 1U;
    return true;
}

/* R-TYPE-0047: every computed bound at module scope is checked in its declaration, also when no
 * instance closes it. */
static bool r_consteval_check_computed_bounds(RFrontendContext *context) {
    size_t index;
    RHirNodeId hir;

    for (index = 0U; index < context->computed_bound_count; ++index) {
        if (!r_consteval_computed_hir(context, index, &hir)) {
            return false;
        }
    }
    return true;
}

/* A computed bound closed for canonically spelled arguments: the key discovery passes share. */
static bool r_consteval_instance_key(RFrontendContext *context,
                                     uint32_t bound,
                                     const RTypeVector *arguments,
                                     uint32_t *key) {
    const RInternEntry *formula = &context->intern_entries[(size_t)bound - 1U];
    RGenericText text = {context, NULL, 0U, 0U, false};
    size_t index;
    bool success =
        r_generic_text(&text, formula->bytes, formula->length) && r_generic_text(&text, "#", 1U);

    for (index = 0U; success && (index < arguments->count); ++index) {
        success = ((index == 0U) || r_generic_text(&text, ",", 1U)) &&
                  r_generic_type_name(&text, arguments->items[index], 0U);
    }
    success = success && r_intern_bytes(context, text.bytes, text.length, key);
    r_context_free(context, text.bytes);
    return success;
}

/*
 * R-TYPE-0047: the value of a closed computed bound. Once bodies exist it is evaluated for the
 * instance its arguments close; before that a discovery pass computes it, keyed by expression and
 * arguments. `valid` stays false after a diagnostic and while a discovery pass waits for it.
 */
static bool r_consteval_computed_value(RFrontendContext *context,
                                       RTypeId expression,
                                       uint64_t *value,
                                       bool *valid) {
    const RSemanticType *type = r_semantic_type(context, expression);
    RTypeVector arguments = {NULL, 0U, 0U};
    RComputedValue computed;
    RConstevalValue *injected;
    RConstevalValue request;
    RComputedBound bound;
    RAstNodeView view;
    RAstRef reference;
    RTypeId link;
    RHirNodeId hir = R_HIR_NODE_ID_INVALID;
    size_t entry;
    size_t slot;
    uint32_t first_argument = 0U;
    uint32_t instance = 0U;
    bool success = false;

    *value = 0U;
    *valid = false;
    for (slot = 0U; slot < context->computed_value_count; ++slot) {
        computed = context->computed_values[slot];
        if (computed.expression != expression) {
            continue;
        }
        if (computed.pending) {
            if (!r_consteval_computed_entry(context, (uint32_t)type->length, &entry)) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            reference.source = context->computed_bounds[entry].source;
            reference.node = context->computed_bounds[entry].node;
            return r_ast_ref_view(context, reference, &view) &&
                   r_semantic_add_diagnostic(context,
                                             "R-DIAG-CONST-002",
                                             "R-EXPR-0032",
                                             "translation-time value depends on itself",
                                             view.span);
        }
        *value = computed.value;
        *valid = computed.valid;
        return true;
    }
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR) ||
        (type->flags != (uint32_t)R_TOKEN_LPAREN) ||
        !r_consteval_computed_entry(context, (uint32_t)type->length, &entry)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    bound = context->computed_bounds[entry];
    reference.source = bound.source;
    reference.node = bound.node;
    if (!r_ast_ref_view(context, reference, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (link = type->base; link != R_TYPE_ID_INVALID;) {
        const RSemanticType *part = r_semantic_type(context, link);
        if ((part == NULL) || (part->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR) ||
            (part->flags != (uint32_t)R_TOKEN_COMMA)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        if (!r_type_vector_push(context, &arguments, part->base)) {
            goto cleanup;
        }
        link = part->second;
    }
    computed.expression = expression;
    computed.value = 0U;
    computed.valid = false;
    computed.pending = false;
    if (!context->consteval_ready && r_consteval_ast_has_call(context, reference, 0U)) {
        /* Function bodies do not exist yet: a discovery pass has computed the value. */
        if (!r_consteval_instance_key(context, bound.key, &arguments, &instance)) {
            goto cleanup;
        }
        injected = r_consteval_injected_instance(context, 0U, 0U, instance);
        if ((injected != NULL) && injected->resolved) {
            computed.value = injected->bits;
            computed.valid = true;
            context->consteval_evaluations += 1U;
        } else if (context->consteval_discovery) {
            /* The instance waits for this pass to evaluate the bound once bodies exist. */
            context->computed_pending += 1U;
            if (injected == NULL) {
                (void)memset(&request, 0, sizeof(request));
                request.instance = instance;
                request.span = view.span;
                request.request = true;
                request.deferred = expression;
                success = r_consteval_record(context, &request);
            } else {
                success = true;
            }
            goto cleanup;
        } else if (!((injected != NULL)
                         ? r_consteval_report_injected(context, injected, view.span)
                         : r_semantic_add_diagnostic(
                               context,
                               "R-DIAG-CONST-002",
                               "R-TYPE-0047",
                               "computed bound has no translation-time value at this point",
                               view.span))) {
            goto cleanup;
        }
    } else {
        computed.pending = true;
        if (!r_grow_array(context,
                          (void **)&context->computed_values,
                          &context->computed_value_capacity,
                          sizeof(*context->computed_values),
                          context->computed_value_count + 1U)) {
            goto cleanup;
        }
        slot = context->computed_value_count++;
        context->computed_values[slot] = computed;
        if (!r_consteval_computed_hir(context, entry, &hir) ||
            ((hir != R_HIR_NODE_ID_INVALID) &&
             (!r_generic_commit_arguments(context, &arguments, &first_argument) ||
              !r_consteval_instance_value(context,
                                          hir,
                                          bound.schema,
                                          first_argument,
                                          view.span,
                                          &computed.value,
                                          &computed.valid)))) {
            goto cleanup;
        }
        computed.pending = false;
        context->computed_values[slot] = computed;
        *value = computed.value;
        *valid = computed.valid;
        success = true;
        goto cleanup;
    }
    if (!r_grow_array(context,
                      (void **)&context->computed_values,
                      &context->computed_value_capacity,
                      sizeof(*context->computed_values),
                      context->computed_value_count + 1U)) {
        goto cleanup;
    }
    context->computed_values[context->computed_value_count++] = computed;
    *value = computed.value;
    *valid = computed.valid;
    success = true;

cleanup:
    r_context_free(context, arguments.items);
    return success;
}

/* ---- associated constants (R-TYPE-0050) ---- */

/*
 * The value of associated constant `index` of applied trait `trait` for closed type `type`: the
 * implementation's formula, or the trait's default for this implementation, evaluated as a
 * computed formula of its declaration for the implementation's arguments.
 */
static bool r_consteval_constant_value(
    RFrontendContext *context, RTypeId type, uint32_t trait, uint32_t index, RTypeId *literal) {
    const RSemanticTrait record = context->semantic_traits[trait - 1U];
    const RSemanticAssociatedConstant constant =
        context->semantic_associated_constants[record.first_constant + index];
    RTypeVector arguments = {NULL, 0U, 0U};
    RAstRef formula;
    RTypeId expression = R_TYPE_ID_INVALID;
    uint32_t impl_id = 0U;
    uint32_t schema = 0U;
    uint32_t first_argument = 0U;
    bool success = false;

    *literal = R_TYPE_ID_INVALID;
    if (!r_semantic_constant_impl(context, type, trait, &impl_id)) {
        return false;
    }
    if (impl_id == 0U) {
        return true; /* the constraint that named the constant was diagnosed */
    }
    formula = context->semantic_constant_bindings
                  [context->semantic_impls[impl_id - 1U].first_constant_binding + index];
    if (formula.node != R_AST_NODE_ID_INVALID) {
        const RSemanticImpl impl = context->semantic_impls[impl_id - 1U];
        schema = impl.generic_schema;
        if (schema != 0U) {
            /* A generic implementation is applied through the closed target's arguments. */
            const RSemanticAggregate *applied = r_semantic_aggregate_for_type(context, type);
            if ((applied == NULL) || (applied->generic_origin == 0U)) {
                return true;
            }
            first_argument = applied->first_generic_argument;
        }
    } else {
        const uint32_t origin = record.generic_origin != 0U ? record.generic_origin : trait;
        formula = constant.initializer;
        schema = context->semantic_traits[origin - 1U].generic_schema;
        if (!r_semantic_trait_arguments(context, trait, type, impl_id, &arguments) ||
            !r_generic_commit_arguments(context, &arguments, &first_argument)) {
            goto cleanup;
        }
    }
    if (!r_consteval_computed_bound(
            context, formula, schema, R_HIR_NODE_ID_INVALID, true, constant.type, &expression)) {
        goto cleanup;
    }
    if (schema != 0U) {
        success = r_generic_substitute_type(context, expression, schema, first_argument, literal);
    } else {
        *literal = expression;
        success = r_generic_canonical_constant(context, literal, constant.name_span);
    }
    if (!success) {
        *literal = R_TYPE_ID_INVALID;
        success = context->resource_status == R_FRONTEND_OK;
    }

cleanup:
    r_context_free(context, arguments.items);
    return success;
}

/* R-TYPE-0050: the value of a closed `P::NAME`. Before implementations are collected a discovery
 * pass has computed it, keyed by its spelling. `valid` stays false after a diagnostic. */
static bool r_consteval_projection_value(RFrontendContext *context,
                                         RTypeId expression,
                                         uint64_t *value,
                                         bool *valid) {
    const RSemanticType *type = r_semantic_type(context, expression);
    RTypeVector chain = {NULL, 0U, 0U};
    RGenericText text = {context, NULL, 0U, 0U, false};
    RConstevalValue *injected;
    RConstevalValue request;
    RTypeId link;
    RTypeId literal = R_TYPE_ID_INVALID;
    uint32_t trait;
    uint32_t index;
    uint32_t key = 0U;
    bool success = false;

    *value = 0U;
    *valid = false;
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR) ||
        (type->flags != (uint32_t)R_TOKEN_DOT)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    trait = (uint32_t)(type->length >> 32U);
    index = (uint32_t)(type->length & UINT32_MAX);
    for (link = type->base; link != R_TYPE_ID_INVALID;) {
        const RSemanticType *part = r_semantic_type(context, link);
        if ((part == NULL) || !r_type_vector_push(context, &chain, part->base)) {
            goto cleanup;
        }
        link = part->second;
    }
    if (chain.count == 0U) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        goto cleanup;
    }
    if (!context->impls_ready) {
        /* Implementations are collected after the types that may close this constant. */
        if (!r_generic_type_name(&text, expression, 0U) ||
            !r_intern_bytes(context, text.bytes, text.length, &key)) {
            goto cleanup;
        }
        injected = r_consteval_injected_instance(context, 0U, 0U, key);
        if ((injected != NULL) && injected->resolved) {
            *value = injected->bits;
            *valid = true;
            context->consteval_evaluations += 1U;
            success = true;
        } else if (context->consteval_discovery) {
            context->computed_pending += 1U;
            if (injected == NULL) {
                (void)memset(&request, 0, sizeof(request));
                request.instance = key;
                request.span = context
                                   ->semantic_associated_constants
                                       [context->semantic_traits[trait - 1U].first_constant + index]
                                   .name_span;
                request.request = true;
                request.deferred = expression;
                success = r_consteval_record(context, &request);
            } else {
                success = true;
            }
        } else {
            success = r_semantic_add_diagnostic(
                context,
                "R-DIAG-CONST-002",
                "R-TYPE-0050",
                "associated constant has no translation-time value at this point",
                context
                    ->semantic_associated_constants
                        [context->semantic_traits[trait - 1U].first_constant + index]
                    .name_span);
        }
        goto cleanup;
    }
    if (chain.count > 1U) {
        /* A generic trait is applied again to its closed arguments. */
        RTypeVector trait_arguments = {chain.items + 1U, chain.count - 1U, chain.count - 1U};
        if (!r_semantic_apply_trait(context,
                                    trait,
                                    &trait_arguments,
                                    context->semantic_traits[trait - 1U].span,
                                    &trait)) {
            goto cleanup;
        }
    }
    if (!r_consteval_constant_value(context, chain.items[0], trait, index, &literal)) {
        goto cleanup;
    }
    if (literal != R_TYPE_ID_INVALID) {
        *value = r_semantic_type(context, literal)->length;
        *valid = true;
    }
    success = true;

cleanup:
    r_context_free(context, chain.items);
    r_context_free(context, text.bytes);
    return success;
}

/* R-TYPE-0050, R-TYPE-0047: the value of any closed deferred constant formula. */
static bool r_consteval_deferred_value(RFrontendContext *context,
                                       RTypeId expression,
                                       uint64_t *value,
                                       bool *valid) {
    const RSemanticType *type = r_semantic_type(context, expression);
    return (type != NULL && type->flags == (uint32_t)R_TOKEN_DOT)
               ? r_consteval_projection_value(context, expression, value, valid)
               : r_consteval_computed_value(context, expression, value, valid);
}

/*
 * R-TYPE-0050: `P::NAME`, `Self::NAME` or `Type::NAME` as a value. A constant over a type that is
 * not closed becomes a generic constant of its projection; a closed one is its value. `handled`
 * stays false when no trait of the owner declares the name, unless `report_missing` asks for a
 * diagnostic over an owner that has no enumerators.
 */
static bool r_body_associated_constant(RBodyContext *body,
                                       RAstRef path,
                                       bool report_missing,
                                       RExpressionResult *result,
                                       bool *handled) {
    RFrontendContext *context = body->frontend;
    RAstTokenView identifiers[8];
    RAstNodeView view;
    RAstRef applied;
    RTypeId owner = R_TYPE_ID_INVALID;
    RTypeId constant_type;
    RTypeId projection;
    RTypeId literal = R_TYPE_ID_INVALID;
    uint32_t count = 0U;
    uint32_t child_index;
    uint32_t trait;
    uint32_t index;
    bool ambiguous;
    bool has_applied;

    *handled = false;
    if (!r_ast_ref_view(context, path, &view)) {
        return true;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;
        if (r_ast_ref_child(context, path, child_index, &child) &&
            r_ast_ref_token(context, child, &token) &&
            ((token.kind == R_TOKEN_IDENTIFIER) ||
             ((token.kind == R_TOKEN_KW_SELF) && (count == 0U))) &&
            (count < (uint32_t)(sizeof(identifiers) / sizeof(identifiers[0])))) {
            identifiers[count++] = token;
        }
    }
    has_applied = r_ast_find_direct_child(context, path, R_SYNTAX_TYPE, &applied);
    if ((count == 0U) || (!has_applied && (count < 2U))) {
        return true;
    }
    if (has_applied) {
        if (count != 1U || !r_semantic_type_from_ast(context, applied, &owner)) {
            return context->resource_status == R_FRONTEND_OK;
        }
    } else if ((count == 2U) && (identifiers[0].kind == R_TOKEN_KW_SELF)) {
        if (!r_semantic_self_type_at(context, identifiers[0].span, &owner)) {
            return true;
        }
    } else if (!((count == 2U) && r_generic_resolve_parameter(context, &identifiers[0], &owner))) {
        const RSemanticAggregate *match = NULL;
        size_t aggregate_index;
        for (aggregate_index = 0U; aggregate_index < context->semantic_aggregate_count;
             ++aggregate_index) {
            const RSemanticAggregate *aggregate = &context->semantic_aggregates[aggregate_index];
            const RSource *declaration = r_get_source_const(context, aggregate->module_source);
            bool visible = false;
            size_t import_index;
            if (aggregate->generic_schema != 0U || aggregate->generic_origin != 0U ||
                declaration == NULL || declaration->module_name == NULL ||
                aggregate->name_intern_id != identifiers[count - 2U].intern_id) {
                continue;
            }
            if (count == 2U) {
                visible = aggregate->module_source == body->source;
                for (import_index = 0U; !visible && import_index < context->import_count;
                     ++import_index) {
                    const RImportEntry *import = &context->imports[import_index];
                    visible = (import->source == body->source) && import->imports_name &&
                              (import->name != NULL) &&
                              r_semantic_token_text_equal_owned(
                                  context, &identifiers[count - 2U], import->name) &&
                              (strcmp(import->module_name, declaration->module_name) == 0);
                }
            } else if (r_body_qualified_module_matches(
                           body, path, count - 2U, declaration->module_name)) {
                visible = aggregate->module_source == body->source;
                for (import_index = 0U; !visible && import_index < context->import_count;
                     ++import_index) {
                    const RImportEntry *import = &context->imports[import_index];
                    visible = (import->source == body->source) && !import->imports_name &&
                              (strcmp(import->module_name, declaration->module_name) == 0);
                }
            }
            if (visible) {
                match = aggregate;
            }
        }
        if (match == NULL) {
            return true;
        }
        owner = match->type;
    }
    if (!r_semantic_find_constant(
            context, owner, identifiers[count - 1U].intern_id, &trait, &index, &ambiguous)) {
        return false;
    }
    if (ambiguous) {
        *handled = true;
        return r_body_diagnostic(body,
                                 "R-DIAG-NAME-001",
                                 "R-TYPE-0050",
                                 "more than one trait of this type declares the associated "
                                 "constant",
                                 view.span);
    }
    if (trait == 0U) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, owner);
        if (!report_missing ||
            ((aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM))) {
            return true;
        }
        *handled = true;
        r_body_expression_invalid(result);
        return r_body_diagnostic(body,
                                 "R-DIAG-NAME-001",
                                 "R-TYPE-0050",
                                 "no trait of this type declares the associated constant",
                                 view.span);
    }
    *handled = true;
    constant_type =
        context
            ->semantic_associated_constants[context->semantic_traits[trait - 1U].first_constant +
                                            index]
            .type;
    r_body_expression_invalid(result);
    if (r_generic_type_is_dependent(context, owner, 0U)) {
        if (!r_semantic_constant_projection(context, owner, trait, index, &projection) ||
            !r_body_append_node(body,
                                R_HIR_GENERIC_CONSTANT,
                                view.span,
                                constant_type,
                                0U,
                                R_TOKEN_INVALID,
                                0U,
                                false,
                                NULL,
                                &result->node)) {
            return false;
        }
        r_body_hir_node(body, result->node)->auxiliary_type = projection;
    } else {
        if (!r_semantic_constant_projection(context, owner, trait, index, &projection) ||
            !r_consteval_constant_value(context, owner, trait, index, &literal)) {
            return false;
        }
        if (literal == R_TYPE_ID_INVALID) {
            r_body_poison(body);
            return true;
        }
        if (!r_body_append_node(body,
                                R_HIR_LITERAL,
                                view.span,
                                constant_type,
                                0U,
                                R_TOKEN_INVALID,
                                r_semantic_type(context, literal)->length,
                                false,
                                NULL,
                                &result->node)) {
            return false;
        }
    }
    result->type = constant_type;
    result->valid = true;
    return true;
}

/* R-TYPE-0050: every closed implementation evaluates its constants, also when unused. */
static bool r_consteval_check_impl_constants(RFrontendContext *context) {
    size_t impl_index;
    for (impl_index = 0U; impl_index < context->semantic_impl_count; ++impl_index) {
        const RSemanticImpl impl = context->semantic_impls[impl_index];
        uint32_t index;
        if (impl.poisoned || impl.generic_schema != 0U || impl.trait == 0U ||
            r_generic_type_is_dependent(context, impl.target_type, 0U)) {
            continue;
        }
        for (index = 0U; index < context->semantic_traits[impl.trait - 1U].constant_count;
             ++index) {
            RTypeId literal;
            if (!r_consteval_constant_value(
                    context, impl.target_type, impl.trait, index, &literal)) {
                return false;
            }
        }
    }
    return true;
}

/* Evaluates the recorded module-scope expressions once bodies exist (discovery pass only). */
static bool r_consteval_discovery_finish(RFrontendContext *context) {
    const size_t count = context->consteval_value_count;
    size_t index;

    if (!context->consteval_ready) {
        return true;
    }
    for (index = 0U; index < count; ++index) {
        const RConstevalValue entry = context->consteval_values[index];
        RAstRef expression;
        RBodyContext body;
        RExpressionResult value;
        uint64_t bits = 0U;
        bool valid = false;
        const size_t first_diagnostic = context->diagnostic_count;

        if (!entry.request || entry.resolved) {
            continue;
        }
        if (entry.deferred != R_TYPE_ID_INVALID) {
            /* R-TYPE-0047: a computed bound of a generic instance closed before bodies. */
            if (!r_consteval_deferred_value(context, entry.deferred, &bits, &valid)) {
                return false;
            }
            if (valid) {
                RConstevalValue resolved = entry;
                resolved.bits = bits;
                resolved.resolved = true;
                if (!r_consteval_record(context, &resolved)) {
                    return false;
                }
            } else if (!r_consteval_record_failure(context,
                                                   entry.source,
                                                   entry.node,
                                                   entry.instance,
                                                   first_diagnostic,
                                                   entry.span,
                                                   NULL)) {
                return false;
            }
            continue;
        }
        expression.source = entry.source;
        expression.node = entry.node;
        (void)memset(&body, 0, sizeof(body));
        body.frontend = context;
        body.source = entry.source;
        body.translation_effects = true;
        if (entry.condition ? !r_body_lower_condition(&body, expression, &value)
                            : !r_body_lower_value(&body, expression, entry.expected, &value)) {
            r_context_free(context, body.active_symbols.items);
            return false;
        }
        r_context_free(context, body.active_symbols.items);
        if (!value.valid || body.poisoned || (!entry.condition && (value.type != entry.expected))) {
            /* A module condition is checked only here, so its own diagnostic is kept. */
            const bool own = entry.condition && (first_diagnostic < context->diagnostic_count);
            if (!r_consteval_record_failure(
                    context,
                    entry.source,
                    entry.node,
                    0U,
                    first_diagnostic,
                    entry.span,
                    own ? NULL
                    : entry.condition
                        ? "a module @if condition requires a constant condition"
                        : "a translation-time call in a module-scope declaration requires a closed "
                          "constant expression of the declared type")) {
                return false;
            }
            continue;
        }
        if (!r_consteval_required_scalar(context, value.node, &bits, &valid)) {
            return false;
        }
        if (valid) {
            RConstevalValue resolved = entry;
            resolved.bits = bits;
            resolved.resolved = true;
            if (!r_consteval_record(context, &resolved)) {
                return false;
            }
        } else if (!r_consteval_record_failure(
                       context, entry.source, entry.node, 0U, first_diagnostic, entry.span, NULL)) {
            return false;
        }
    }
    return true;
}

/* ---- deciding whether a discovery pass is needed ---- */

typedef struct RConstevalNames {
    uint32_t *items;
    size_t count;
    size_t capacity;
} RConstevalNames;

static bool r_consteval_names_contain(const RConstevalNames *names, uint32_t name) {
    size_t index;

    for (index = 0U; index < names->count; ++index) {
        if (names->items[index] == name) {
            return true;
        }
    }
    return false;
}

/* A module initializer other than integer literals, names and integer operators is computed by
 * translation-time evaluation (the operator subset is folded while declarations are collected). */
static bool r_consteval_ast_computed(const RFrontendContext *context,
                                     RAstRef ref,
                                     const RConstevalNames *names,
                                     uint32_t depth) {
    RAstNodeView view;
    RAstTokenView token;
    uint32_t index;

    if (depth > r_frontend_tree_depth_limit(context)) {
        return true;
    }
    if (r_ast_ref_token(context, ref, &token)) {
        switch (token.kind) {
        case R_TOKEN_INTEGER_LITERAL:
        case R_TOKEN_PLUS:
        case R_TOKEN_MINUS:
        case R_TOKEN_STAR:
        case R_TOKEN_SLASH:
        case R_TOKEN_PERCENT:
        case R_TOKEN_AMP:
        case R_TOKEN_PIPE:
        case R_TOKEN_CARET:
        case R_TOKEN_TILDE:
        case R_TOKEN_LESS_LESS:
        case R_TOKEN_GREATER_GREATER:
            return false;
        case R_TOKEN_IDENTIFIER:
            return r_consteval_names_contain(names, token.intern_id);
        default:
            return true;
        }
    }
    if (!r_ast_ref_view(context, ref, &view)) {
        return true;
    }
    if ((view.kind != R_SYNTAX_EXPRESSION) && (view.kind != R_SYNTAX_INITIALIZER) &&
        (view.kind != R_SYNTAX_BINARY_EXPRESSION) && (view.kind != R_SYNTAX_UNARY_EXPRESSION) &&
        (view.kind != R_SYNTAX_POSTFIX_EXPRESSION) && (view.kind != R_SYNTAX_PRIMARY_EXPRESSION)) {
        return true;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (!r_ast_ref_child(context, ref, index, &child) ||
            r_consteval_ast_computed(context, child, names, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static bool r_consteval_ast_mentions(const RFrontendContext *context,
                                     RAstRef ref,
                                     const RConstevalNames *names,
                                     uint32_t depth) {
    RAstNodeView view;
    RAstTokenView token;
    uint32_t index;

    if (depth > r_frontend_tree_depth_limit(context)) {
        return false;
    }
    if (r_ast_ref_token(context, ref, &token)) {
        return (token.kind == R_TOKEN_IDENTIFIER) &&
               r_consteval_names_contain(names, token.intern_id);
    }
    if (!r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    if ((view.kind == R_SYNTAX_CALL_SUFFIX) || (view.kind == R_SYNTAX_STANDARD_TYPE_CALL)) {
        return true;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) &&
            r_consteval_ast_mentions(context, child, names, depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* Types and enumerator values outside function bodies are checked before bodies exist. */
static bool r_consteval_ast_early_use(const RFrontendContext *context,
                                      RAstRef ref,
                                      const RConstevalNames *names,
                                      uint32_t depth) {
    RAstNodeView view;
    uint32_t index;

    if ((depth > r_frontend_tree_depth_limit(context)) || !r_ast_ref_view(context, ref, &view) ||
        (view.kind == R_SYNTAX_BLOCK)) {
        return false;
    }
    if ((view.kind == R_SYNTAX_TYPE) || (view.kind == R_SYNTAX_ENUM_VARIANT)) {
        return r_consteval_ast_mentions(context, ref, names, depth + 1U);
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) &&
            r_consteval_ast_early_use(context, child, names, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static bool r_consteval_needs_discovery(RFrontendContext *context, bool *needed) {
    RConstevalNames names = {NULL, 0U, 0U};
    size_t source_index;
    bool changed = true;
    bool success = false;

    *needed = false;
    while (changed) {
        changed = false;
        for (source_index = 0U; source_index < context->source_count; ++source_index) {
            RAstRef root;
            RAstNodeView root_view;
            uint32_t child_index;

            if (!r_semantic_root_ref(context, (RSourceId)(source_index + 1U), &root) ||
                !r_ast_ref_view(context, root, &root_view)) {
                continue;
            }
            for (child_index = 0U; child_index < root_view.child_count; ++child_index) {
                RAstRef external;
                RAstRef object;
                RAstRef initializer;
                RAstTokenView name;

                if (!r_ast_ref_child(context, root, child_index, &external) ||
                    !r_ast_find_direct_child(
                        context, external, R_SYNTAX_OBJECT_DECLARATION, &object) ||
                    !r_ast_find_direct_child(context, object, R_SYNTAX_INITIALIZER, &initializer) ||
                    !r_semantic_object_name(context, object, &name) ||
                    r_consteval_names_contain(&names, name.intern_id) ||
                    !r_consteval_ast_computed(context, initializer, &names, 0U)) {
                    continue;
                }
                if (!r_grow_array(context,
                                  (void **)&names.items,
                                  &names.capacity,
                                  sizeof(*names.items),
                                  names.count + 1U)) {
                    goto cleanup;
                }
                names.items[names.count++] = name.intern_id;
                changed = true;
            }
        }
    }
    /* R-TYPE-0050: implementations are collected after the types that may name their
       associated constants, so those names are candidates too. */
    for (source_index = 0U; source_index < context->source_count; ++source_index) {
        RAstRef root;
        RAstNodeView root_view;
        uint32_t child_index;
        if (!r_semantic_root_ref(context, (RSourceId)(source_index + 1U), &root) ||
            !r_ast_ref_view(context, root, &root_view)) {
            continue;
        }
        for (child_index = 0U; child_index < root_view.child_count; ++child_index) {
            RAstRef external;
            RAstRef declaration;
            RAstNodeView declaration_view;
            uint32_t member_index;
            if (!r_ast_ref_child(context, root, child_index, &external) ||
                !(r_ast_find_direct_child(
                      context, external, R_SYNTAX_TRAIT_DECLARATION, &declaration) ||
                  r_ast_find_direct_child(
                      context, external, R_SYNTAX_IMPL_DECLARATION, &declaration)) ||
                !r_ast_ref_view(context, declaration, &declaration_view)) {
                continue;
            }
            for (member_index = 0U; member_index < declaration_view.child_count; ++member_index) {
                RAstRef member;
                RAstNodeView member_view;
                RAstTokenView name;
                if (!r_ast_ref_child(context, declaration, member_index, &member) ||
                    !r_ast_ref_view(context, member, &member_view) ||
                    (member_view.kind != R_SYNTAX_ASSOCIATED_CONSTANT) ||
                    !r_ast_direct_token(context, member, 2U, NULL, &name) ||
                    r_consteval_names_contain(&names, name.intern_id)) {
                    continue;
                }
                if (!r_grow_array(context,
                                  (void **)&names.items,
                                  &names.capacity,
                                  sizeof(*names.items),
                                  names.count + 1U)) {
                    goto cleanup;
                }
                names.items[names.count++] = name.intern_id;
            }
        }
    }
    for (source_index = 0U; (source_index < context->source_count) && !*needed; ++source_index) {
        RAstRef root;
        if (r_semantic_root_ref(context, (RSourceId)(source_index + 1U), &root)) {
            *needed = r_consteval_ast_early_use(context, root, &names, 0U);
        }
    }
    success = true;

cleanup:
    r_context_free(context, names.items);
    return success;
}

/* ---- discovery passes ---- */

static bool r_consteval_copy_text(RFrontendContext *context,
                                  const RFrontendContext *shadow,
                                  uint32_t shadow_id,
                                  uint32_t *id);

/* A second context over the same sources, options and profile, seeded with the values known so
 * far; its analysis records every module-scope value it can compute. */
static RFrontendContext *r_consteval_shadow(RFrontendContext *context) {
    RFrontendOptions options = context->options;
    RFrontendContext *shadow;
    size_t index;

    options.limits.max_diagnostics = UINT32_MAX / 2U;
    shadow = r_frontend_create(&options);
    if (shadow == NULL) {
        return NULL;
    }
    shadow->profile = context->profile;
    shadow->deny_panic_alloc = context->deny_panic_alloc;
    shadow->consteval_discovery = true;
    for (index = 0U; index < context->source_count; ++index) {
        const RSource *source = &context->sources[index];
        RSourceId source_id = R_SOURCE_ID_INVALID;
        RSource *copy;

        if (r_frontend_add_source(shadow,
                                  source->display_name == NULL ? "" : source->display_name,
                                  source->bytes,
                                  source->length,
                                  &source_id) != R_FRONTEND_OK) {
            r_frontend_destroy(shadow);
            return NULL;
        }
        copy = r_get_source(shadow, source_id);
        if (copy == NULL) {
            r_frontend_destroy(shadow);
            return NULL;
        }
        copy->is_library = source->is_library;
        copy->minimum_profile = source->minimum_profile;
    }
    for (index = 0U; index < context->consteval_value_count; ++index) {
        RConstevalValue value = context->consteval_values[index];
        if (!value.resolved) {
            continue;
        }
        value.code = 0U;
        value.rule = 0U;
        value.message = 0U;
        value.request = false;
        value.expected = R_TYPE_ID_INVALID;
        value.condition = false;
        value.deferred = R_TYPE_ID_INVALID;
        if (!r_consteval_copy_text(shadow, context, value.instance, &value.instance) ||
            !r_consteval_record(shadow, &value)) {
            r_frontend_destroy(shadow);
            return NULL;
        }
    }
    return shadow;
}

static bool r_consteval_copy_text(RFrontendContext *context,
                                  const RFrontendContext *shadow,
                                  uint32_t shadow_id,
                                  uint32_t *id) {
    const RInternEntry *entry;

    *id = 0U;
    if ((shadow_id == 0U) || ((size_t)shadow_id > shadow->intern_count)) {
        return true;
    }
    entry = &shadow->intern_entries[(size_t)shadow_id - 1U];
    return r_intern_bytes(context, (const uint8_t *)entry->bytes, entry->length, id);
}

/* Merges one value of a discovery pass; a resolved value is never replaced. */
static bool r_consteval_merge(RFrontendContext *context,
                              const RFrontendContext *shadow,
                              const RConstevalValue *found,
                              bool *progress) {
    const RConstevalValue *existing;
    RConstevalValue value = *found;

    if (!r_consteval_copy_text(context, shadow, found->instance, &value.instance)) {
        return false;
    }
    existing = r_consteval_injected_instance(context, found->source, found->node, value.instance);
    if ((existing != NULL) && existing->resolved) {
        return true;
    }
    value.request = false;
    value.expected = R_TYPE_ID_INVALID;
    value.condition = false;
    value.deferred = R_TYPE_ID_INVALID;
    if (!r_consteval_copy_text(context, shadow, found->code, &value.code) ||
        !r_consteval_copy_text(context, shadow, found->rule, &value.rule) ||
        !r_consteval_copy_text(context, shadow, found->message, &value.message)) {
        return false;
    }
    if (value.resolved) {
        *progress = true;
    }
    return r_consteval_record(context, &value);
}

static bool
r_consteval_harvest(RFrontendContext *context, RFrontendContext *shadow, bool *progress) {
    size_t index;

    for (index = 0U; index < shadow->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &shadow->semantic_symbols[index];
        RConstevalValue value;

        if ((symbol->kind != R_SEMANTIC_SYMBOL_MODULE_CONSTANT) || !symbol->consteval_computed ||
            symbol->consteval_pending || symbol->poisoned) {
            continue;
        }
        (void)memset(&value, 0, sizeof(value));
        value.source = symbol->module_source;
        value.node = symbol->declaration_ast;
        value.bits = symbol->integer_value;
        value.resolved = true;
        value.span = symbol->name_span;
        if (!r_consteval_merge(context, shadow, &value, progress)) {
            return false;
        }
    }
    for (index = 0U; index < shadow->consteval_value_count; ++index) {
        const RConstevalValue value = shadow->consteval_values[index];
        if (!value.resolved && (value.message == 0U)) {
            continue;
        }
        if (!r_consteval_merge(context, shadow, &value, progress)) {
            return false;
        }
    }
    return true;
}

/* Runs discovery passes until no further module-scope value becomes known. */
static bool r_consteval_discover(RFrontendContext *context) {
    uint32_t round;

    for (round = 0U; round < (uint32_t)R_CONSTEVAL_DISCOVERY_ROUNDS; ++round) {
        RFrontendContext *shadow = r_consteval_shadow(context);
        RFrontendStatus status;
        bool progress = false;

        if (shadow == NULL) {
            context->resource_status = R_FRONTEND_OUT_OF_MEMORY;
            return false;
        }
        status = r_analyze_program(shadow);
        /* A pass that stops early simply discovers less; only exhausted memory is fatal. */
        if ((status != R_FRONTEND_INTERNAL_ERROR) && (shadow->resource_status == R_FRONTEND_OK)) {
            (void)r_consteval_discovery_finish(shadow);
        }
        if ((status == R_FRONTEND_OUT_OF_MEMORY) ||
            (shadow->resource_status == R_FRONTEND_OUT_OF_MEMORY) ||
            !r_consteval_harvest(context, shadow, &progress)) {
            r_frontend_destroy(shadow);
            context->resource_status = R_FRONTEND_OUT_OF_MEMORY;
            return false;
        }
        r_frontend_destroy(shadow);
        if (!progress) {
            return true;
        }
    }
    /* Every pass still computed a new value: the chain of module-scope values may be longer. */
    for (size_t source = 0U; source < context->source_count; ++source) {
        if (!context->sources[source].is_library) {
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-LIMIT-001",
                "R-LIMIT-0003",
                "module-scope translation-time values and module @if conditions need more than 32 "
                "discovery passes",
                (RSourceSpan){(RSourceId)(source + 1U), 0U, 0U});
        }
    }
    return true;
}
