#include "frontend_internal.h"
#include "standard_async_sync.h"
#include "standard_fs_async.h"
#include "standard_net_operations.h"
#include "standard_scoped_operations.h"
#include "standard_sync.h"

typedef struct RMirTemporaryBlock {
    RMirInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    bool terminated;
} RMirTemporaryBlock;

typedef struct RMirPlace {
    RSymbolId symbol;
    uint32_t ordinal;
    bool is_parameter;
} RMirPlace;

typedef struct RMirValueVector {
    RMirValueId *items;
    size_t count;
    size_t capacity;
} RMirValueVector;

typedef struct RMirEffectHandler {
    RTypeId error_type;
    size_t block;
    size_t finally_count;
    /* The first handler of the same try statement (R-ERR-0003, L21.2). */
    size_t group;
} RMirEffectHandler;

typedef struct RMirFinally {
    RHirNodeId node;
    size_t entry_block;
    uint32_t identifier;
} RMirFinally;

typedef struct RMirPendingCompletion {
    RMirPendingCompletionReason reason;
    RTypeId type;
    RMirValueId payload;
    RMirBlockId target;
} RMirPendingCompletion;

/* The jump targets of an enclosing loop, for labeled jumps (R-STMT-0004). */
typedef struct RMirLoopTarget {
    size_t break_target;
    size_t continue_target;
    size_t finally_count;
} RMirLoopTarget;

typedef struct RMirBuildContext {
    RFrontendContext *frontend;
    RMirTemporaryBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    RMirPlace *places;
    size_t place_count;
    size_t place_capacity;
    size_t current_block;
    uint32_t next_value;
    uint32_t next_parameter;
    uint32_t next_local;
    size_t break_target;
    size_t continue_target;
    size_t break_finally_count;
    size_t continue_finally_count;
    RMirLoopTarget *loops;
    size_t loop_count;
    size_t loop_capacity;
    RTypeId function_effects;
    RMirEffectHandler *effect_handlers;
    size_t effect_handler_count;
    size_t effect_handler_capacity;
    RMirValueVector pending_aggregate_values;
    RMirFinally *finalies;
    size_t finally_count;
    size_t finally_capacity;
    uint32_t next_finally_identifier;
} RMirBuildContext;

typedef struct RMirExpressionResult {
    RMirValueId value;
    RSymbolId place_symbol;
    uint32_t place_ordinal;
    RMirValueId place_projection;
    bool place_is_parameter;
    bool is_place;
} RMirExpressionResult;

typedef struct RMirScalarCase {
    RHirNodeId node;
    size_t block;
} RMirScalarCase;

static bool r_mir_standard_type_name_equal(const RFrontendContext *context,
                                           const RSemanticType *type,
                                           const char *name) {
    const RInternEntry *entry;
    const size_t name_length = strlen(name);

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->length == UINT64_C(0)) || (type->length > (uint64_t)context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)type->length - 1U];
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_mir_standard_type_is_exact(const RFrontendContext *context,
                                         const RSemanticType *type,
                                         const char *name) {
    return r_mir_standard_type_name_equal(context, type, name) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base == R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID);
}

static bool r_mir_effect_set_is_single_standard_type(const RFrontendContext *context,
                                                     RTypeId effects,
                                                     const char *name) {
    const RTypeId error_type = r_semantic_effect_at(context, effects, UINT32_C(0));

    return (r_semantic_effect_count(context, effects) == UINT32_C(1)) &&
           r_mir_standard_type_is_exact(context, r_semantic_type(context, error_type), name);
}

static uint32_t r_mir_standard_outcome_variant_count(const RFrontendContext *context,
                                                     const RSemanticType *type) {
    if (type != NULL && type->base != R_TYPE_ID_INVALID && type->second == R_TYPE_ID_INVALID) {
        size_t index;
        for (index = 0U; r_standard_sync_outcome_at(index) != NULL; ++index) {
            const RStandardSyncOutcome *schema = r_standard_sync_outcome_at(index);
            if (r_mir_standard_type_name_equal(context, type, schema->name))
                return schema->variant_count;
        }
    }
    if (r_mir_standard_type_name_equal(context, type, "core::atomic_compare_exchange_result") &&
        (type->base != R_TYPE_ID_INVALID) && (type->second == R_TYPE_ID_INVALID)) {
        return UINT32_C(2);
    }
    if (r_mir_standard_type_name_equal(context, type, "std.async::broadcast_result") &&
        (type->base != R_TYPE_ID_INVALID) && (type->second == R_TYPE_ID_INVALID)) {
        return UINT32_C(3);
    }
    if ((r_mir_standard_type_name_equal(context, type, "std.thread::join_result") ||
         r_mir_standard_type_name_equal(context, type, "std.arc::try_unwrap_result") ||
         r_mir_standard_type_name_equal(context, type, "std.rc::try_unwrap_result")) &&
        (type->base != R_TYPE_ID_INVALID) && (type->second == R_TYPE_ID_INVALID)) {
        return UINT32_C(2);
    }
    if ((r_mir_standard_type_name_equal(context, type, "std.array::push_error") ||
         r_mir_standard_type_name_equal(context, type, "std.list::push_error")) &&
        (type->base != R_TYPE_ID_INVALID) && (type->second == R_TYPE_ID_INVALID)) {
        return 1U;
    }
    if (r_mir_standard_type_name_equal(context, type, "std.dict::insert_error") &&
        (type->base != R_TYPE_ID_INVALID) && (type->second != R_TYPE_ID_INVALID)) {
        return 1U;
    }
    if (r_mir_standard_type_is_exact(context, type, "std.fs::directory_next_result"))
        return UINT32_C(3);
    for (size_t index = 0U; r_standard_net_outcome_at(index) != NULL; ++index) {
        const RStandardNetOutcome *net = r_standard_net_outcome_at(index);
        if (r_mir_standard_type_is_exact(context, type, net->name))
            return net->variant_count;
    }
    if (r_mir_standard_type_is_exact(context, type, "std.io::read_result")) {
        return UINT32_C(3);
    }
    if (r_mir_standard_type_is_exact(context, type, "std.string::from_bytes_result") ||
        r_mir_standard_type_is_exact(context, type, "std.process::spawn_result") ||
        r_mir_standard_type_is_exact(context, type, "std.process::wait_result") ||
        r_mir_standard_type_is_exact(context, type, "std.io::write_result") ||
        r_mir_standard_type_is_exact(context, type, "std.io::write_all_result") ||
        r_mir_standard_type_is_exact(context, type, "std.io::shared_write_result") ||
        r_mir_standard_type_is_exact(context, type, "std.fs::write_file_result")) {
        return UINT32_C(2);
    }
    return UINT32_C(0);
}

static const RHirNode *r_mir_hir_node(const RFrontendContext *context, RHirNodeId node_id) {
    if ((node_id == R_HIR_NODE_ID_INVALID) || ((size_t)node_id > context->hir_node_count)) {
        return NULL;
    }
    return &context->hir_nodes[(size_t)node_id - 1U];
}

static RHirNodeId
r_mir_hir_child(const RFrontendContext *context, const RHirNode *node, uint32_t child_index) {
    size_t index;

    if ((node == NULL) || (child_index >= node->child_count)) {
        return R_HIR_NODE_ID_INVALID;
    }
    index = (size_t)node->first_child + (size_t)child_index;
    if (index >= context->hir_child_count) {
        return R_HIR_NODE_ID_INVALID;
    }
    return context->hir_children[index];
}

static bool
r_mir_push_place(RMirBuildContext *build, RSymbolId symbol, uint32_t ordinal, bool is_parameter) {
    RMirPlace *place;

    if (!r_grow_array(build->frontend,
                      (void **)&build->places,
                      &build->place_capacity,
                      sizeof(*build->places),
                      build->place_count + 1U)) {
        return false;
    }
    place = &build->places[build->place_count];
    place->symbol = symbol;
    place->ordinal = ordinal;
    place->is_parameter = is_parameter;
    build->place_count += 1U;
    return true;
}

static RMirInstruction *r_mir_temporary_value_definition(RMirBuildContext *build,
                                                         RMirValueId value) {
    size_t block_index;

    if (value == R_MIR_VALUE_ID_INVALID) {
        return NULL;
    }
    for (block_index = 0U; block_index < build->block_count; ++block_index) {
        RMirTemporaryBlock *block = &build->blocks[block_index];
        size_t instruction_index;

        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            RMirInstruction *instruction = &block->instructions[instruction_index];

            if (instruction->result == value) {
                return instruction;
            }
        }
    }
    return NULL;
}

static const RMirPlace *r_mir_find_place(const RMirBuildContext *build, RSymbolId symbol) {
    size_t index;

    for (index = build->place_count; index != 0U; --index) {
        if (build->places[index - 1U].symbol == symbol) {
            return &build->places[index - 1U];
        }
    }
    return NULL;
}

static bool r_mir_add_block(RMirBuildContext *build, size_t *block_index) {
    RMirTemporaryBlock *block;

    if ((build->block_count >= UINT32_MAX) || !r_grow_array(build->frontend,
                                                            (void **)&build->blocks,
                                                            &build->block_capacity,
                                                            sizeof(*build->blocks),
                                                            build->block_count + 1U)) {
        if (build->frontend->resource_status == R_FRONTEND_OK) {
            build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        }
        return false;
    }
    block = &build->blocks[build->block_count];
    (void)memset(block, 0, sizeof(*block));
    *block_index = build->block_count;
    build->block_count += 1U;
    return true;
}

static bool r_mir_append_instruction(RMirBuildContext *build, RMirInstruction instruction) {
    RMirTemporaryBlock *block;

    if (build->current_block >= build->block_count) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    block = &build->blocks[build->current_block];
    if (block->terminated || !r_grow_array(build->frontend,
                                           (void **)&block->instructions,
                                           &block->instruction_capacity,
                                           sizeof(*block->instructions),
                                           block->instruction_count + 1U)) {
        if (block->terminated && (build->frontend->resource_status == R_FRONTEND_OK)) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        }
        return false;
    }
    block->instructions[block->instruction_count] = instruction;
    block->instruction_count += 1U;
    if ((instruction.kind == R_MIR_INSTRUCTION_BRANCH) ||
        (instruction.kind == R_MIR_INSTRUCTION_JUMP) ||
        (instruction.kind == R_MIR_INSTRUCTION_AWAIT) ||
        (instruction.kind == R_MIR_INSTRUCTION_TASK_SCOPE_WAIT) ||
        (instruction.kind == R_MIR_INSTRUCTION_RETURN) ||
        (instruction.kind == R_MIR_INSTRUCTION_THROW) ||
        (instruction.kind == R_MIR_INSTRUCTION_FINALLY_EXIT) ||
        (instruction.kind == R_MIR_INSTRUCTION_CANCEL) ||
        (instruction.kind == R_MIR_INSTRUCTION_UNREACHABLE)) {
        block->terminated = true;
    }
    return true;
}

static bool r_mir_new_value(RMirBuildContext *build, RMirValueId *value) {
    if (build->next_value == UINT32_MAX) {
        build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    build->next_value += 1U;
    *value = build->next_value;
    return true;
}

static bool
r_mir_value_vector_push(RMirBuildContext *build, RMirValueVector *values, RMirValueId value) {
    if (!r_grow_array(build->frontend,
                      (void **)&values->items,
                      &values->capacity,
                      sizeof(*values->items),
                      values->count + 1U)) {
        return false;
    }
    values->items[values->count] = value;
    values->count += 1U;
    return true;
}

static bool r_mir_push_effect_handler(
    RMirBuildContext *build, RTypeId error_type, size_t block, size_t finally_count, size_t group) {
    RMirEffectHandler *handler;

    if (!r_grow_array(build->frontend,
                      (void **)&build->effect_handlers,
                      &build->effect_handler_capacity,
                      sizeof(*build->effect_handlers),
                      build->effect_handler_count + 1U)) {
        return false;
    }
    handler = &build->effect_handlers[build->effect_handler_count];
    handler->error_type = error_type;
    handler->block = block;
    handler->finally_count = finally_count;
    handler->group = group;
    build->effect_handler_count += 1U;
    return true;
}

static bool r_mir_push_finally(RMirBuildContext *build,
                               RHirNodeId node,
                               size_t entry_block,
                               uint32_t *identifier) {
    RMirFinally *finally_context;

    if ((entry_block >= build->block_count) || (build->next_finally_identifier == UINT32_MAX)) {
        build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(build->frontend,
                      (void **)&build->finalies,
                      &build->finally_capacity,
                      sizeof(*build->finalies),
                      build->finally_count + 1U)) {
        return false;
    }
    build->next_finally_identifier += UINT32_C(1);
    finally_context = &build->finalies[build->finally_count];
    finally_context->node = node;
    finally_context->entry_block = entry_block;
    finally_context->identifier = build->next_finally_identifier;
    build->finally_count += 1U;
    if (identifier != NULL) {
        *identifier = finally_context->identifier;
    }
    return true;
}

/* The innermost try statement with a clause for the error or an ancestor handles it, by the
   clause of the nearest ancestor (R-ERR-0003). */
static const RMirEffectHandler *r_mir_find_effect_handler(const RMirBuildContext *build,
                                                          RTypeId error_type) {
    size_t end = build->effect_handler_count;

    while (end != 0U) {
        const size_t start = build->effect_handlers[end - 1U].group;
        const RMirEffectHandler *nearest = NULL;
        uint32_t nearest_distance = UINT32_MAX;

        if (start >= end) {
            return NULL;
        }
        for (size_t index = start; index < end; ++index) {
            const RMirEffectHandler *handler = &build->effect_handlers[index];
            const uint32_t distance =
                r_semantic_error_catch_distance(build->frontend, handler->error_type, error_type);

            if (distance < nearest_distance) {
                nearest = handler;
                nearest_distance = distance;
            }
        }
        if (nearest != NULL) {
            return nearest;
        }
        end = start;
    }
    return NULL;
}

static uint64_t
r_mir_effect_tag(const RFrontendContext *context, RTypeId effects, RTypeId error_type) {
    const uint32_t count = r_semantic_effect_count(context, effects);
    uint32_t index;

    for (index = 0U; index < count; ++index) {
        if (r_semantic_effect_at(context, effects, index) == error_type) {
            return (uint64_t)index + UINT64_C(1);
        }
    }
    return UINT64_C(0);
}

static bool r_mir_append_operands(RMirBuildContext *build,
                                  const RMirValueVector *values,
                                  uint32_t *first_operand) {
    RFrontendContext *context = build->frontend;

    if ((context->mir_operand_count > UINT32_MAX) || (values->count > UINT32_MAX) ||
        (values->count > ((size_t)UINT32_MAX - context->mir_operand_count))) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&context->mir_operands,
                      &context->mir_operand_capacity,
                      sizeof(*context->mir_operands),
                      context->mir_operand_count + values->count) ||
        !r_grow_array(context,
                      (void **)&context->mir_operand_members,
                      &context->mir_operand_member_capacity,
                      sizeof(*context->mir_operand_members),
                      context->mir_operand_count + values->count)) {
        return false;
    }
    *first_operand = (uint32_t)context->mir_operand_count;
    if (values->count != 0U) {
        (void)memcpy(context->mir_operands + context->mir_operand_count,
                     values->items,
                     values->count * sizeof(*values->items));
        (void)memset(context->mir_operand_members + context->mir_operand_count,
                     0,
                     values->count * sizeof(*context->mir_operand_members));
        context->mir_operand_count += values->count;
    }
    return true;
}

static bool r_mir_append_aggregate_operands(RMirBuildContext *build,
                                            const RMirValueVector *values,
                                            const uint32_t *members,
                                            uint32_t *first_operand) {
    RFrontendContext *context = build->frontend;

    if ((values->count != 0U) && (members == NULL)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_mir_append_operands(build, values, first_operand)) {
        return false;
    }
    if (values->count != 0U) {
        (void)memcpy(context->mir_operand_members + *first_operand,
                     members,
                     values->count * sizeof(*members));
    }
    return true;
}

static RTokenKind r_mir_compound_operation(RTokenKind operation) {
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
        return R_TOKEN_INVALID;
    }
}

/* Whether a type, with outer const removed, is never (R-TYPE-0007). */
static bool r_mir_type_is_never(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type =
        r_semantic_type(context, r_semantic_representation_type(context, type_id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_NEVER);
}

/* R-TYPE-0007: a read of a never object is unreachable and produces no value. */
static bool r_mir_lower_never_read(RMirBuildContext *build,
                                   const RHirNode *node,
                                   RMirExpressionResult *result) {
    (void)memset(result, 0, sizeof(*result));
    result->value = R_MIR_VALUE_ID_INVALID;
    if (!build->blocks[build->current_block].terminated) {
        RMirInstruction unreachable;

        (void)memset(&unreachable, 0, sizeof(unreachable));
        unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
        unreachable.span = node->span;
        return r_mir_append_instruction(build, unreachable);
    }
    return true;
}

/* R-TYPE-0007: an operand of type never does not complete, so neither does the expression that
   holds it; the rest lowers into a block without entries, where a placeholder of the expression's
   type keeps later uses well formed. */
static bool r_mir_lower_never_operand_completion(RMirBuildContext *build,
                                                 const RHirNode *node,
                                                 RMirExpressionResult *result) {
    const RSemanticType *type = r_semantic_type(
        build->frontend, r_semantic_representation_type(build->frontend, node->type));
    RMirInstruction placeholder;

    (void)memset(result, 0, sizeof(*result));
    result->value = R_MIR_VALUE_ID_INVALID;
    if (!build->blocks[build->current_block].terminated) {
        (void)memset(&placeholder, 0, sizeof(placeholder));
        placeholder.kind = R_MIR_INSTRUCTION_UNREACHABLE;
        placeholder.span = node->span;
        if (!r_mir_append_instruction(build, placeholder)) {
            return false;
        }
    }
    if ((type == NULL) || (type->kind == R_SEMANTIC_TYPE_VOID) ||
        (type->kind == R_SEMANTIC_TYPE_NEVER)) {
        return true;
    }
    {
        size_t unreachable_block;

        if (!r_mir_add_block(build, &unreachable_block)) {
            return false;
        }
        build->current_block = unreachable_block;
    }
    (void)memset(&placeholder, 0, sizeof(placeholder));
    placeholder.kind = R_MIR_INSTRUCTION_CAST;
    placeholder.span = node->span;
    placeholder.type = node->type;
    placeholder.operation = R_TOKEN_INVALID;
    if (!r_mir_new_value(build, &placeholder.result) ||
        !r_mir_append_instruction(build, placeholder)) {
        return false;
    }
    result->value = placeholder.result;
    return true;
}

static bool r_mir_lower_expression(RMirBuildContext *build,
                                   RHirNodeId node_id,
                                   uint32_t depth,
                                   RMirExpressionResult *result);
static bool r_mir_lower_statement(RMirBuildContext *build, RHirNodeId node_id, uint32_t depth);

static bool r_mir_append_jump(RMirBuildContext *build, RSourceSpan span, size_t target) {
    RMirInstruction instruction;

    if (target >= UINT32_MAX) {
        build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_JUMP;
    instruction.span = span;
    instruction.target0 = (RMirBlockId)(target + 1U);
    return r_mir_append_instruction(build, instruction);
}

static bool r_mir_append_cancel(RMirBuildContext *build, RSourceSpan span) {
    RMirInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_CANCEL;
    instruction.span = span;
    return r_mir_append_instruction(build, instruction);
}

static RMirPendingCompletion r_mir_pending_completion(RMirPendingCompletionReason reason,
                                                      RTypeId type,
                                                      RMirValueId payload,
                                                      RMirBlockId target) {
    const RMirPendingCompletion pending = {
        .reason = reason,
        .type = type,
        .payload = payload,
        .target = target,
    };
    return pending;
}

static bool r_mir_append_pending_marker(RMirBuildContext *build,
                                        RMirInstructionKind kind,
                                        RSourceSpan span,
                                        const RMirPendingCompletion *pending,
                                        size_t stop_count,
                                        size_t resume_block,
                                        uint32_t innermost_finally) {
    RMirInstruction instruction;

    if ((pending == NULL) || (pending->reason == R_MIR_PENDING_COMPLETION_INVALID) ||
        ((kind != R_MIR_INSTRUCTION_PENDING_SET) && (kind != R_MIR_INSTRUCTION_PENDING_RESUME)) ||
        (stop_count > UINT32_MAX) || (resume_block >= UINT32_MAX) ||
        (innermost_finally == UINT32_C(0))) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = kind;
    instruction.span = span;
    instruction.type = pending->type;
    instruction.operand0 = pending->payload;
    instruction.target0 = pending->target;
    instruction.target1 = (RMirBlockId)(resume_block + 1U);
    instruction.integer_value = (uint64_t)pending->reason;
    instruction.place_ordinal = (uint32_t)stop_count;
    instruction.aggregate_member = innermost_finally;
    return r_mir_append_instruction(build, instruction);
}

static bool r_mir_begin_pending_completion(RMirBuildContext *build,
                                           size_t stop_count,
                                           RSourceSpan transfer_span,
                                           const RMirPendingCompletion *pending,
                                           size_t *resume_block) {
    const size_t saved_count = build->finally_count;
    const RMirFinally *innermost;

    if ((resume_block == NULL) || (stop_count > saved_count)) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (stop_count == saved_count) {
        *resume_block = SIZE_MAX;
        return true;
    }
    innermost = &build->finalies[saved_count - 1U];
    if ((innermost->entry_block >= build->block_count) || !r_mir_add_block(build, resume_block) ||
        !r_mir_append_pending_marker(build,
                                     R_MIR_INSTRUCTION_PENDING_SET,
                                     transfer_span,
                                     pending,
                                     stop_count,
                                     *resume_block,
                                     innermost->identifier)) {
        return false;
    }
    return true;
}

static bool r_mir_finish_pending_completion(RMirBuildContext *build,
                                            size_t stop_count,
                                            RSourceSpan transfer_span,
                                            const RMirPendingCompletion *pending,
                                            size_t resume_block) {
    const size_t saved_count = build->finally_count;
    const RMirFinally *innermost;

    if (resume_block == SIZE_MAX) {
        return stop_count == saved_count;
    }
    if ((stop_count >= saved_count) || (resume_block >= build->block_count)) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    innermost = &build->finalies[saved_count - 1U];
    if ((innermost->entry_block >= build->block_count) ||
        !r_mir_append_jump(build, transfer_span, innermost->entry_block)) {
        return false;
    }
    build->current_block = resume_block;
    return r_mir_append_pending_marker(build,
                                       R_MIR_INSTRUCTION_PENDING_RESUME,
                                       transfer_span,
                                       pending,
                                       stop_count,
                                       resume_block,
                                       innermost->identifier);
}

static bool r_mir_lower_active_finalies(RMirBuildContext *build,
                                        size_t stop_count,
                                        RSourceSpan transfer_span,
                                        const RMirPendingCompletion *pending) {
    size_t resume_block;

    return r_mir_begin_pending_completion(
               build, stop_count, transfer_span, pending, &resume_block) &&
           r_mir_finish_pending_completion(build, stop_count, transfer_span, pending, resume_block);
}

static bool r_mir_lower_pending_cleanup_segments(RMirBuildContext *build,
                                                 const RHirNode *cleanup_owner,
                                                 uint32_t cleanup_start,
                                                 size_t stop_count,
                                                 RSourceSpan transfer_span,
                                                 const RMirPendingCompletion *pending,
                                                 uint32_t depth) {
    const size_t saved_finally_count = build->finally_count;
    size_t crossed_finally_count;
    uint32_t cleanup_index = cleanup_start;
    size_t defer_finally_count;
    bool success = false;

    if ((cleanup_owner == NULL) || (cleanup_start > cleanup_owner->child_count) ||
        (stop_count > saved_finally_count)) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    crossed_finally_count = saved_finally_count - stop_count;
    for (defer_finally_count = 0U; defer_finally_count < crossed_finally_count;
         ++defer_finally_count) {
        const size_t segment_finally_count = saved_finally_count - defer_finally_count;
        const size_t segment_stop_count = segment_finally_count - 1U;
        size_t resume_block;

        build->finally_count = segment_finally_count;
        if (!r_mir_begin_pending_completion(
                build, segment_stop_count, transfer_span, pending, &resume_block)) {
            goto cleanup;
        }
        while (cleanup_index < cleanup_owner->child_count) {
            const RHirNodeId cleanup_id =
                r_mir_hir_child(build->frontend, cleanup_owner, cleanup_index);
            const RHirNode *cleanup_node = r_mir_hir_node(build->frontend, cleanup_id);

            if ((cleanup_node == NULL) || (cleanup_node->kind != R_HIR_DROP)) {
                build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                goto cleanup;
            }
            if ((cleanup_node->operation == R_TOKEN_INVALID) &&
                (cleanup_node->integer_value == UINT64_MAX)) {
                cleanup_index += UINT32_C(1);
                continue;
            }
            if (cleanup_node->integer_value < (uint64_t)defer_finally_count) {
                build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                goto cleanup;
            }
            if (cleanup_node->integer_value > (uint64_t)defer_finally_count) {
                break;
            }
            if (!r_mir_lower_statement(build, cleanup_id, depth + UINT32_C(1))) {
                goto cleanup;
            }
            cleanup_index += UINT32_C(1);
        }
        if (!r_mir_finish_pending_completion(
                build, segment_stop_count, transfer_span, pending, resume_block)) {
            goto cleanup;
        }
    }
    build->finally_count = stop_count;
    while (cleanup_index < cleanup_owner->child_count) {
        const RHirNodeId cleanup_id =
            r_mir_hir_child(build->frontend, cleanup_owner, cleanup_index);
        const RHirNode *cleanup_node = r_mir_hir_node(build->frontend, cleanup_id);

        if ((cleanup_node != NULL) && (cleanup_node->kind == R_HIR_DROP) &&
            (cleanup_node->operation == R_TOKEN_INVALID) &&
            (cleanup_node->integer_value == UINT64_MAX)) {
            cleanup_index += UINT32_C(1);
            continue;
        }
        if ((cleanup_node == NULL) || (cleanup_node->kind != R_HIR_DROP) ||
            (cleanup_node->integer_value != (uint64_t)crossed_finally_count) ||
            !r_mir_lower_statement(build, cleanup_id, depth + UINT32_C(1))) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        cleanup_index += UINT32_C(1);
    }
    success = true;

cleanup:
    build->finally_count = saved_finally_count;
    return success;
}

static bool r_mir_lower_short_circuit(RMirBuildContext *build,
                                      const RHirNode *node,
                                      uint32_t depth,
                                      RMirExpressionResult *result) {
    RMirExpressionResult left;
    RMirExpressionResult right;
    RMirInstruction instruction;
    size_t left_predecessor;
    size_t right_block;
    size_t merge_block;
    size_t right_predecessor;

    if (!r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &left) ||
        left.is_place || (left.value == R_MIR_VALUE_ID_INVALID)) {
        return false;
    }
    left_predecessor = build->current_block;
    if (!r_mir_add_block(build, &right_block) || !r_mir_add_block(build, &merge_block)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_BRANCH;
    instruction.span = node->span;
    instruction.operand0 = left.value;
    if (node->operation == R_TOKEN_AMP_AMP) {
        instruction.target0 = (RMirBlockId)(right_block + 1U);
        instruction.target1 = (RMirBlockId)(merge_block + 1U);
    } else {
        instruction.target0 = (RMirBlockId)(merge_block + 1U);
        instruction.target1 = (RMirBlockId)(right_block + 1U);
    }
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    build->current_block = right_block;
    if (!r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U, &right) ||
        right.is_place || (right.value == R_MIR_VALUE_ID_INVALID)) {
        return false;
    }
    right_predecessor = build->current_block;
    if (!r_mir_append_jump(build, node->span, merge_block)) {
        return false;
    }
    build->current_block = merge_block;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_PHI;
    instruction.span = node->span;
    instruction.type = node->type;
    instruction.operand0 = left.value;
    instruction.operand1 = right.value;
    instruction.target0 = (RMirBlockId)(left_predecessor + 1U);
    instruction.target1 = (RMirBlockId)(right_predecessor + 1U);
    if (!r_mir_new_value(build, &instruction.result) ||
        !r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    result->value = instruction.result;
    return true;
}

static bool r_mir_lower_conditional(RMirBuildContext *build,
                                    const RHirNode *node,
                                    uint32_t depth,
                                    RMirExpressionResult *result) {
    RMirExpressionResult condition;
    RMirExpressionResult arms[2] = {{0}, {0}};
    bool reaches[2] = {true, true};
    RMirInstruction instruction;
    size_t blocks[2];
    size_t predecessors[2];
    size_t merge;
    uint32_t index;

    if (!r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, 0U), depth + 1U, &condition) ||
        condition.is_place || (condition.value == R_MIR_VALUE_ID_INVALID) ||
        !r_mir_add_block(build, &blocks[0]) || !r_mir_add_block(build, &blocks[1]) ||
        !r_mir_add_block(build, &merge)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_BRANCH;
    instruction.span = node->span;
    instruction.operand0 = condition.value;
    instruction.target0 = (RMirBlockId)(blocks[0] + 1U);
    instruction.target1 = (RMirBlockId)(blocks[1] + 1U);
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    for (index = 0U; index < 2U; ++index) {
        build->current_block = blocks[index];
        const RHirNodeId arm_id = r_mir_hir_child(build->frontend, node, index + 1U);
        const RHirNode *arm = r_mir_hir_node(build->frontend, arm_id);
        const RSemanticType *arm_type =
            arm == NULL ? NULL : r_semantic_type(build->frontend, arm->type);
        reaches[index] = arm_type == NULL || arm_type->kind != R_SEMANTIC_TYPE_NEVER;
        if (!r_mir_lower_expression(build, arm_id, depth + 1U, &arms[index]) ||
            arms[index].is_place || (reaches[index] && arms[index].value == 0U))
            return false;
        if (!reaches[index]) {
            if (!build->blocks[build->current_block].terminated) {
                RMirInstruction unreachable = {0};
                unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
                unreachable.span = node->span;
                if (!r_mir_append_instruction(build, unreachable))
                    return false;
            }
            continue;
        }
        predecessors[index] = build->current_block;
        if (!r_mir_append_jump(build, node->span, merge)) {
            return false;
        }
    }
    build->current_block = merge;
    if (!reaches[0] && !reaches[1]) {
        RMirInstruction unreachable = {0};
        unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
        unreachable.span = node->span;
        (void)memset(result, 0, sizeof(*result));
        return r_mir_append_instruction(build, unreachable);
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_PHI;
    instruction.span = node->span;
    instruction.type = node->type;
    instruction.borrow_origin = node->borrow_origin;
    instruction.borrow_origin_set = node->borrow_origin_set;
    instruction.borrow_origin_multiple = node->borrow_origin_multiple;
    const uint32_t first = reaches[0] ? 0U : 1U;
    instruction.operand0 = arms[first].value;
    instruction.target0 = (RMirBlockId)(predecessors[first] + 1U);
    if (reaches[0] && reaches[1]) {
        instruction.operand1 = arms[1].value;
        instruction.target1 = (RMirBlockId)(predecessors[1] + 1U);
    }
    if (!r_mir_new_value(build, &instruction.result) ||
        !r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    result->value = instruction.result;
    return true;
}

static bool r_mir_call_borrow_mask_has(const RMirCallBorrowMask *mask, uint32_t index) {
    if (index < UINT32_C(64)) {
        return (mask->low & (UINT64_C(1) << index)) != UINT64_C(0);
    }
    if (index < UINT32_C(128)) {
        return (mask->high & (UINT64_C(1) << (index - UINT32_C(64)))) != UINT64_C(0);
    }
    return false;
}

static bool
r_mir_call_borrow_mask_equals(const RMirCallBorrowMask *mask, uint64_t low, uint64_t high) {
    return (mask->low == low) && (mask->high == high);
}

static bool r_mir_build_call_borrow_mask(const RMirBuildContext *build,
                                         const RHirNode *node,
                                         RMirCallBorrowMask *mask) {
    const RFrontendContext *context = build->frontend;
    const RSemanticSymbol *function;
    size_t first_parameter_type;
    uint32_t parameter_index;

    mask->low = UINT64_C(0);
    mask->high = UINT64_C(0);
    if ((node->kind != R_HIR_CALL) || (node->symbol == R_SYMBOL_ID_INVALID) ||
        ((size_t)node->symbol > context->semantic_symbol_count)) {
        return false;
    }
    function = &context->semantic_symbols[(size_t)node->symbol - 1U];
    first_parameter_type = (size_t)function->first_parameter_type;
    if ((function->kind != R_SEMANTIC_SYMBOL_FUNCTION) || function->is_async ||
        !function->signature_supported || function->poisoned ||
        (function->parameter_count != node->child_count) ||
        (first_parameter_type > context->semantic_parameter_type_count) ||
        ((size_t)function->parameter_count >
         (context->semantic_parameter_type_count - first_parameter_type))) {
        return false;
    }
    for (parameter_index = UINT32_C(0); parameter_index < function->parameter_count;
         ++parameter_index) {
        const RTypeId parameter_type_id =
            context->semantic_parameter_types[first_parameter_type + (size_t)parameter_index];
        const RSemanticType *parameter_type = r_semantic_type(context, parameter_type_id);
        if (parameter_type == NULL) {
            return false;
        }
        if (r_semantic_type_crosses_await(context, parameter_type_id)) {
            if (parameter_index >= UINT32_C(128)) {
                return false;
            }
            if (parameter_index < UINT32_C(64)) {
                mask->low |= UINT64_C(1) << parameter_index;
            } else {
                mask->high |= UINT64_C(1) << (parameter_index - UINT32_C(64));
            }
        }
    }
    return true;
}

static bool r_mir_prepare_throw_value(RMirBuildContext *build,
                                      RSourceSpan span,
                                      RTypeId error_type,
                                      RMirValueId payload,
                                      RMirInstruction *instruction,
                                      RMirPendingCompletion *pending,
                                      size_t *finally_count) {
    const RMirEffectHandler *handler = r_mir_find_effect_handler(build, error_type);
    const uint64_t tag =
        handler == NULL ? r_mir_effect_tag(build->frontend, build->function_effects, error_type)
                        : UINT64_C(0);
    const RMirBlockId target =
        handler == NULL ? R_MIR_BLOCK_ID_INVALID : (RMirBlockId)(handler->block + 1U);

    if ((instruction == NULL) || (pending == NULL) || (finally_count == NULL) ||
        (payload == R_MIR_VALUE_ID_INVALID) || ((handler == NULL) && (tag == UINT64_C(0)))) {
        return false;
    }
    if ((handler != NULL) && (handler->error_type != error_type)) {
        /* L21.2: a clause of a family receives the exact error as the variant holding it. */
        const uint32_t variant =
            r_semantic_error_family_tag(build->frontend, handler->error_type, error_type);
        RMirInstruction upcast;

        if (variant == UINT32_MAX) {
            return false;
        }
        (void)memset(&upcast, 0, sizeof(upcast));
        upcast.kind = R_MIR_INSTRUCTION_VARIANT;
        upcast.span = span;
        upcast.type = handler->error_type;
        upcast.integer_value = (uint64_t)variant;
        upcast.operand0 = payload;
        if (!r_mir_new_value(build, &upcast.result) || !r_mir_append_instruction(build, upcast)) {
            return false;
        }
        payload = upcast.result;
        error_type = handler->error_type;
    }
    *finally_count = handler == NULL ? 0U : handler->finally_count;
    *pending = r_mir_pending_completion(
        R_MIR_PENDING_COMPLETION_CHECKED_ERROR, error_type, payload, target);
    (void)memset(instruction, 0, sizeof(*instruction));
    instruction->kind = R_MIR_INSTRUCTION_THROW;
    instruction->span = span;
    instruction->type = error_type;
    instruction->auxiliary_type = build->function_effects;
    instruction->operand0 = payload;
    instruction->integer_value = tag;
    instruction->target0 = target;
    return true;
}

static bool r_mir_discard_pending_aggregate_values(RMirBuildContext *build, RSourceSpan span) {
    size_t index;

    for (index = build->pending_aggregate_values.count; index != 0U; --index) {
        const RMirValueId value = build->pending_aggregate_values.items[index - 1U];
        const RMirInstruction *definition = r_mir_temporary_value_definition(build, value);
        RMirInstruction instruction;

        if (definition == NULL) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_DISCARD;
        instruction.span = span;
        instruction.type = definition->type;
        instruction.operand0 = value;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
    }
    return true;
}

/* L25.4: the error exits of one carrier that share one path: the errors the function does not
   catch, or the members of one family that the same catch receives, when their cleanups agree. The
   shared path keeps the error in the carrier and relays it once, at its catch or at the caller. */
typedef struct RMirEffectGroup {
    size_t handler; /* SIZE_MAX: the caller receives the error. */
    RHirNodeId cleanup_block;
    uint32_t member_count;
    size_t block;
} RMirEffectGroup;

#define R_MIR_EFFECT_GROUP_LIMIT 8U
#define R_MIR_EFFECT_GROUP_MEMBER_LIMIT 64U

static uint32_t r_mir_group_effect_exits(const RMirBuildContext *build,
                                         RTypeId effects,
                                         const RHirNode *effect_source,
                                         uint32_t effect_count,
                                         uint8_t *member_groups,
                                         RMirEffectGroup *groups) {
    uint32_t group_count = 0U;
    uint32_t effect_index;

    (void)memset(member_groups, 0, R_MIR_EFFECT_GROUP_MEMBER_LIMIT);
    if (effect_count > R_MIR_EFFECT_GROUP_MEMBER_LIMIT) {
        return 0U;
    }
    for (effect_index = 0U; effect_index < effect_count; ++effect_index) {
        const RTypeId error_type = r_semantic_effect_at(build->frontend, effects, effect_index);
        const RHirEffectExit *effect_exit =
            &build->frontend->hir_effect_exits[(size_t)effect_source->first_effect_exit +
                                               (size_t)effect_index];
        const RMirEffectHandler *handler = r_mir_find_effect_handler(build, error_type);
        const size_t handler_index =
            handler == NULL ? SIZE_MAX : (size_t)(handler - build->effect_handlers);
        uint32_t group;

        if ((handler != NULL)
                ? ((handler->error_type == error_type) ||
                   (r_semantic_error_family_tag(build->frontend, handler->error_type, error_type) ==
                    UINT32_MAX))
                : (r_mir_effect_tag(build->frontend, build->function_effects, error_type) ==
                   UINT64_C(0))) {
            continue;
        }
        for (group = 0U; group < group_count; ++group) {
            if ((groups[group].handler == handler_index) &&
                r_hir_same_tree(
                    build->frontend, groups[group].cleanup_block, effect_exit->cleanup_block)) {
                break;
            }
        }
        if (group == group_count) {
            if (group_count == R_MIR_EFFECT_GROUP_LIMIT) {
                continue;
            }
            groups[group].handler = handler_index;
            groups[group].cleanup_block = effect_exit->cleanup_block;
            groups[group].member_count = 0U;
            groups[group].block = SIZE_MAX;
            group_count += 1U;
        }
        groups[group].member_count += 1U;
        member_groups[effect_index] = (uint8_t)(group + 1U);
    }
    for (effect_index = 0U; effect_index < effect_count; ++effect_index) {
        if ((member_groups[effect_index] != 0U) &&
            (groups[member_groups[effect_index] - 1U].member_count < 2U)) {
            member_groups[effect_index] = 0U;
        }
    }
    return group_count;
}

static bool r_mir_append_tag_test(RMirBuildContext *build,
                                  RSourceSpan span,
                                  RTypeId u32_type,
                                  RTypeId bool_type,
                                  RMirValueId tag,
                                  uint64_t expected,
                                  RTokenKind operation,
                                  size_t true_block,
                                  size_t false_block) {
    RMirInstruction instruction;
    RMirValueId expected_tag;
    RMirValueId matches;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
    instruction.span = span;
    instruction.type = u32_type;
    instruction.integer_value = expected;
    if (!r_mir_new_value(build, &expected_tag)) {
        return false;
    }
    instruction.result = expected_tag;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_BINARY;
    instruction.span = span;
    instruction.type = bool_type;
    instruction.operation = operation;
    instruction.operand0 = tag;
    instruction.operand1 = expected_tag;
    if (!r_mir_new_value(build, &matches)) {
        return false;
    }
    instruction.result = matches;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_BRANCH;
    instruction.span = span;
    instruction.operand0 = matches;
    instruction.target0 = (RMirBlockId)(true_block + 1U);
    instruction.target1 = (RMirBlockId)(false_block + 1U);
    return r_mir_append_instruction(build, instruction);
}

/* The shared path of one group: the carrier is the pending payload through the cleanups and
   finally bodies, and the throw relays its error to the catch or to the caller's carrier. */
static bool r_mir_lower_effect_group(RMirBuildContext *build,
                                     RSourceSpan span,
                                     RTypeId carrier_type_id,
                                     RMirValueId carrier,
                                     const RMirEffectGroup *group) {
    const RMirEffectHandler *handler =
        group->handler == SIZE_MAX ? NULL : &build->effect_handlers[group->handler];
    const RMirBlockId target =
        handler == NULL ? R_MIR_BLOCK_ID_INVALID : (RMirBlockId)(handler->block + 1U);
    const RMirPendingCompletion pending = r_mir_pending_completion(
        R_MIR_PENDING_COMPLETION_CHECKED_ERROR, carrier_type_id, carrier, target);
    RMirInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_THROW;
    instruction.span = span;
    instruction.type = handler == NULL ? carrier_type_id : handler->error_type;
    instruction.auxiliary_type = build->function_effects;
    instruction.runtime_type = carrier_type_id;
    instruction.operand0 = carrier;
    instruction.target0 = target;
    return r_mir_discard_pending_aggregate_values(build, span) &&
           r_mir_lower_pending_cleanup_segments(
               build,
               r_mir_hir_node(build->frontend, group->cleanup_block),
               UINT32_C(0),
               handler == NULL ? 0U : handler->finally_count,
               span,
               &pending,
               UINT32_C(1)) &&
           r_mir_append_instruction(build, instruction);
}

static bool r_mir_dispatch_effect_carrier(RMirBuildContext *build,
                                          RSourceSpan span,
                                          RTypeId carrier_type_id,
                                          RMirValueId carrier,
                                          const RHirNode *effect_source,
                                          RMirExpressionResult *result) {
    const RSemanticType *carrier_type = r_semantic_type(build->frontend, carrier_type_id);
    const RSemanticType *success_type;
    const uint32_t effect_count =
        carrier_type == NULL ? UINT32_C(0)
                             : r_semantic_effect_count(build->frontend, carrier_type->second);
    uint8_t member_groups[R_MIR_EFFECT_GROUP_MEMBER_LIMIT];
    RMirEffectGroup groups[R_MIR_EFFECT_GROUP_LIMIT];
    uint32_t group_count;
    uint32_t last_group = UINT32_MAX;
    uint32_t group_index;
    RTypeId u32_type;
    RTypeId bool_type;
    RMirInstruction instruction;
    RMirValueId tag;
    uint32_t effect_index;

    if ((carrier_type == NULL) || (carrier_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
        (effect_count == UINT32_C(0)) || (effect_source == NULL) ||
        (effect_source->effect_exit_count != effect_count) ||
        ((size_t)effect_source->first_effect_exit > build->frontend->hir_effect_exit_count) ||
        ((size_t)effect_count >
         (build->frontend->hir_effect_exit_count - (size_t)effect_source->first_effect_exit)) ||
        !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_U32, &u32_type) ||
        !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_BOOL, &bool_type)) {
        return false;
    }
    success_type = r_semantic_type(build->frontend, carrier_type->base);
    if (success_type == NULL) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_EFFECT_TAG;
    instruction.span = span;
    instruction.type = u32_type;
    instruction.auxiliary_type = carrier_type_id;
    instruction.operand0 = carrier;
    if (!r_mir_new_value(build, &tag)) {
        return false;
    }
    instruction.result = tag;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    group_count = r_mir_group_effect_exits(
        build, carrier_type->second, effect_source, effect_count, member_groups, groups);
    for (group_index = 0U; group_index < group_count; ++group_index) {
        /* The largest group is tested last by a nonzero tag; the others by their members. */
        if ((groups[group_index].member_count >= 2U) &&
            ((last_group == UINT32_MAX) ||
             (groups[group_index].member_count > groups[last_group].member_count))) {
            last_group = group_index;
        }
    }
    for (effect_index = UINT32_C(0); effect_index < effect_count; ++effect_index) {
        const RTypeId error_type =
            r_semantic_effect_at(build->frontend, carrier_type->second, effect_index);
        const RHirEffectExit *effect_exit =
            &build->frontend->hir_effect_exits[(size_t)effect_source->first_effect_exit +
                                               (size_t)effect_index];
        const RHirNode *cleanup_block = r_mir_hir_node(build->frontend, effect_exit->cleanup_block);
        const uint64_t error_tag = (uint64_t)effect_index + UINT64_C(1);
        const uint32_t member_group = effect_index < R_MIR_EFFECT_GROUP_MEMBER_LIMIT
                                          ? (uint32_t)member_groups[effect_index]
                                          : 0U;
        size_t error_block;
        size_t next_block;

        if ((error_type == R_TYPE_ID_INVALID) || (effect_exit->error_type != error_type) ||
            (cleanup_block == NULL) || (cleanup_block->kind != R_HIR_BLOCK)) {
            return false;
        }
        if ((member_group != 0U) && ((member_group - 1U) == last_group)) {
            continue;
        }
        if ((member_group != 0U) && (groups[member_group - 1U].block == SIZE_MAX) &&
            !r_mir_add_block(build, &groups[member_group - 1U].block)) {
            return false;
        }
        if (member_group != 0U) {
            if (!r_mir_add_block(build, &next_block) ||
                !r_mir_append_tag_test(build,
                                       span,
                                       u32_type,
                                       bool_type,
                                       tag,
                                       error_tag,
                                       R_TOKEN_EQUAL_EQUAL,
                                       groups[member_group - 1U].block,
                                       next_block)) {
                return false;
            }
            build->current_block = next_block;
            continue;
        }
        if (!r_mir_add_block(build, &error_block) || !r_mir_add_block(build, &next_block) ||
            !r_mir_append_tag_test(build,
                                   span,
                                   u32_type,
                                   bool_type,
                                   tag,
                                   error_tag,
                                   R_TOKEN_EQUAL_EQUAL,
                                   error_block,
                                   next_block)) {
            return false;
        }
        build->current_block = error_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_EFFECT_PAYLOAD;
        instruction.span = span;
        instruction.type = error_type;
        instruction.auxiliary_type = carrier_type_id;
        instruction.operand0 = carrier;
        instruction.integer_value = error_tag;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        {
            RMirInstruction throw_instruction;
            RMirPendingCompletion pending;
            size_t finally_count;

            if (!r_mir_prepare_throw_value(build,
                                           span,
                                           error_type,
                                           instruction.result,
                                           &throw_instruction,
                                           &pending,
                                           &finally_count) ||
                !r_mir_discard_pending_aggregate_values(build, span) ||
                !r_mir_lower_pending_cleanup_segments(build,
                                                      cleanup_block,
                                                      UINT32_C(0),
                                                      finally_count,
                                                      span,
                                                      &pending,
                                                      UINT32_C(1)) ||
                !r_mir_append_instruction(build, throw_instruction)) {
                return false;
            }
        }
        build->current_block = next_block;
    }
    if (last_group != UINT32_MAX) {
        size_t next_block;

        if (!r_mir_add_block(build, &groups[last_group].block) ||
            !r_mir_add_block(build, &next_block) ||
            !r_mir_append_tag_test(build,
                                   span,
                                   u32_type,
                                   bool_type,
                                   tag,
                                   UINT64_C(0),
                                   R_TOKEN_BANG_EQUAL,
                                   groups[last_group].block,
                                   next_block)) {
            return false;
        }
        build->current_block = next_block;
    }
    {
        const size_t success_block = build->current_block;

        for (group_index = 0U; group_index < group_count; ++group_index) {
            if (groups[group_index].block == SIZE_MAX) {
                continue;
            }
            build->current_block = groups[group_index].block;
            if (!r_mir_lower_effect_group(
                    build, span, carrier_type_id, carrier, &groups[group_index])) {
                return false;
            }
        }
        build->current_block = success_block;
    }
    (void)memset(result, 0, sizeof(*result));
    if (success_type->kind == R_SEMANTIC_TYPE_VOID) {
        result->value = R_MIR_VALUE_ID_INVALID;
        return true;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_EFFECT_PAYLOAD;
    instruction.span = span;
    instruction.type = carrier_type->base;
    instruction.auxiliary_type = carrier_type_id;
    instruction.operand0 = carrier;
    instruction.integer_value = UINT64_C(0);
    if (!r_mir_new_value(build, &instruction.result) ||
        !r_mir_append_instruction(build, instruction)) {
        return false;
    }
    result->value = instruction.result;
    return true;
}

static bool r_mir_lower_call(RMirBuildContext *build,
                             const RHirNode *node,
                             uint32_t depth,
                             RMirExpressionResult *result) {
    RMirValueVector arguments = {0};
    RMirInstruction instruction;
    RMirCallBorrowMask call_borrow_mask = {0};
    uint32_t child_index;
    bool success = false;

    if ((node->kind == R_HIR_CALL) &&
        !r_mir_build_call_borrow_mask(build, node, &call_borrow_mask)) {
        goto cleanup;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        RMirExpressionResult argument;
        const RHirNode *argument_node =
            r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, child_index));
        if (!r_mir_lower_expression(build,
                                    r_mir_hir_child(build->frontend, node, child_index),
                                    depth + 1U,
                                    &argument) ||
            argument.is_place) {
            goto cleanup;
        }
        if ((argument.value == R_MIR_VALUE_ID_INVALID) && (argument_node != NULL) &&
            r_mir_type_is_never(build->frontend, argument_node->type)) {
            /* R-TYPE-0007: a never argument ends the call before it starts. */
            success = r_mir_lower_never_operand_completion(build, node, result);
            goto cleanup;
        }
        if ((argument.value == R_MIR_VALUE_ID_INVALID) ||
            !r_mir_value_vector_push(build, &arguments, argument.value)) {
            goto cleanup;
        }
    }
    if (node->kind == R_HIR_ASYNC_START) {
        for (child_index = 0U; child_index < (uint32_t)arguments.count; ++child_index) {
            RMirInstruction *definition =
                r_mir_temporary_value_definition(build, arguments.items[child_index]);

            if ((definition != NULL) && (definition->kind == R_MIR_INSTRUCTION_MOVE)) {
                definition->is_async_staged_move = true;
            }
        }
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = node->kind == R_HIR_ASYNC_START     ? R_MIR_INSTRUCTION_ASYNC_START
                       : node->kind == R_HIR_INDIRECT_CALL ? R_MIR_INSTRUCTION_INDIRECT_CALL
                                                           : R_MIR_INSTRUCTION_CALL;
    instruction.span = node->span;
    if (node->auxiliary_type != R_TYPE_ID_INVALID) {
        const RSemanticType *carrier = r_semantic_type(build->frontend, node->auxiliary_type);
        const RSemanticType *call_type = r_semantic_type(build->frontend, node->type);
        const RSemanticType *success_type =
            carrier == NULL ? NULL : r_semantic_type(build->frontend, carrier->base);

        /* R-FUNC-0003: the carrier of a never function has no success value. */
        if ((carrier == NULL) || (carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (call_type == NULL) || (success_type == NULL) ||
            ((carrier->base != node->type) && ((call_type->kind != R_SEMANTIC_TYPE_NEVER) ||
                                               (success_type->kind != R_SEMANTIC_TYPE_VOID)))) {
            goto cleanup;
        }
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
    } else {
        instruction.type = node->type;
        instruction.auxiliary_type = R_TYPE_ID_INVALID;
    }
    instruction.symbol = node->symbol;
    instruction.task_scope = node->task_scope;
    instruction.immediate_await = (node->kind == R_HIR_ASYNC_START) && node->immediate_await;
    instruction.borrow_origin = node->borrow_origin;
    instruction.borrow_origin_set = node->borrow_origin_set;
    instruction.borrow_origin_multiple = node->borrow_origin_multiple;
    instruction.operand_count = (uint32_t)arguments.count;
    instruction.call_borrow_mask = call_borrow_mask;
    if (!r_mir_append_operands(build, &arguments, &instruction.first_operand)) {
        goto cleanup;
    }
    {
        const RSemanticType *type = r_semantic_type(build->frontend, instruction.type);
        if ((type == NULL) ||
            ((type->kind != R_SEMANTIC_TYPE_VOID) && (type->kind != R_SEMANTIC_TYPE_NEVER) &&
             !r_mir_new_value(build, &instruction.result))) {
            goto cleanup;
        }
    }
    if (!r_mir_append_instruction(build, instruction)) {
        goto cleanup;
    }
    if (node->auxiliary_type != R_TYPE_ID_INVALID) {
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
    } else {
        (void)memset(result, 0, sizeof(*result));
        result->value = instruction.result;
    }
    {
        const RSemanticType *call_type = r_semantic_type(build->frontend, node->type);

        /* R-FUNC-0003: a never call does not complete; its success path is unreachable. */
        if ((call_type != NULL) && (call_type->kind == R_SEMANTIC_TYPE_NEVER) &&
            !build->blocks[build->current_block].terminated) {
            RMirInstruction unreachable;

            (void)memset(&unreachable, 0, sizeof(unreachable));
            unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
            unreachable.span = node->span;
            if (!r_mir_append_instruction(build, unreachable)) {
                goto cleanup;
            }
            (void)memset(result, 0, sizeof(*result));
        }
    }
    success = true;

cleanup:
    r_context_free(build->frontend, arguments.items);
    return success;
}

static bool r_mir_lower_await(RMirBuildContext *build,
                              const RHirNode *node,
                              uint32_t depth,
                              RMirExpressionResult *result) {
    const RSemanticType *task_type = r_semantic_type(build->frontend, node->auxiliary_type);
    const RSemanticType *result_type = r_semantic_type(build->frontend, node->type);
    /* Lowering may intern types and move the type table: keep kinds and ids, not pointers. */
    const RSemanticTypeKind result_kind =
        result_type == NULL ? R_SEMANTIC_TYPE_INVALID : result_type->kind;
    const RTypeId task_errors = task_type == NULL ? R_TYPE_ID_INVALID : task_type->second;
    RMirExpressionResult place;
    RMirInstruction instruction;
    RTypeId carrier_type = R_TYPE_ID_INVALID;
    size_t await_block;
    size_t resume_block;
    size_t cancel_block;

    if ((node->child_count != UINT32_C(1)) || !node->is_move ||
        (node->operation != R_TOKEN_KW_AWAIT) || (task_type == NULL) ||
        (task_type->kind != R_SEMANTIC_TYPE_TASK) || (task_type->base != node->type) ||
        (result_type == NULL) ||
        !r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
        !place.is_place || (place.place_symbol != node->symbol) ||
        !r_mir_add_block(build, &await_block) || !r_mir_add_block(build, &resume_block) ||
        !r_mir_add_block(build, &cancel_block) ||
        !r_mir_append_jump(build, node->span, await_block)) {
        return false;
    }
    if ((task_errors != R_TYPE_ID_INVALID) &&
        !r_semantic_effect_carrier_type(build->frontend, node->type, task_errors, &carrier_type)) {
        return false;
    }
    build->current_block = await_block;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_AWAIT;
    instruction.span = node->span;
    instruction.type = carrier_type == R_TYPE_ID_INVALID ? node->type : carrier_type;
    instruction.auxiliary_type = node->auxiliary_type;
    instruction.symbol = node->symbol;
    instruction.operation = R_TOKEN_KW_AWAIT;
    instruction.place_ordinal = place.place_ordinal;
    instruction.place_is_parameter = place.place_is_parameter;
    instruction.operand1 = place.place_projection;
    instruction.target0 = (RMirBlockId)(resume_block + 1U);
    instruction.target1 = (RMirBlockId)(cancel_block + 1U);
    /* A task that never completes normally yields no value, like task<void> (R-FUNC-0003). */
    if (!((carrier_type == R_TYPE_ID_INVALID) &&
          ((result_kind == R_SEMANTIC_TYPE_VOID) || (result_kind == R_SEMANTIC_TYPE_NEVER))) &&
        !r_mir_new_value(build, &instruction.result)) {
        return false;
    }
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    build->current_block = cancel_block;
    {
        const RMirPendingCompletion pending =
            r_mir_pending_completion(R_MIR_PENDING_COMPLETION_CANCEL,
                                     R_TYPE_ID_INVALID,
                                     R_MIR_VALUE_ID_INVALID,
                                     R_MIR_BLOCK_ID_INVALID);

        if (!r_mir_lower_active_finalies(build, 0U, node->span, &pending) ||
            !r_mir_append_cancel(build, node->span)) {
            return false;
        }
    }
    build->current_block = resume_block;
    if (carrier_type != R_TYPE_ID_INVALID) {
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, carrier_type, instruction.result, node, result)) {
            return false;
        }
    } else {
        (void)memset(result, 0, sizeof(*result));
        result->value = instruction.result;
    }
    /* R-FUNC-0003: a task that never completes normally has an unreachable success path. */
    if ((result_kind == R_SEMANTIC_TYPE_NEVER) && !build->blocks[build->current_block].terminated) {
        RMirInstruction unreachable;

        (void)memset(&unreachable, 0, sizeof(unreachable));
        unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
        unreachable.span = node->span;
        if (!r_mir_append_instruction(build, unreachable)) {
            return false;
        }
        (void)memset(result, 0, sizeof(*result));
    }
    return true;
}

#include "task_scope.inc"

static bool r_mir_lower_standard_call(RMirBuildContext *build,
                                      const RHirNode *node,
                                      uint32_t depth,
                                      RMirExpressionResult *result) {
    const RSemanticType *result_type = r_semantic_type(build->frontend, node->type);
    const RStandardMathOperationDescriptor *math_operation =
        node->standard_operation == R_STANDARD_CALL_MATH_OPERATION
            ? r_standard_math_operation(node->integer_value)
            : NULL;
    const RStandardErrorErasureDescriptor *error_erasure =
        node->standard_operation == R_STANDARD_CALL_ERROR_ERASURE
            ? r_standard_error_erasure(node->integer_value)
            : NULL;
    const bool has_checked_effect =
        (node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER) ||
        (node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_APPEND) ||
        ((node->standard_operation == R_STANDARD_CALL_CORE_CLONE) &&
         (r_semantic_type(build->frontend, node->auxiliary_type) != NULL) &&
         (r_semantic_type(build->frontend, node->auxiliary_type)->kind ==
          R_SEMANTIC_TYPE_EFFECT_CARRIER)) ||
        (node->standard_operation == R_STANDARD_CALL_ALLOC_TRY_NEW) ||
        (node->standard_operation == R_STANDARD_CALL_ALLOC_BYTES) ||
        (node->standard_operation == R_STANDARD_CALL_MATH_SIN_F64) ||
        ((node->standard_operation >= R_STANDARD_CALL_ENV_ARGUMENTS) &&
         (node->standard_operation <= R_STANDARD_CALL_ENV_REMOVE)) ||
        ((math_operation != NULL) && math_operation->checked) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_FROM_PARTS) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_ADD) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_SUB) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_MULTIPLY) ||
        ((node->standard_operation >= R_STANDARD_CALL_STRING_WITH_CAPACITY) &&
         (node->standard_operation <= R_STANDARD_CALL_STRING_TRUNCATE)) ||
        (node->standard_operation == R_STANDARD_CALL_STRING_FROM_STR) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_BARRIER_NEW) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_CHANNEL) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_SYNC_CHANNEL) ||
        (node->standard_operation == R_STANDARD_CALL_FORMAT_WITH_CAPACITY) ||
        ((node->standard_operation >= R_STANDARD_CALL_FORMAT_APPEND_STR) &&
         (node->standard_operation <= R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE)) ||
        (node->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_INTEGER) ||
        ((node->standard_operation >= R_STANDARD_CALL_TIME_MONOTONIC_NOW) &&
         (node->standard_operation <= R_STANDARD_CALL_TIME_FROM_UTC)) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_FOR) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_UNTIL) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_RECEIVE) ||
        (node->standard_operation == R_STANDARD_CALL_ARRAY_RESERVE) ||
        (node->standard_operation == R_STANDARD_CALL_ARRAY_PUSH) ||
        (node->standard_operation == R_STANDARD_CALL_ARRAY_WITH_CAPACITY) ||
        (node->standard_operation == R_STANDARD_CALL_ARRAY_FILLED) ||
        ((node->standard_operation >= R_STANDARD_CALL_LIST_PUSH_FRONT) &&
         (node->standard_operation <= R_STANDARD_CALL_LIST_INSERT_AFTER)) ||
        (node->standard_operation == R_STANDARD_CALL_DICT_WITH_CAPACITY) ||
        (node->standard_operation == R_STANDARD_CALL_DICT_RESERVE) ||
        (node->standard_operation == R_STANDARD_CALL_DICT_INSERT) ||
        (node->standard_operation == R_STANDARD_CALL_BYTES_WITH_CAPACITY) ||
        (node->standard_operation == R_STANDARD_CALL_BYTES_APPEND) ||
        ((node->standard_operation >= R_STANDARD_CALL_BYTES_APPEND_U8) &&
         (node->standard_operation <= R_STANDARD_CALL_BYTES_APPEND_U64_LE)) ||
        (node->standard_operation == R_STANDARD_CALL_BYTES_COPY) ||
        (node->standard_operation == R_STANDARD_CALL_BYTES_COPY_WITHIN) ||
        (node->standard_operation == R_STANDARD_CALL_UTF8_VALIDATE) ||
        (node->standard_operation == R_STANDARD_CALL_CORE_RECURSION_ENTER) ||
        (node->standard_operation == R_STANDARD_CALL_BITS_READ) ||
        (node->standard_operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8) ||
        (node->standard_operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8_BYTES) ||
        (node->standard_operation == R_STANDARD_CALL_FS_PATH_CLONE) ||
        (node->standard_operation == R_STANDARD_CALL_FS_PATH_TO_UTF8) ||
        (node->standard_operation == R_STANDARD_CALL_FS_PATH_JOIN) ||
        ((node->standard_operation >= R_STANDARD_CALL_FS_READ_FILE) &&
         (node->standard_operation <= R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH)) ||
        (node->standard_operation == R_STANDARD_CALL_FS_FILE_METADATA) ||
        (r_standard_fs_async_descriptor(node->standard_operation) != NULL) ||
        ((node->standard_operation >= R_STANDARD_CALL_IO_READ) &&
         (node->standard_operation <= R_STANDARD_CALL_IO_WRITE_SHARED)) ||
        (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
        (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
        (node->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING) ||
        ((node->standard_operation == R_STANDARD_CALL_THREAD_JOIN) &&
         (node->integer_value != UINT64_C(0))) ||
        (((node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE) ||
          (node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE) ||
          (node->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT)) &&
         (node->integer_value != UINT64_C(0))) ||
        (node->standard_operation == R_STANDARD_CALL_CONVERT_PARSE) ||
        (node->standard_operation == R_STANDARD_CALL_NET_PARSE_IP) ||
        (node->standard_operation == R_STANDARD_CALL_NET_FORMAT_IP) ||
        (node->standard_operation == R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS) ||
        (r_standard_net_operation(node->standard_operation) != NULL) ||
        (r_standard_scoped_operation(node->standard_operation) != NULL) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_COMMAND_CREATE) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_ARG) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_ENVIRONMENT) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_SPAWN) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_WAIT) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_TERMINATE) ||
        (node->standard_operation == R_STANDARD_CALL_SIGNAL_LISTEN) ||
        (node->standard_operation == R_STANDARD_CALL_SIGNAL_RAISE) ||
        (node->standard_operation == R_STANDARD_CALL_SIGNAL_NEXT) ||
        (node->standard_operation == R_STANDARD_CALL_C_STRING_FROM_STR) ||
        (node->standard_operation == R_STANDARD_CALL_C_VALIDATE_UTF8) ||
        (node->standard_operation == R_STANDARD_CALL_C_COPY_UTF8) ||
        (node->standard_operation == R_STANDARD_CALL_C_ATTACH_THREAD) ||
        (node->standard_operation == R_STANDARD_CALL_SECRET_WITH_LENGTH) ||
        (node->standard_operation == R_STANDARD_CALL_ERROR_DIAGNOSTIC) ||
        (node->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED) ||
        (node->standard_operation == R_STANDARD_CALL_C_CHECKED) ||
        /* L30: allocating calls and task starts carry their effects. */
        (r_async_sync_operation(node->standard_operation) &&
         (r_semantic_type(build->frontend, node->auxiliary_type) != NULL) &&
         (r_semantic_type(build->frontend, node->auxiliary_type)->kind ==
          R_SEMANTIC_TYPE_EFFECT_CARRIER));
    const RSemanticType *effect_carrier =
        has_checked_effect ? r_semantic_type(build->frontend, node->auxiliary_type) : NULL;
    RMirValueVector arguments = {0};
    RMirInstruction instruction;
    uint32_t child_index;
    uint32_t expected_count;
    bool success = false;

    if ((result_type == NULL) || (node->standard_operation == R_STANDARD_CALL_INVALID) ||
        ((node->standard_operation == R_STANDARD_CALL_MATH_OPERATION) &&
         (math_operation == NULL)) ||
        ((node->standard_operation == R_STANDARD_CALL_ERROR_ERASURE) && (error_erasure == NULL)) ||
        (node->auxiliary_type == R_TYPE_ID_INVALID)) {
        return false;
    }
    if (has_checked_effect &&
        ((effect_carrier == NULL) || (effect_carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
         (effect_carrier->base != node->type))) {
        return false;
    }
    if (node->standard_operation == R_STANDARD_CALL_CORE_PANIC) {
        /* R-ERR-0004: the message is the only operand; a never call produces no value. */
        RMirExpressionResult message;

        if ((node->child_count != UINT32_C(1)) || (result_type->kind != R_SEMANTIC_TYPE_NEVER) ||
            !r_mir_lower_expression(build,
                                    r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                    depth + UINT32_C(1),
                                    &message) ||
            message.is_place || (message.value == R_MIR_VALUE_ID_INVALID) ||
            !r_mir_value_vector_push(build, &arguments, message.value)) {
            goto cleanup;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.auxiliary_type = node->auxiliary_type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = UINT32_C(1);
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        (void)memset(result, 0, sizeof(*result));
        result->value = R_MIR_VALUE_ID_INVALID;
        success = true;
        goto cleanup;
    }
    if ((node->standard_operation == R_STANDARD_CALL_ARC_CLONE) ||
        (node->standard_operation == R_STANDARD_CALL_RC_CLONE)) {
        const RHirNode *source;
        const RSemanticType *borrow_type;
        RMirExpressionResult source_result;
        const RSemanticTypeKind owner_kind = node->standard_operation == R_STANDARD_CALL_ARC_CLONE
                                                 ? R_SEMANTIC_TYPE_ARC
                                                 : R_SEMANTIC_TYPE_RC;

        if ((node->child_count != UINT32_C(1)) || (result_type->kind != owner_kind) ||
            (result_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
            (result_type->base != node->auxiliary_type)) {
            return false;
        }
        source =
            r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
        borrow_type = source == NULL ? NULL : r_semantic_type(build->frontend, source->type);
        if ((source == NULL) || (borrow_type == NULL) ||
            (borrow_type->kind != R_SEMANTIC_TYPE_BORROW) ||
            (borrow_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED) ||
            (borrow_type->base != node->type)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.auxiliary_type = node->auxiliary_type;
        instruction.borrow_origin = source->borrow_origin;
        instruction.borrow_origin_set = source->borrow_origin_set;
        instruction.borrow_origin_multiple = source->borrow_origin_multiple;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        if (source->kind == R_HIR_BORROW) {
            if ((source->child_count != UINT32_C(1)) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, source, UINT32_C(0)),
                                        depth + UINT32_C(1),
                                        &source_result) ||
                !source_result.is_place) {
                return false;
            }
            instruction.symbol = source_result.place_symbol;
            instruction.operand0 = source_result.place_projection;
            instruction.place_ordinal = source_result.place_ordinal;
            instruction.place_is_parameter = source_result.place_is_parameter;
        } else {
            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                        depth + UINT32_C(1),
                                        &source_result) ||
                source_result.is_place || (source_result.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, source_result.value) ||
                !r_mir_append_operands(build, &arguments, &instruction.first_operand)) {
                r_context_free(build->frontend, arguments.items);
                return false;
            }
            instruction.operand_count = UINT32_C(1);
        }
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            r_context_free(build->frontend, arguments.items);
            return false;
        }
        r_context_free(build->frontend, arguments.items);
        (void)memset(result, 0, sizeof(*result));
        result->value = instruction.result;
        return true;
    }
    if ((node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_FOR) ||
        (node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_UNTIL)) {
        const bool is_for = node->standard_operation == R_STANDARD_CALL_TIME_SLEEP_FOR;
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);
        const RHirNode *argument_node =
            node->child_count == UINT32_C(1)
                ? r_mir_hir_node(build->frontend,
                                 r_mir_hir_child(build->frontend, node, UINT32_C(0)))
                : NULL;
        const RSemanticType *argument_type =
            argument_node == NULL ? NULL : r_semantic_type(build->frontend, argument_node->type);
        RMirExpressionResult argument;

        if ((node->child_count != UINT32_C(1)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, task_type->second, "std.time::time_error") ||
            (logical_type == NULL) || (logical_type->kind != R_SEMANTIC_TYPE_VOID) ||
            !r_mir_standard_type_is_exact(build->frontend,
                                          argument_type,
                                          is_for ? "std.time::duration" : "std.time::instant") ||
            !r_mir_lower_expression(build,
                                    r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                    depth + UINT32_C(1),
                                    &argument) ||
            argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
            !r_mir_value_vector_push(build, &arguments, argument.value)) {
            goto cleanup;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = UINT32_C(1);
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction) ||
            !r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if (node->standard_operation == R_STANDARD_CALL_IO_FLUSH) {
        const RHirNode *stream;
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);
        RMirExpressionResult stream_place;
        RMirExpressionResult deadline;
        bool stream_is_place;

        if ((node->child_count != UINT32_C(2)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, task_type->second, "std.io::io_error") ||
            (logical_type == NULL) || (logical_type->kind != R_SEMANTIC_TYPE_VOID)) {
            return false;
        }
        stream =
            r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
        stream_is_place = (stream != NULL) && (stream->kind == R_HIR_BORROW);
        if ((stream == NULL) ||
            (stream_is_place
                 ? ((stream->child_count != UINT32_C(1)) ||
                    !r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, stream, UINT32_C(0)),
                                            depth + UINT32_C(1),
                                            &stream_place) ||
                    !stream_place.is_place)
                 : (!r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                            depth + UINT32_C(1),
                                            &stream_place) ||
                    stream_place.is_place || (stream_place.value == R_MIR_VALUE_ID_INVALID) ||
                    !r_mir_value_vector_push(build, &arguments, stream_place.value))) ||
            !r_mir_lower_expression(build,
                                    r_mir_hir_child(build->frontend, node, UINT32_C(1)),
                                    depth + UINT32_C(1),
                                    &deadline) ||
            deadline.is_place || (deadline.value == R_MIR_VALUE_ID_INVALID) ||
            !r_mir_value_vector_push(build, &arguments, deadline.value)) {
            goto cleanup;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.borrow_origin = stream->borrow_origin;
        instruction.borrow_origin_set = stream->borrow_origin_set;
        instruction.borrow_origin_multiple = stream->borrow_origin_multiple;
        if (stream_is_place) {
            instruction.symbol = stream_place.place_symbol;
            instruction.operand0 = stream_place.place_projection;
            instruction.place_ordinal = stream_place.place_ordinal;
            instruction.place_is_parameter = stream_place.place_is_parameter;
        } else {
            instruction.call_borrow_mask.low = UINT64_C(1);
        }
        instruction.operand_count = (uint32_t)arguments.count;
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if ((node->standard_operation == R_STANDARD_CALL_IO_CLOSE_INPUT) ||
        (node->standard_operation == R_STANDARD_CALL_IO_CLOSE_OUTPUT)) {
        const bool is_input = node->standard_operation == R_STANDARD_CALL_IO_CLOSE_INPUT;
        const char *stream_type_name = is_input ? "std.io::input" : "std.io::output";
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);

        if ((node->child_count != UINT32_C(2)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            (task_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (task_type->length != UINT64_C(0)) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, task_type->second, "std.io::io_error") ||
            (logical_type == NULL) || (logical_type->kind != R_SEMANTIC_TYPE_VOID)) {
            return false;
        }
        for (child_index = UINT32_C(0); child_index < UINT32_C(2); ++child_index) {
            const RHirNode *argument_node = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, child_index));
            const RSemanticType *argument_type =
                argument_node == NULL ? NULL
                                      : r_semantic_type(build->frontend, argument_node->type);
            const RSemanticType *nested_type =
                (child_index == UINT32_C(1)) && (argument_type != NULL)
                    ? r_semantic_type(build->frontend, argument_type->base)
                    : NULL;
            RMirExpressionResult argument;

            if ((argument_node == NULL) || (argument_type == NULL) ||
                ((child_index == UINT32_C(0)) &&
                 ((argument_node->kind != R_HIR_MOVE) ||
                  !r_mir_standard_type_is_exact(
                      build->frontend, argument_type, stream_type_name))) ||
                ((child_index == UINT32_C(1)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_OPTION) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  !r_mir_standard_type_is_exact(
                      build->frontend, nested_type, "std.time::instant"))) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
            if (child_index == UINT32_C(0)) {
                RMirInstruction *definition =
                    r_mir_temporary_value_definition(build, argument.value);

                if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_MOVE)) {
                    goto cleanup;
                }
                definition->is_async_staged_move = true;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = (uint32_t)arguments.count;
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if (node->standard_operation == R_STANDARD_CALL_IO_WRITE_SHARED) {
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);

        if ((node->child_count != UINT32_C(5)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            (task_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (task_type->length != UINT64_C(0)) ||
            (task_type->second != R_TYPE_ID_INVALID) ||
            !r_mir_standard_type_is_exact(
                build->frontend, logical_type, "std.io::shared_write_result")) {
            return false;
        }
        for (child_index = UINT32_C(0); child_index < UINT32_C(5); ++child_index) {
            const RHirNode *argument_node = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, child_index));
            const RSemanticType *argument_type =
                argument_node == NULL ? NULL
                                      : r_semantic_type(build->frontend, argument_node->type);
            const RSemanticType *nested_type =
                argument_type == NULL ? NULL
                                      : r_semantic_type(build->frontend, argument_type->base);
            const RSemanticType *element_type =
                ((child_index == UINT32_C(1)) && (nested_type != NULL))
                    ? r_semantic_type(build->frontend, nested_type->base)
                    : NULL;
            RMirExpressionResult argument;

            if ((argument_node == NULL) || (argument_type == NULL) ||
                ((child_index == UINT32_C(0)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  !r_mir_standard_type_is_exact(build->frontend, nested_type, "std.io::output"))) ||
                ((child_index == UINT32_C(1)) &&
                 ((argument_node->kind != R_HIR_MOVE) ||
                  (argument_type->kind != R_SEMANTIC_TYPE_ARC) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) || (nested_type == NULL) ||
                  (nested_type->kind != R_SEMANTIC_TYPE_ARRAY) ||
                  (nested_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (nested_type->length != UINT64_C(0)) ||
                  (nested_type->second != R_TYPE_ID_INVALID) || (element_type == NULL) ||
                  (element_type->kind != R_SEMANTIC_TYPE_U8) ||
                  (element_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (element_type->base != R_TYPE_ID_INVALID) ||
                  (element_type->second != R_TYPE_ID_INVALID) ||
                  (element_type->length != UINT64_C(0)))) ||
                (((child_index == UINT32_C(2)) || (child_index == UINT32_C(3))) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_USIZE) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (argument_type->base != R_TYPE_ID_INVALID) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  (argument_type->length != UINT64_C(0)))) ||
                ((child_index == UINT32_C(4)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_OPTION) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  !r_mir_standard_type_is_exact(
                      build->frontend, nested_type, "std.time::instant"))) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
            if (child_index == UINT32_C(1)) {
                RMirInstruction *definition =
                    r_mir_temporary_value_definition(build, argument.value);

                if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_MOVE)) {
                    goto cleanup;
                }
                definition->is_async_staged_move = true;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = (uint32_t)arguments.count;
        instruction.call_borrow_mask.low = UINT64_C(1) << UINT32_C(0);
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if ((node->standard_operation == R_STANDARD_CALL_IO_READ) ||
        (node->standard_operation == R_STANDARD_CALL_IO_WRITE) ||
        (node->standard_operation == R_STANDARD_CALL_IO_WRITE_ALL)) {
        const bool is_read = node->standard_operation == R_STANDARD_CALL_IO_READ;
        const bool is_write = node->standard_operation == R_STANDARD_CALL_IO_WRITE;
        const char *stream_type_name = is_read ? "std.io::input" : "std.io::output";
        const char *logical_type_name = is_read    ? "std.io::read_result"
                                        : is_write ? "std.io::write_result"
                                                   : "std.io::write_all_result";
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);

        if ((node->child_count != UINT32_C(3)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            (task_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (task_type->length != UINT64_C(0)) ||
            (task_type->second != R_TYPE_ID_INVALID) ||
            !r_mir_standard_type_is_exact(build->frontend, logical_type, logical_type_name)) {
            return false;
        }
        for (child_index = UINT32_C(0); child_index < UINT32_C(3); ++child_index) {
            const RHirNode *argument_node = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, child_index));
            const RSemanticType *argument_type =
                argument_node == NULL ? NULL
                                      : r_semantic_type(build->frontend, argument_node->type);
            const RSemanticType *element_type =
                ((child_index == UINT32_C(1)) && (argument_type != NULL))
                    ? r_semantic_type(build->frontend, argument_type->base)
                    : NULL;
            const RSemanticType *nested_type =
                (((child_index == UINT32_C(0)) || (child_index == UINT32_C(2))) &&
                 (argument_type != NULL))
                    ? r_semantic_type(build->frontend, argument_type->base)
                    : NULL;
            RMirExpressionResult argument;

            if ((argument_node == NULL) || (argument_type == NULL) ||
                ((child_index == UINT32_C(0)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  !r_mir_standard_type_is_exact(build->frontend, nested_type, stream_type_name))) ||
                ((child_index == UINT32_C(1)) &&
                 ((argument_node->kind != R_HIR_MOVE) ||
                  (argument_type->kind != R_SEMANTIC_TYPE_ARRAY) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (element_type == NULL) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  (element_type->kind != R_SEMANTIC_TYPE_U8))) ||
                ((child_index == UINT32_C(2)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_OPTION) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
                  (argument_type->length != UINT64_C(0)) ||
                  (argument_type->second != R_TYPE_ID_INVALID) ||
                  !r_mir_standard_type_is_exact(
                      build->frontend, nested_type, "std.time::instant"))) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
            if (child_index == UINT32_C(1)) {
                RMirInstruction *definition =
                    r_mir_temporary_value_definition(build, argument.value);

                if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_MOVE)) {
                    goto cleanup;
                }
                definition->is_async_staged_move = true;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = (uint32_t)arguments.count;
        instruction.call_borrow_mask.low = UINT64_C(1) << UINT32_C(0);
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    {
        const RStandardScopedOperationDescriptor *descriptor =
            r_standard_scoped_operation(node->standard_operation);
        if (descriptor != NULL) {
            /* The handle borrow and the loaned view are both call-bounded for the emitter; the
               group keeps the viewed storage alive until the outcome is acknowledged. */
            const uint32_t argument_count = r_standard_scoped_argument_count(descriptor);
            const uint64_t borrow_mask =
                UINT64_C(1) | (UINT64_C(1) << r_standard_scoped_view_index(descriptor));
            if (node->child_count != argument_count || effect_carrier == NULL ||
                effect_carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER ||
                effect_carrier->base != node->type || result_type->kind != R_SEMANTIC_TYPE_TASK ||
                !r_mir_effect_set_is_single_standard_type(
                    build->frontend, effect_carrier->second, "std.async::start_error"))
                goto cleanup;
            for (child_index = 0U; child_index < argument_count; ++child_index) {
                RMirExpressionResult argument;
                if (!r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, node, child_index),
                                            depth + 1U,
                                            &argument) ||
                    argument.is_place || argument.value == R_MIR_VALUE_ID_INVALID ||
                    !r_mir_value_vector_push(build, &arguments, argument.value))
                    goto cleanup;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
            instruction.span = node->span;
            instruction.type = node->auxiliary_type;
            instruction.auxiliary_type = node->type;
            instruction.standard_operation = node->standard_operation;
            instruction.task_scope = node->task_scope;
            instruction.operand_count = (uint32_t)arguments.count;
            instruction.call_borrow_mask.low = borrow_mask;
            if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
                !r_mir_new_value(build, &instruction.result) ||
                !r_mir_append_instruction(build, instruction) ||
                !r_mir_dispatch_effect_carrier(
                    build, node->span, node->auxiliary_type, instruction.result, node, result))
                goto cleanup;
            success = true;
            goto cleanup;
        }
    }
    {
        const RStandardNetOperationDescriptor *descriptor =
            r_standard_net_operation(node->standard_operation);
        if (descriptor != NULL && descriptor->is_async) {
            uint64_t borrow_mask = descriptor->borrow_mask;
            if (node->child_count != descriptor->argument_count || effect_carrier == NULL ||
                effect_carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER ||
                effect_carrier->base != node->type || result_type->kind != R_SEMANTIC_TYPE_TASK ||
                !r_mir_effect_set_is_single_standard_type(
                    build->frontend, effect_carrier->second, "std.async::start_error"))
                goto cleanup;
            for (child_index = 0U; child_index < descriptor->argument_count; ++child_index) {
                RMirExpressionResult argument;
                if (!r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, node, child_index),
                                            depth + 1U,
                                            &argument) ||
                    argument.is_place || argument.value == R_MIR_VALUE_ID_INVALID ||
                    !r_mir_value_vector_push(build, &arguments, argument.value))
                    goto cleanup;
                if (descriptor->argument_types[child_index][0] == '&')
                    borrow_mask |= UINT64_C(1) << child_index;
                if (child_index == descriptor->move_index) {
                    RMirInstruction *definition =
                        r_mir_temporary_value_definition(build, argument.value);
                    if (definition == NULL || definition->kind != R_MIR_INSTRUCTION_MOVE)
                        goto cleanup;
                    definition->is_async_staged_move = true;
                }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
            instruction.span = node->span;
            instruction.type = node->auxiliary_type;
            instruction.auxiliary_type = node->type;
            instruction.standard_operation = node->standard_operation;
            instruction.task_scope = node->task_scope;
            instruction.operand_count = (uint32_t)arguments.count;
            instruction.call_borrow_mask.low = borrow_mask;
            if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
                !r_mir_new_value(build, &instruction.result) ||
                !r_mir_append_instruction(build, instruction) ||
                !r_mir_dispatch_effect_carrier(
                    build, node->span, node->auxiliary_type, instruction.result, node, result))
                goto cleanup;
            success = true;
            goto cleanup;
        }
    }
    {
        const RStandardFsAsyncDescriptor *descriptor =
            r_standard_fs_async_descriptor(node->standard_operation);
        if (descriptor != NULL) {
            uint64_t borrow_mask = 0U;
            if (node->child_count != descriptor->argument_count || effect_carrier == NULL ||
                effect_carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER ||
                effect_carrier->base != node->type || result_type->kind != R_SEMANTIC_TYPE_TASK ||
                !r_mir_effect_set_is_single_standard_type(
                    build->frontend, effect_carrier->second, "std.async::start_error"))
                goto cleanup;
            for (child_index = 0U; child_index < descriptor->argument_count; ++child_index) {
                RMirExpressionResult argument;
                if (!r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, node, child_index),
                                            depth + 1U,
                                            &argument) ||
                    argument.is_place || argument.value == R_MIR_VALUE_ID_INVALID ||
                    !r_mir_value_vector_push(build, &arguments, argument.value))
                    goto cleanup;
                if (descriptor->arguments[child_index][0] == '&')
                    borrow_mask |= UINT64_C(1) << child_index;
                if (child_index == descriptor->move_index) {
                    RMirInstruction *definition =
                        r_mir_temporary_value_definition(build, argument.value);
                    if (definition == NULL || definition->kind != R_MIR_INSTRUCTION_MOVE)
                        goto cleanup;
                    definition->is_async_staged_move = true;
                }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
            instruction.span = node->span;
            instruction.type = node->auxiliary_type;
            instruction.auxiliary_type = node->type;
            instruction.standard_operation = node->standard_operation;
            instruction.task_scope = node->task_scope;
            instruction.operand_count = (uint32_t)arguments.count;
            instruction.call_borrow_mask.low = borrow_mask;
            if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
                !r_mir_new_value(build, &instruction.result) ||
                !r_mir_append_instruction(build, instruction) ||
                !r_mir_dispatch_effect_carrier(
                    build, node->span, node->auxiliary_type, instruction.result, node, result))
                goto cleanup;
            success = true;
            goto cleanup;
        }
    }
    if (node->standard_operation == R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH) {
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);

        if ((node->child_count != UINT32_C(4)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            (task_type->second != R_TYPE_ID_INVALID) ||
            !r_mir_standard_type_name_equal(
                build->frontend, logical_type, "std.fs::write_file_result")) {
            return false;
        }
        for (child_index = UINT32_C(0); child_index < UINT32_C(4); ++child_index) {
            const RHirNode *argument_node = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, child_index));
            const RSemanticType *argument_type =
                argument_node == NULL ? NULL
                                      : r_semantic_type(build->frontend, argument_node->type);
            const RSemanticType *element_type =
                ((child_index == UINT32_C(2)) && (argument_type != NULL))
                    ? r_semantic_type(build->frontend, argument_type->base)
                    : NULL;
            RMirExpressionResult argument;

            if ((argument_node == NULL) || (argument_type == NULL) ||
                ((child_index < UINT32_C(2)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED))) ||
                ((child_index == UINT32_C(2)) &&
                 ((argument_node->kind != R_HIR_MOVE) ||
                  (argument_type->kind != R_SEMANTIC_TYPE_ARRAY) || (element_type == NULL) ||
                  (element_type->kind != R_SEMANTIC_TYPE_U8))) ||
                ((child_index == UINT32_C(3)) && (argument_type->kind != R_SEMANTIC_TYPE_OPTION)) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
            if (child_index == UINT32_C(2)) {
                RMirInstruction *definition =
                    r_mir_temporary_value_definition(build, argument.value);

                if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_MOVE)) {
                    goto cleanup;
                }
                definition->is_async_staged_move = true;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = (uint32_t)arguments.count;
        instruction.call_borrow_mask.low =
            (UINT64_C(1) << UINT32_C(0)) | (UINT64_C(1) << UINT32_C(1));
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if ((node->standard_operation == R_STANDARD_CALL_PROCESS_SPAWN) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_WAIT) ||
        (node->standard_operation == R_STANDARD_CALL_PROCESS_TERMINATE) ||
        (node->standard_operation == R_STANDARD_CALL_SIGNAL_NEXT)) {
        const bool is_spawn = node->standard_operation == R_STANDARD_CALL_PROCESS_SPAWN;
        const bool is_wait = node->standard_operation == R_STANDARD_CALL_PROCESS_WAIT;
        /* R-SLIB-SIGNAL-0002: next borrows its listener like terminate borrows its child. */
        const bool is_next = node->standard_operation == R_STANDARD_CALL_SIGNAL_NEXT;
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);

        if ((node->child_count != UINT32_C(2)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            (logical_type == NULL) ||
            (is_spawn ? !r_mir_standard_type_is_exact(
                            build->frontend, logical_type, "std.process::spawn_result")
             : is_wait ? !r_mir_standard_type_is_exact(
                             build->frontend, logical_type, "std.process::wait_result")
                       : ((logical_type->kind !=
                           (is_next ? R_SEMANTIC_TYPE_U64 : R_SEMANTIC_TYPE_VOID)) ||
                          !r_mir_effect_set_is_single_standard_type(
                              build->frontend, task_type->second, "std.process::process_error"))) ||
            ((is_spawn || is_wait) &&
             (r_semantic_effect_count(build->frontend, task_type->second) != UINT32_C(0)))) {
            return false;
        }
        for (child_index = UINT32_C(0); child_index < UINT32_C(2); ++child_index) {
            RMirExpressionResult argument;

            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
            if ((child_index == UINT32_C(0)) && (is_spawn || is_wait)) {
                RMirInstruction *definition =
                    r_mir_temporary_value_definition(build, argument.value);

                if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_MOVE)) {
                    goto cleanup;
                }
                definition->is_async_staged_move = true;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = UINT32_C(2);
        instruction.call_borrow_mask.low = is_spawn || is_wait ? UINT64_C(0) : UINT64_C(1);
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction) ||
            !r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if ((node->standard_operation == R_STANDARD_CALL_FS_READ_FILE) ||
        (node->standard_operation == R_STANDARD_CALL_FS_OPEN_DIRECTORY) ||
        (node->standard_operation == R_STANDARD_CALL_FS_OPEN_FILE) ||
        (node->standard_operation == R_STANDARD_CALL_FS_FILE_METADATA)) {
        const bool is_read_file = node->standard_operation == R_STANDARD_CALL_FS_READ_FILE;
        const bool is_open_file = node->standard_operation == R_STANDARD_CALL_FS_OPEN_FILE;
        const bool is_file_metadata = node->standard_operation == R_STANDARD_CALL_FS_FILE_METADATA;
        const uint32_t fs_expected_count =
            (is_read_file || is_open_file) ? UINT32_C(3) : UINT32_C(2);
        const RHirNode *path;
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);
        RMirExpressionResult path_place;
        bool path_is_place;

        if ((node->child_count != fs_expected_count) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, task_type->second, "std.fs::fs_error") ||
            (logical_type == NULL)) {
            return false;
        }
        if (is_read_file) {
            const RSemanticType *element_type =
                r_semantic_type(build->frontend, logical_type->base);

            if ((logical_type->kind != R_SEMANTIC_TYPE_ARRAY) || (element_type == NULL) ||
                (element_type->kind != R_SEMANTIC_TYPE_U8)) {
                return false;
            }
        } else if (!r_mir_standard_type_is_exact(build->frontend,
                                                 logical_type,
                                                 is_file_metadata ? "std.fs::metadata"
                                                 : is_open_file   ? "std.fs::file"
                                                                  : "std.fs::directory")) {
            return false;
        }
        path = r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
        path_is_place = (path != NULL) && (path->kind == R_HIR_BORROW);
        if ((path == NULL) ||
            (path_is_place
                 ? ((path->child_count != UINT32_C(1)) ||
                    !r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, path, UINT32_C(0)),
                                            depth + UINT32_C(1),
                                            &path_place) ||
                    !path_place.is_place)
                 : (!r_mir_lower_expression(build,
                                            r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                            depth + UINT32_C(1),
                                            &path_place) ||
                    path_place.is_place || (path_place.value == R_MIR_VALUE_ID_INVALID) ||
                    !r_mir_value_vector_push(build, &arguments, path_place.value)))) {
            goto cleanup;
        }
        for (child_index = UINT32_C(1); child_index < fs_expected_count; ++child_index) {
            RMirExpressionResult argument;

            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.borrow_origin = path->borrow_origin;
        instruction.borrow_origin_set = path->borrow_origin_set;
        instruction.borrow_origin_multiple = path->borrow_origin_multiple;
        if (path_is_place) {
            instruction.symbol = path_place.place_symbol;
            instruction.operand0 = path_place.place_projection;
            instruction.place_ordinal = path_place.place_ordinal;
            instruction.place_is_parameter = path_place.place_is_parameter;
        } else {
            instruction.call_borrow_mask.low = UINT64_C(1);
        }
        instruction.operand_count = (uint32_t)arguments.count;
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if (node->standard_operation == R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) {
        const RSemanticType *start_type = effect_carrier;
        const RSemanticType *task_type = result_type;
        const RSemanticType *logical_type =
            task_type == NULL ? NULL : r_semantic_type(build->frontend, task_type->base);

        if ((node->child_count != UINT32_C(4)) || (start_type == NULL) ||
            (start_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            (start_type->base != node->type) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, start_type->second, "std.async::start_error") ||
            (task_type == NULL) || (task_type->kind != R_SEMANTIC_TYPE_TASK) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, task_type->second, "std.fs::fs_error") ||
            (logical_type == NULL) || (logical_type->kind != R_SEMANTIC_TYPE_VOID)) {
            return false;
        }
        for (child_index = UINT32_C(0); child_index < UINT32_C(4); ++child_index) {
            const RHirNode *argument_node = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, child_index));
            const RSemanticType *argument_type =
                argument_node == NULL ? NULL
                                      : r_semantic_type(build->frontend, argument_node->type);
            RMirExpressionResult argument;

            if ((argument_node == NULL) || (argument_type == NULL) ||
                ((child_index < UINT32_C(2)) &&
                 ((argument_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                  (argument_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED))) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + UINT32_C(1),
                                        &argument) ||
                argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &arguments, argument.value)) {
                goto cleanup;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.auxiliary_type = node->type;
        instruction.standard_operation = node->standard_operation;
        instruction.task_scope = node->task_scope;
        instruction.operand_count = (uint32_t)arguments.count;
        instruction.call_borrow_mask.low =
            (UINT64_C(1) << UINT32_C(0)) | (UINT64_C(1) << UINT32_C(1));
        if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
            !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
        success = true;
        goto cleanup;
    }
    if ((node->standard_operation == R_STANDARD_CALL_FS_AS_ERROR) ||
        (node->standard_operation == R_STANDARD_CALL_IO_AS_ERROR)) {
        const bool is_fs = node->standard_operation == R_STANDARD_CALL_FS_AS_ERROR;
        const RHirNode *source =
            node->child_count == UINT32_C(1)
                ? r_mir_hir_node(build->frontend,
                                 r_mir_hir_child(build->frontend, node, UINT32_C(0)))
                : NULL;
        const RSemanticType *source_type = r_semantic_type(build->frontend, node->auxiliary_type);

        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_standard_type_is_exact(build->frontend, result_type, "std.error::error") ||
            !r_mir_standard_type_is_exact(
                build->frontend, source_type, is_fs ? "std.fs::fs_error" : "std.io::io_error") ||
            (source == NULL) || (source->type != node->auxiliary_type)) {
            return false;
        }
    }
    if (node->standard_operation == R_STANDARD_CALL_ERROR_ERASURE) {
        const RHirNode *source =
            node->child_count == UINT32_C(1)
                ? r_mir_hir_node(build->frontend,
                                 r_mir_hir_child(build->frontend, node, UINT32_C(0)))
                : NULL;

        if ((error_erasure == NULL) || (node->child_count != UINT32_C(1)) ||
            !r_mir_standard_type_is_exact(build->frontend, result_type, "std.error::error") ||
            !r_semantic_error_erasure_accepts(
                build->frontend, error_erasure, node->auxiliary_type) ||
            (source == NULL) || (source->type != node->auxiliary_type)) {
            return false;
        }
    }
    if (node->standard_operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE) {
        const RHirNode *source =
            node->child_count == UINT32_C(1)
                ? r_mir_hir_node(build->frontend,
                                 r_mir_hir_child(build->frontend, node, UINT32_C(0)))
                : NULL;
        const RSemanticType *source_type = r_semantic_type(build->frontend, node->auxiliary_type);
        const RSemanticType *path_type =
            source_type == NULL ? NULL : r_semantic_type(build->frontend, source_type->base);

        if ((node->child_count != UINT32_C(1)) || (result_type->kind != R_SEMANTIC_TYPE_BOOL) ||
            (result_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) ||
            (result_type->base != R_TYPE_ID_INVALID) ||
            (result_type->second != R_TYPE_ID_INVALID) || (result_type->length != UINT64_C(0)) ||
            (source_type == NULL) || (source_type->kind != R_SEMANTIC_TYPE_BORROW) ||
            (source_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED) ||
            (source_type->second != R_TYPE_ID_INVALID) || (source_type->length != UINT64_C(0)) ||
            !r_mir_standard_type_is_exact(build->frontend, path_type, "std.fs::path") ||
            (source == NULL) || (source->type != node->auxiliary_type)) {
            return false;
        }
    }
    if ((node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
        (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED)) {
        const bool scoped = node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED;
        const RSemanticSymbol *entry =
            (node->symbol == R_SYMBOL_ID_INVALID) ||
                    ((size_t)node->symbol > build->frontend->semantic_symbol_count)
                ? NULL
                : &build->frontend->semantic_symbols[(size_t)node->symbol - 1U];
        if ((entry == NULL) || (entry->kind != R_SEMANTIC_SYMBOL_FUNCTION) || entry->is_unsafe ||
            entry->is_async || entry->is_extern_c || entry->poisoned) {
            return false;
        }
        if (!r_mir_standard_type_name_equal(build->frontend,
                                            result_type,
                                            scoped ? "std.thread::scoped_join_handle"
                                                   : "std.thread::join_handle") ||
            (result_type->base != entry->return_type) ||
            (result_type->second != entry->throws_type) ||
            (node->child_count != entry->parameter_count) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, effect_carrier->second, "std.thread::thread_error") ||
            (scoped ? (node->integer_value == UINT64_C(0))
                    : (node->integer_value != UINT64_C(0)))) {
            return false;
        }
    }
    if (node->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING) {
        /* R-SLIB-ASYNC-0017: the task carries the entry's result and checked errors and the start
           may fail only with std.async::start_error. */
        const RSemanticSymbol *entry =
            (node->symbol == R_SYMBOL_ID_INVALID) ||
                    ((size_t)node->symbol > build->frontend->semantic_symbol_count)
                ? NULL
                : &build->frontend->semantic_symbols[(size_t)node->symbol - 1U];
        if ((entry == NULL) || (entry->kind != R_SEMANTIC_SYMBOL_FUNCTION) || entry->is_unsafe ||
            entry->is_async || entry->is_extern_c || entry->poisoned) {
            return false;
        }
        if ((result_type->kind != R_SEMANTIC_TYPE_TASK) ||
            (result_type->base != entry->return_type) ||
            (result_type->second != entry->throws_type) ||
            (node->child_count != entry->parameter_count) ||
            !r_mir_effect_set_is_single_standard_type(
                build->frontend, effect_carrier->second, "std.async::start_error") ||
            (node->integer_value != UINT64_C(0))) {
            return false;
        }
    }
    if ((node->standard_operation == R_STANDARD_CALL_SYNC_ONCE_NEW) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_ONCE_LOCK)) {
        const bool is_lock = node->standard_operation == R_STANDARD_CALL_SYNC_ONCE_LOCK;

        if ((node->child_count != UINT32_C(0)) || (node->symbol != R_SYMBOL_ID_INVALID) ||
            (node->integer_value != UINT64_C(0)) ||
            (is_lock
                 ? (!r_mir_standard_type_name_equal(
                        build->frontend, result_type, "std.sync::once_lock") ||
                    (result_type->base != node->auxiliary_type) ||
                    (result_type->second != R_TYPE_ID_INVALID))
                 : (!r_mir_standard_type_is_exact(build->frontend, result_type, "std.sync::once") ||
                    (node->auxiliary_type != node->type)))) {
            return false;
        }
    }
    if ((node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE) ||
        (node->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT)) {
        const bool is_get = node->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT;
        const RHirNode *resource =
            node->child_count == UINT32_C(1)
                ? r_mir_hir_node(build->frontend,
                                 r_mir_hir_child(build->frontend, node, UINT32_C(0)))
                : NULL;
        const RSemanticType *resource_borrow =
            resource == NULL ? NULL : r_semantic_type(build->frontend, resource->type);
        const RSemanticType *resource_type =
            resource_borrow == NULL ? NULL
                                    : r_semantic_type(build->frontend, resource_borrow->base);
        const RSemanticSymbol *initializer =
            (node->symbol == R_SYMBOL_ID_INVALID) ||
                    ((size_t)node->symbol > build->frontend->semantic_symbol_count)
                ? NULL
                : &build->frontend->semantic_symbols[(size_t)node->symbol - 1U];
        const bool initializer_has_effect =
            (initializer != NULL) &&
            (r_semantic_effect_count(build->frontend, initializer->throws_type) != UINT32_C(0));

        if ((resource == NULL) || (resource_borrow == NULL) || (resource_type == NULL) ||
            (resource_borrow->kind != R_SEMANTIC_TYPE_BORROW) ||
            (resource_borrow->flags != R_SEMANTIC_TYPE_FLAG_SHARED) || (initializer == NULL) ||
            (initializer->kind != R_SEMANTIC_SYMBOL_FUNCTION) ||
            (initializer->parent != R_SYMBOL_ID_INVALID) || initializer->is_unsafe ||
            initializer->is_async || initializer->is_extern_c || initializer->poisoned ||
            (initializer->parameter_count != UINT32_C(0)) ||
            (initializer_has_effect != (node->integer_value != UINT64_C(0))) ||
            (node->runtime_type != (initializer_has_effect ? initializer->effect_carrier_type
                                                           : initializer->return_type)) ||
            (initializer_has_effect &&
             ((effect_carrier == NULL) || (effect_carrier->second != initializer->throws_type))) ||
            (!initializer_has_effect && (node->auxiliary_type != resource_borrow->base))) {
            return false;
        }
        if (is_get) {
            if (!r_mir_standard_type_name_equal(
                    build->frontend, resource_type, "std.sync::once_lock") ||
                (resource_type->base == R_TYPE_ID_INVALID) ||
                (resource_type->second != R_TYPE_ID_INVALID) ||
                (result_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                (result_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED) ||
                (result_type->base != resource_type->base) ||
                (initializer->return_type != resource_type->base)) {
                return false;
            }
        } else if (!r_mir_standard_type_is_exact(
                       build->frontend, resource_type, "std.sync::once") ||
                   (result_type->kind != R_SEMANTIC_TYPE_VOID) ||
                   (initializer->return_type != node->type)) {
            return false;
        }
    }
    expected_count =
        (math_operation != NULL) ? math_operation->parameter_count
        /* R-SLIB-NET-0012..0013: the synchronous option operations take their descriptor's
           operands. */
        : (r_standard_net_operation(node->standard_operation) != NULL)
            ? r_standard_net_operation(node->standard_operation)->argument_count
        : r_async_sync_operation(node->standard_operation)
            ? r_async_sync_operand_count(r_async_sync_descriptor(node->standard_operation))
        : ((node->standard_operation == R_STANDARD_CALL_IO_STDIN) ||
           (node->standard_operation == R_STANDARD_CALL_IO_STDOUT) ||
           (node->standard_operation == R_STANDARD_CALL_IO_STDERR) ||
           (node->standard_operation == R_STANDARD_CALL_ARRAY_CREATE) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_CREATE) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_CREATE) ||
           (node->standard_operation == R_STANDARD_CALL_C_TARGET) ||
           (node->standard_operation == R_STANDARD_CALL_C_ATTACH_THREAD) ||
           (node->standard_operation == R_STANDARD_CALL_STRING_CREATE) ||
           (node->standard_operation == R_STANDARD_CALL_FORMAT_CREATE) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_MONOTONIC_NOW) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_SYSTEM_NOW))
            ? UINT32_C(0)
        : (node->standard_operation == R_STANDARD_CALL_PROCESS_ABORT) ? UINT32_C(0)
        : ((node->standard_operation == R_STANDARD_CALL_THREAD_CURRENT) ||
           (node->standard_operation == R_STANDARD_CALL_THREAD_PARK) ||
           (node->standard_operation == R_STANDARD_CALL_THREAD_YIELD_NOW))
            ? UINT32_C(0)
        : ((node->standard_operation == R_STANDARD_CALL_ENV_ARGUMENTS) ||
           (node->standard_operation == R_STANDARD_CALL_ENV_VARIABLES))
            ? UINT32_C(0)
        : ((node->standard_operation == R_STANDARD_CALL_SYNC_ONCE_NEW) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_ONCE_LOCK) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_CHANNEL) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_CONDVAR_NEW) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_RECURSION_ENTER) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_RECURSION_LEAVE))
            ? UINT32_C(0)
        : ((node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
           (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
           (node->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING))
            ? node->child_count
        : ((node->standard_operation == R_STANDARD_CALL_SYNC_SEND) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_SYNC_SEND) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_TRY_SEND) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_SET) ||
           (node->standard_operation == R_STANDARD_CALL_SYNC_WAIT))
            ? UINT32_C(2)
        : (node->standard_operation == R_STANDARD_CALL_CONVERT_PARSE)
            ? (((node->integer_value == UINT64_C(10)) || (node->integer_value == UINT64_C(11)) ||
                (node->integer_value == UINT64_C(38)) || (node->integer_value == UINT64_C(39)) ||
                (node->integer_value == UINT64_C(40)))
                   ? UINT32_C(1)
                   : UINT32_C(2))
        : (node->standard_operation == R_STANDARD_CALL_BITS_READ)             ? UINT32_C(3)
        : (node->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_INTEGER) ? UINT32_C(3)
        : (node->standard_operation == R_STANDARD_CALL_PROCESS_ENVIRONMENT)   ? UINT32_C(3)
        : (node->standard_operation == R_STANDARD_CALL_BYTES_COPY_WITHIN)     ? UINT32_C(4)
        : (node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER) ? UINT32_C(1)
        : ((node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_APPEND) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_REPLACE) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_TAKE) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_SWAP) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_KEY_EQUAL) ||
           (node->standard_operation == R_STANDARD_CALL_ARRAY_PUSH) ||
           (node->standard_operation == R_STANDARD_CALL_ARRAY_FILLED) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_APPEND) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_APPEND_U8) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_APPEND_U16_LE) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_APPEND_U32_LE) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_APPEND_U64_LE) ||
           (node->standard_operation == R_STANDARD_CALL_ALLOC_BYTES) ||
           (node->standard_operation == R_STANDARD_CALL_C_ADOPT_HANDLE) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_EQUAL) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_COMPARE) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_COPY) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_FILL) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_FIND) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_FIND_SLICE) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_STARTS_WITH) ||
           (node->standard_operation == R_STANDARD_CALL_BYTES_ENDS_WITH) ||
           (node->standard_operation == R_STANDARD_CALL_SECRET_CONSTANT_TIME_EQUAL) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_COMPARE) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_FROM_PARTS) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_ADD) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_SUB) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_DURATION_MULTIPLY) ||
           (node->standard_operation == R_STANDARD_CALL_STRING_RESERVE) ||
           (node->standard_operation == R_STANDARD_CALL_STRING_APPEND_STR) ||
           (node->standard_operation == R_STANDARD_CALL_STRING_APPEND_UTF8) ||
           (node->standard_operation == R_STANDARD_CALL_STRING_PUSH_SCALAR) ||
           (node->standard_operation == R_STANDARD_CALL_STRING_TRUNCATE) ||
           ((node->standard_operation >= R_STANDARD_CALL_FORMAT_APPEND_STR) &&
            (node->standard_operation <= R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE)) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_INSTANT_ADD) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_INSTANT_DURATION) ||
           (node->standard_operation == R_STANDARD_CALL_TIME_SYSTEM_ADD) ||
           (node->standard_operation == R_STANDARD_CALL_FS_PATH_JOIN) ||
           (node->standard_operation == R_STANDARD_CALL_ENV_SET) ||
           (node->standard_operation == R_STANDARD_CALL_PROCESS_ARG) ||
           (node->standard_operation == R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT) ||
           (node->standard_operation == R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY) ||
           (node->standard_operation == R_STANDARD_CALL_PROCESS_SET_STDIO) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_STORE) ||
           ((node->standard_operation >= R_STANDARD_CALL_CORE_CHECKED_ADD) &&
            (node->standard_operation <= R_STANDARD_CALL_CORE_SATURATING_MUL)) ||
           (node->standard_operation == R_STANDARD_CALL_CORE_ATOMIC_LOAD))
            ? UINT32_C(2)
        : ((node->standard_operation == R_STANDARD_CALL_ARC_PTR_EQ) ||
           (node->standard_operation == R_STANDARD_CALL_RC_PTR_EQ))
            ? UINT32_C(2)
        : ((node->standard_operation == R_STANDARD_CALL_LIST_INSERT_BEFORE) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_INSERT_AFTER) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_INSERT))
            ? UINT32_C(3)
        : (node->standard_operation == R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE) ? UINT32_C(5)
        : (((node->standard_operation >= R_STANDARD_CALL_CORE_ATOMIC_STORE) &&
            (node->standard_operation <= R_STANDARD_CALL_CORE_ATOMIC_FETCH_XOR)))
            ? UINT32_C(3)
        : ((node->standard_operation == R_STANDARD_CALL_ARRAY_RESERVE) ||
           (node->standard_operation == R_STANDARD_CALL_ARRAY_REMOVE) ||
           (node->standard_operation == R_STANDARD_CALL_ARRAY_GET) ||
           (node->standard_operation == R_STANDARD_CALL_ARRAY_GET_MUT) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_PUSH_FRONT) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_PUSH_BACK) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_GET) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_GET_MUT) ||
           (node->standard_operation == R_STANDARD_CALL_LIST_REMOVE) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_RESERVE) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_CONTAINS) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_GET) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_GET_MUT) ||
           (node->standard_operation == R_STANDARD_CALL_DICT_REMOVE))
            ? UINT32_C(2)
            : UINT32_C(1);
    if (node->child_count != expected_count) {
        return false;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        RMirExpressionResult argument;
        if (!r_mir_lower_expression(build,
                                    r_mir_hir_child(build->frontend, node, child_index),
                                    depth + 1U,
                                    &argument) ||
            argument.is_place || (argument.value == R_MIR_VALUE_ID_INVALID) ||
            !r_mir_value_vector_push(build, &arguments, argument.value)) {
            goto cleanup;
        }
    }
    if ((node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
        (node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
        (node->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING)) {
        for (child_index = UINT32_C(0); child_index < (uint32_t)arguments.count; ++child_index) {
            RMirInstruction *definition =
                r_mir_temporary_value_definition(build, arguments.items[child_index]);

            if ((definition != NULL) && (definition->kind == R_MIR_INSTRUCTION_MOVE)) {
                definition->is_async_staged_move = true;
            }
        }
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_STANDARD_CALL;
    instruction.span = node->span;
    instruction.type = has_checked_effect ? node->auxiliary_type : node->type;
    instruction.auxiliary_type = has_checked_effect ? node->type : node->auxiliary_type;
    instruction.runtime_type = node->runtime_type;
    instruction.operation = node->operation;
    instruction.standard_operation = node->standard_operation;
    instruction.symbol = node->symbol;
    instruction.task_scope = node->task_scope;
    instruction.borrow_origin = node->borrow_origin;
    instruction.borrow_origin_set = node->borrow_origin_set;
    instruction.borrow_origin_multiple = node->borrow_origin_multiple;
    instruction.integer_value = node->integer_value;
    instruction.operand_count = (uint32_t)arguments.count;
    if (node->standard_operation == R_STANDARD_CALL_CORE_HASH ||
        node->standard_operation == R_STANDARD_CALL_CORE_KEY_EQUAL) {
        instruction.call_borrow_mask.low =
            node->standard_operation == R_STANDARD_CALL_CORE_HASH ? 1U : 3U;
    } else if (r_async_sync_operation(node->standard_operation)) {
        /* L30: the first operand of a resource operation is a borrow of the resource. */
        instruction.call_borrow_mask.low =
            r_async_sync_borrows(r_async_sync_descriptor(node->standard_operation)) ? UINT64_C(1)
                                                                                    : UINT64_C(0);
    } else if ((node->standard_operation == R_STANDARD_CALL_BYTES_APPEND) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_EQUAL) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_COMPARE) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_COPY) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_FIND_SLICE) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_STARTS_WITH) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_ENDS_WITH) ||
               (node->standard_operation == R_STANDARD_CALL_BITS_READ)) {
        instruction.call_borrow_mask.low = UINT64_C(3);
    } else if ((node->standard_operation == R_STANDARD_CALL_STRING_LEN) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_CAPACITY) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_CLEAR) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_FROM_STR) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_AS_STR) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_AS_BYTES) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_FROM_UTF8)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_STRING_RESERVE) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_PUSH_SCALAR) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_TRUNCATE)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_STRING_APPEND_STR) ||
               (node->standard_operation == R_STANDARD_CALL_STRING_APPEND_UTF8)) {
        instruction.call_borrow_mask.low = UINT64_C(3);
    } else if ((node->standard_operation >= R_STANDARD_CALL_ENV_GET) &&
               (node->standard_operation <= R_STANDARD_CALL_ENV_REMOVE)) {
        instruction.call_borrow_mask.low =
            node->standard_operation == R_STANDARD_CALL_ENV_SET ? UINT64_C(3) : UINT64_C(1);
    } else if (node->standard_operation == R_STANDARD_CALL_CONVERT_PARSE) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_NET_PARSE_IP) ||
               (node->standard_operation == R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_RECEIVE) ||
               (r_standard_net_operation(node->standard_operation) != NULL)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation >= R_STANDARD_CALL_PROCESS_COMMAND_CREATE) &&
               (node->standard_operation <= R_STANDARD_CALL_PROCESS_ABORT)) {
        if (node->standard_operation == R_STANDARD_CALL_PROCESS_ENVIRONMENT) {
            instruction.call_borrow_mask.low = UINT64_C(7);
        } else if ((node->standard_operation == R_STANDARD_CALL_PROCESS_ARG) ||
                   (node->standard_operation == R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT) ||
                   (node->standard_operation == R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY)) {
            instruction.call_borrow_mask.low = UINT64_C(3);
        } else if ((node->standard_operation != R_STANDARD_CALL_PROCESS_EXIT) &&
                   (node->standard_operation != R_STANDARD_CALL_PROCESS_ABORT)) {
            instruction.call_borrow_mask.low = UINT64_C(1);
        }
    } else if ((node->standard_operation >= R_STANDARD_CALL_C_STRING_FROM_STR) &&
               (node->standard_operation <= R_STANDARD_CALL_C_COPY_UTF8)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if (node->standard_operation == R_STANDARD_CALL_CORE_REPLACE ||
               node->standard_operation == R_STANDARD_CALL_CORE_TAKE ||
               node->standard_operation == R_STANDARD_CALL_CORE_CLONE) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if (node->standard_operation == R_STANDARD_CALL_CORE_SWAP) {
        instruction.call_borrow_mask.low = UINT64_C(3);
    } else if (node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if (node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_APPEND) {
        instruction.call_borrow_mask.low = UINT64_C(3);
    } else if (node->standard_operation == R_STANDARD_CALL_C_HANDLE_POINTER) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_SECRET_LEN) ||
               (node->standard_operation == R_STANDARD_CALL_SECRET_AS_SLICE) ||
               (node->standard_operation == R_STANDARD_CALL_SECRET_AS_SLICE_MUT) ||
               (node->standard_operation == R_STANDARD_CALL_SECRET_ZEROIZE) ||
               (node->standard_operation == R_STANDARD_CALL_RANDOM_FILL)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if (node->standard_operation == R_STANDARD_CALL_SECRET_CONSTANT_TIME_EQUAL) {
        instruction.call_borrow_mask.low = UINT64_C(3);
    } else if ((node->standard_operation == R_STANDARD_CALL_FS_PATH_CLONE) ||
               (node->standard_operation == R_STANDARD_CALL_FS_PATH_TO_UTF8) ||
               (node->standard_operation == R_STANDARD_CALL_FS_PATH_JOIN)) {
        instruction.call_borrow_mask.low =
            node->standard_operation == R_STANDARD_CALL_FS_PATH_JOIN ? UINT64_C(3) : UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_THREAD_CLONE) ||
               (node->standard_operation == R_STANDARD_CALL_THREAD_UNPARK) ||
               (node->standard_operation == R_STANDARD_CALL_THREAD_PANIC_CATEGORY) ||
               (node->standard_operation == R_STANDARD_CALL_THREAD_PANIC_TEXT)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_SYNC_BARRIER_WAIT) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_NOTIFY_ONE) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_NOTIFY_ALL)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_FORMAT_AS_STR) ||
               (node->standard_operation == R_STANDARD_CALL_FORMAT_CLEAR)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if (node->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_INTEGER) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation >= R_STANDARD_CALL_FORMAT_APPEND_STR) &&
               (node->standard_operation <= R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
        if (node->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_STR) {
            instruction.call_borrow_mask.low = UINT64_C(3);
        }
    } else if ((node->standard_operation == R_STANDARD_CALL_ARRAY_PUSH) ||
               ((node->standard_operation >= R_STANDARD_CALL_ARRAY_CAPACITY) &&
                (node->standard_operation <= R_STANDARD_CALL_ARRAY_CLEAR)) ||
               ((node->standard_operation >= R_STANDARD_CALL_LIST_PUSH_FRONT) &&
                (node->standard_operation <= R_STANDARD_CALL_LIST_NEXT)) ||
               ((node->standard_operation >= R_STANDARD_CALL_DICT_RESERVE) &&
                (node->standard_operation <= R_STANDARD_CALL_DICT_NEXT)) ||
               (node->standard_operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE) ||
               ((node->standard_operation >= R_STANDARD_CALL_BYTES_APPEND_U8) &&
                (node->standard_operation <= R_STANDARD_CALL_BYTES_APPEND_U64_LE)) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_COPY_WITHIN) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_FILL) ||
               (node->standard_operation == R_STANDARD_CALL_BYTES_FIND) ||
               ((node->standard_operation >= R_STANDARD_CALL_HASH_CRC32) &&
                (node->standard_operation <= R_STANDARD_CALL_HASH_SHA512)) ||
               (node->standard_operation == R_STANDARD_CALL_UTF8_IS_VALID) ||
               (node->standard_operation == R_STANDARD_CALL_UTF8_VALIDATE) ||
               (node->standard_operation == R_STANDARD_CALL_BITS_ALIGN_BYTE)) {
        instruction.call_borrow_mask.low = UINT64_C(1) << UINT32_C(0);
        if ((node->standard_operation == R_STANDARD_CALL_LIST_INSERT_BEFORE) ||
            (node->standard_operation == R_STANDARD_CALL_LIST_INSERT_AFTER) ||
            (node->standard_operation == R_STANDARD_CALL_LIST_REMOVE) ||
            (node->standard_operation == R_STANDARD_CALL_DICT_CONTAINS) ||
            (node->standard_operation == R_STANDARD_CALL_DICT_GET) ||
            (node->standard_operation == R_STANDARD_CALL_DICT_GET_MUT) ||
            (node->standard_operation == R_STANDARD_CALL_DICT_REMOVE)) {
            instruction.call_borrow_mask.low |= UINT64_C(1) << UINT32_C(1);
        }
    } else if (((node->standard_operation >= R_STANDARD_CALL_SYNC_LOCK) &&
                (node->standard_operation <= R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT)) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_WAIT) ||
               ((node->standard_operation >= R_STANDARD_CALL_SYNC_SENDER) &&
                (node->standard_operation <= R_STANDARD_CALL_SYNC_SET) &&
                (node->standard_operation != R_STANDARD_CALL_SYNC_SYNC_RECEIVER)) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE) ||
               (node->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT)) {
        instruction.call_borrow_mask.low = UINT64_C(1) << UINT32_C(0);
    } else if ((node->standard_operation == R_STANDARD_CALL_ARC_CLONE_WEAK) ||
               (node->standard_operation == R_STANDARD_CALL_ARC_DOWNGRADE) ||
               (node->standard_operation == R_STANDARD_CALL_ARC_UPGRADE) ||
               (node->standard_operation == R_STANDARD_CALL_ARC_GET_MUT) ||
               (node->standard_operation == R_STANDARD_CALL_ARC_STRONG_COUNT) ||
               (node->standard_operation == R_STANDARD_CALL_ARC_WEAK_COUNT) ||
               (node->standard_operation == R_STANDARD_CALL_RC_CLONE_WEAK) ||
               (node->standard_operation == R_STANDARD_CALL_RC_DOWNGRADE) ||
               (node->standard_operation == R_STANDARD_CALL_RC_UPGRADE) ||
               (node->standard_operation == R_STANDARD_CALL_RC_GET_MUT) ||
               (node->standard_operation == R_STANDARD_CALL_RC_STRONG_COUNT) ||
               (node->standard_operation == R_STANDARD_CALL_RC_WEAK_COUNT)) {
        instruction.call_borrow_mask.low = UINT64_C(1);
    } else if ((node->standard_operation == R_STANDARD_CALL_ARC_PTR_EQ) ||
               (node->standard_operation == R_STANDARD_CALL_RC_PTR_EQ)) {
        instruction.call_borrow_mask.low = UINT64_C(3);
    }
    if (!r_mir_append_operands(build, &arguments, &instruction.first_operand) ||
        (((has_checked_effect ? effect_carrier : result_type)->kind != R_SEMANTIC_TYPE_VOID) &&
         ((has_checked_effect ? effect_carrier : result_type)->kind != R_SEMANTIC_TYPE_NEVER) &&
         !r_mir_new_value(build, &instruction.result)) ||
        !r_mir_append_instruction(build, instruction)) {
        goto cleanup;
    }
    if (has_checked_effect) {
        if (!r_mir_dispatch_effect_carrier(
                build, node->span, node->auxiliary_type, instruction.result, node, result)) {
            goto cleanup;
        }
    } else {
        (void)memset(result, 0, sizeof(*result));
        result->value = instruction.result;
    }
    success = true;

cleanup:
    r_context_free(build->frontend, arguments.items);
    return success;
}

static bool r_mir_lower_assignment(RMirBuildContext *build,
                                   const RHirNode *node,
                                   uint32_t depth,
                                   RMirExpressionResult *result) {
    RMirExpressionResult place;
    RMirExpressionResult value;
    RMirInstruction instruction;
    const RHirNode *place_node =
        r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
    RTypeId value_type;

    if ((place_node == NULL) ||
        !r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
        !place.is_place) {
        return false;
    }
    value_type = place_node->type;
    if (node->kind == R_HIR_COMPOUND_ASSIGN) {
        RMirValueId old_value;
        RMirValueId combined_value;
        const RTypeId calculation_type =
            node->auxiliary_type == R_TYPE_ID_INVALID ? value_type : node->auxiliary_type;
        RTokenKind operation = r_mir_compound_operation(node->operation);
        if (operation == R_TOKEN_INVALID) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_LOAD;
        instruction.span = node->span;
        instruction.type = value_type;
        instruction.symbol = place.place_symbol;
        instruction.place_ordinal = place.place_ordinal;
        instruction.place_is_parameter = place.place_is_parameter;
        instruction.operand1 = place.place_projection;
        if (!r_mir_new_value(build, &old_value)) {
            return false;
        }
        instruction.result = old_value;
        if (!r_mir_append_instruction(build, instruction) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U, &value) ||
            value.is_place || (value.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        if (calculation_type != value_type) {
            RMirValueId converted_old;

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_CAST;
            instruction.span = node->span;
            instruction.type = calculation_type;
            instruction.operation = R_TOKEN_INVALID;
            instruction.operand0 = old_value;
            if (!r_mir_new_value(build, &converted_old)) {
                return false;
            }
            instruction.result = converted_old;
            if (!r_mir_append_instruction(build, instruction)) {
                return false;
            }
            old_value = converted_old;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BINARY;
        instruction.span = node->span;
        instruction.type = calculation_type;
        instruction.operation = operation;
        instruction.operand0 = old_value;
        instruction.operand1 = value.value;
        if (!r_mir_new_value(build, &combined_value)) {
            return false;
        }
        instruction.result = combined_value;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        value.value = combined_value;
        if (calculation_type != value_type) {
            RMirValueId converted_value;

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_CAST;
            instruction.span = node->span;
            instruction.type = value_type;
            instruction.operation = node->operation;
            instruction.operand0 = value.value;
            if (!r_mir_new_value(build, &converted_value)) {
                return false;
            }
            instruction.result = converted_value;
            if (!r_mir_append_instruction(build, instruction)) {
                return false;
            }
            value.value = converted_value;
        }
    } else if (!r_mir_lower_expression(build,
                                       r_mir_hir_child(build->frontend, node, UINT32_C(1)),
                                       depth + 1U,
                                       &value) ||
               value.is_place || (value.value == R_MIR_VALUE_ID_INVALID)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_STORE;
    instruction.span = node->span;
    instruction.type = value_type;
    instruction.symbol = place.place_symbol;
    instruction.operand0 = value.value;
    instruction.place_ordinal = place.place_ordinal;
    instruction.place_is_parameter = place.place_is_parameter;
    instruction.operand1 = place.place_projection;
    instruction.replaces_initialized = node->replaces_initialized;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    {
        const RSemanticType *result_type = r_semantic_type(build->frontend, node->type);
        if ((result_type != NULL) && (result_type->kind != R_SEMANTIC_TYPE_VOID)) {
            result->value = value.value;
        }
    }
    return true;
}

static bool r_mir_lower_expression(RMirBuildContext *build,
                                   RHirNodeId node_id,
                                   uint32_t depth,
                                   RMirExpressionResult *result) {
    const RHirNode *node = r_mir_hir_node(build->frontend, node_id);
    RMirInstruction instruction;

    if ((node == NULL) || (depth > r_frontend_tree_depth_limit(build->frontend))) {
        if (node != NULL) {
            build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        }
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    if (node->kind == R_HIR_PLACE) {
        const RSemanticSymbol *symbol =
            (node->symbol == R_SYMBOL_ID_INVALID) ||
                    ((size_t)node->symbol > build->frontend->semantic_symbol_count)
                ? NULL
                : &build->frontend->semantic_symbols[(size_t)node->symbol - 1U];
        const RMirPlace *place = r_mir_find_place(build, node->symbol);
        if ((place == NULL) && (symbol != NULL) &&
            (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT)) {
            const uint32_t ordinal = build->next_local;

            if ((ordinal == UINT32_MAX) || !r_mir_push_place(build, node->symbol, ordinal, false)) {
                return false;
            }
            build->next_local += UINT32_C(1);
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_LOCAL;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.symbol = node->symbol;
            instruction.place_ordinal = ordinal;
            if (!r_mir_append_instruction(build, instruction)) {
                return false;
            }
            place = r_mir_find_place(build, node->symbol);
        }
        if (place == NULL) {
            return false;
        }
        result->place_symbol = node->symbol;
        result->place_ordinal = place->ordinal;
        result->place_is_parameter = place->is_parameter;
        result->is_place = true;
        return true;
    }
    if (node->kind == R_HIR_FIELD_PLACE) {
        RMirExpressionResult base;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &base) ||
            !base.is_place || (node->aggregate_member == UINT32_C(0))) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_FIELD;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = base.place_symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = base.place_projection;
        instruction.place_ordinal = base.place_ordinal;
        instruction.place_is_parameter = base.place_is_parameter;
        instruction.aggregate_member = node->aggregate_member;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->place_symbol = base.place_symbol;
        result->place_ordinal = base.place_ordinal;
        result->place_is_parameter = base.place_is_parameter;
        result->place_projection = instruction.result;
        result->is_place = true;
        return true;
    }
    if (node->kind == R_HIR_INDEX_PLACE) {
        RMirExpressionResult base;
        RMirExpressionResult index;
        if ((node->child_count != UINT32_C(2)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &base) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U, &index) ||
            !base.is_place || index.is_place || (index.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_INDEX;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = base.place_symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = base.place_projection;
        instruction.operand1 = index.value;
        instruction.place_ordinal = base.place_ordinal;
        instruction.place_is_parameter = base.place_is_parameter;
        instruction.integer_value = node->integer_value;
        instruction.aggregate_member = node->aggregate_member;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->place_symbol = base.place_symbol;
        result->place_ordinal = base.place_ordinal;
        result->place_is_parameter = base.place_is_parameter;
        result->place_projection = instruction.result;
        result->is_place = true;
        return true;
    }
    if (node->kind == R_HIR_DEREF_PLACE) {
        RMirExpressionResult pointer;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &pointer)) {
            return false;
        }
        if (!pointer.is_place) {
            const RMirPlace *root = r_mir_find_place(build, node->symbol);
            const RSemanticSymbol *hidden =
                (node->deref_local == R_SYMBOL_ID_INVALID) ||
                        ((size_t)node->deref_local > build->frontend->semantic_symbol_count)
                    ? NULL
                    : &build->frontend->semantic_symbols[(size_t)node->deref_local - 1U];

            if (hidden != NULL) {
                /* The hidden local of an async dereference holds the borrow value; the
                   dereference reads it like any local borrow (R-BORROW-0005). */
                const uint32_t ordinal = build->next_local;

                if ((ordinal == UINT32_MAX) ||
                    !r_mir_push_place(build, node->deref_local, ordinal, false)) {
                    return false;
                }
                build->next_local += UINT32_C(1);
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = R_MIR_INSTRUCTION_LOCAL;
                instruction.span = node->span;
                instruction.type = hidden->type;
                instruction.symbol = node->deref_local;
                instruction.place_ordinal = ordinal;
                if (!r_mir_append_instruction(build, instruction)) {
                    return false;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = R_MIR_INSTRUCTION_STORE;
                instruction.span = node->span;
                instruction.type = hidden->type;
                instruction.symbol = node->deref_local;
                instruction.operand0 = pointer.value;
                instruction.place_ordinal = ordinal;
                if (!r_mir_append_instruction(build, instruction)) {
                    return false;
                }
                (void)memset(&pointer, 0, sizeof(pointer));
                pointer.is_place = true;
                pointer.place_symbol = node->deref_local;
                pointer.place_ordinal = ordinal;
            }
            if (!pointer.is_place) {
                pointer.place_symbol = node->symbol;
                pointer.place_ordinal = root == NULL ? UINT32_MAX : root->ordinal;
                pointer.place_is_parameter = root != NULL && root->is_parameter;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_DEREF;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.auxiliary_type = node->auxiliary_type;
        instruction.symbol = pointer.place_symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = pointer.place_projection;
        instruction.operand1 = pointer.is_place ? 0U : pointer.value;
        instruction.place_ordinal = pointer.place_ordinal;
        instruction.place_is_parameter = pointer.place_is_parameter;
        instruction.aggregate_member = node->aggregate_member;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->place_symbol = pointer.place_symbol;
        result->place_ordinal = pointer.place_ordinal;
        result->place_is_parameter = pointer.place_is_parameter;
        result->place_projection = instruction.result;
        result->is_place = true;
        return true;
    }
    if (node->kind == R_HIR_BORROW) {
        RMirExpressionResult place;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
            !place.is_place) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BORROW;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = place.place_symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = place.place_projection;
        instruction.place_ordinal = place.place_ordinal;
        instruction.place_is_parameter = place.place_is_parameter;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_LOAD) {
        RMirExpressionResult place;
        if (!r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
            !place.is_place) {
            return false;
        }
        if (r_mir_type_is_never(build->frontend, node->type)) {
            return r_mir_lower_never_read(build, node, result);
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_LOAD;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = place.place_symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operation = node->operation;
        instruction.place_ordinal = place.place_ordinal;
        instruction.place_is_parameter = place.place_is_parameter;
        instruction.operand1 = place.place_projection;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_MOVE) {
        RMirExpressionResult place;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
            !place.is_place) {
            return false;
        }
        if (r_mir_type_is_never(build->frontend, node->type)) {
            return r_mir_lower_never_read(build, node, result);
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_MOVE;
        instruction.operation =
            node->operation == R_TOKEN_KW_VARIANT ? R_TOKEN_KW_VARIANT : R_TOKEN_INVALID;
        instruction.span = node->span;
        instruction.type = node->type;
        if (r_semantic_type_crosses_await(build->frontend, node->type)) {
            instruction.borrow_origin = node->borrow_origin;
            instruction.borrow_origin_set = node->borrow_origin_set;
            instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        }
        instruction.symbol = place.place_symbol;
        instruction.place_ordinal = place.place_ordinal;
        instruction.place_is_parameter = place.place_is_parameter;
        instruction.operand1 = place.place_projection;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if ((node->kind == R_HIR_VARIANT_TAG) || (node->kind == R_HIR_VARIANT_PAYLOAD)) {
        /* R-STMT-0014: the tag and the transferred payload of an option value. */
        RMirExpressionResult tagged;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &tagged) ||
            tagged.is_place || (tagged.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = node->kind == R_HIR_VARIANT_TAG ? R_MIR_INSTRUCTION_VARIANT_TAG
                                                           : R_MIR_INSTRUCTION_VARIANT_PAYLOAD;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = tagged.value;
        instruction.integer_value = node->integer_value;
        instruction.operation = node->operation;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if ((node->kind == R_HIR_LITERAL) || (node->kind == R_HIR_ENUM_CONSTANT) ||
        node->kind == R_HIR_TYPE_QUERY) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = node->kind == R_HIR_TYPE_QUERY ? R_MIR_INSTRUCTION_TYPE_QUERY
                                                          : R_MIR_INSTRUCTION_CONSTANT;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.auxiliary_type = node->auxiliary_type;
        instruction.operation = node->operation;
        instruction.integer_value = node->integer_value;
        instruction.aggregate_member = node->aggregate_member;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_VALUE_SCOPE) {
        uint32_t index;
        if (node->integer_value >= node->child_count) {
            return false;
        }
        for (index = 0U; index < node->child_count; ++index) {
            RHirNodeId child = r_mir_hir_child(build->frontend, node, index);
            if (build->blocks[build->current_block].terminated) {
                /* R-TYPE-0007: a staged never operand ended the scope; the rest is
                   unreachable. */
                return (index > node->integer_value) ||
                       r_mir_lower_never_operand_completion(build, node, result);
            }
            if (index == node->integer_value) {
                if (!r_mir_lower_expression(build, child, depth + 1U, result)) {
                    return false;
                }
            } else if (!r_mir_lower_statement(build, child, depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    if (node->kind == R_HIR_STRING_LITERAL) {
        if ((node->aggregate_member == UINT32_C(0)) ||
            ((size_t)node->aggregate_member > build->frontend->intern_count)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STRING;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.integer_value = node->integer_value;
        instruction.aggregate_member = node->aggregate_member;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_DEFAULT_VALUE) {
        const RSemanticType *type = r_semantic_type(build->frontend, node->type);
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.span = node->span;
        instruction.type = node->type;
        if ((type != NULL) &&
            ((type->kind == R_SEMANTIC_TYPE_BORROW) || (type->kind == R_SEMANTIC_TYPE_OWN) ||
             (type->kind == R_SEMANTIC_TYPE_RAW) || (type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION)) &&
            ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U)) {
            instruction.operation = R_TOKEN_KW_NULL;
        }
        if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_STRUCT)) {
            instruction.kind = R_MIR_INSTRUCTION_AGGREGATE;
            instruction.integer_value = UINT64_C(1);
        } else if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY)) {
            instruction.kind = R_MIR_INSTRUCTION_ARRAY;
            instruction.integer_value = type->length;
        } else if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_OPTION)) {
            instruction.kind = R_MIR_INSTRUCTION_VARIANT;
        } else {
            instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
        }
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_ARRAY_INIT) {
        RMirValueVector values = {0};
        uint32_t child_index;
        bool success = true;
        for (child_index = 0U; child_index < node->child_count; ++child_index) {
            RMirExpressionResult value;
            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, child_index),
                                        depth + 1U,
                                        &value) ||
                value.is_place || (value.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &values, value.value)) {
                success = false;
                break;
            }
        }
        if (success) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_ARRAY;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.integer_value = node->integer_value;
            instruction.operand_count = (uint32_t)values.count;
            success = r_mir_append_operands(build, &values, &instruction.first_operand) &&
                      r_mir_new_value(build, &instruction.result) &&
                      r_mir_append_instruction(build, instruction);
            if (success) {
                result->value = instruction.result;
            }
        }
        r_context_free(build->frontend, values.items);
        return success;
    }
    if (node->kind == R_HIR_VARIANT) {
        RMirExpressionResult payload;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_VARIANT;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.integer_value = node->integer_value;
        if (node->child_count == UINT32_C(1)) {
            const RHirNode *payload_node = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                        depth + 1U,
                                        &payload) ||
                payload.is_place) {
                return false;
            }
            if ((payload.value == R_MIR_VALUE_ID_INVALID) && (payload_node != NULL) &&
                r_mir_type_is_never(build->frontend, payload_node->type)) {
                /* R-TYPE-0007: the variant of a never payload is never constructed. */
                return r_mir_lower_never_operand_completion(build, node, result);
            }
            if (payload.value == R_MIR_VALUE_ID_INVALID) {
                return false;
            }
            instruction.operand0 = payload.value;
        } else if (node->child_count != UINT32_C(0)) {
            return false;
        }
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_SLICE) {
        RMirExpressionResult place;
        RMirExpressionResult lower;
        RMirExpressionResult upper;

        if (node->operation == R_TOKEN_KW_ARRAY) {
            const RSemanticType *slice_type = r_semantic_type(build->frontend, node->type);
            const RSemanticType *input_type =
                r_semantic_type(build->frontend, node->auxiliary_type);
            const RSemanticType *array_type =
                input_type == NULL ? NULL : r_semantic_type(build->frontend, input_type->base);

            if ((node->child_count != UINT32_C(1)) || (slice_type == NULL) ||
                (slice_type->kind != R_SEMANTIC_TYPE_SLICE) || (input_type == NULL) ||
                (input_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                ((input_type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) ||
                (array_type == NULL) || (array_type->kind != R_SEMANTIC_TYPE_ARRAY) ||
                (array_type->base != slice_type->base) ||
                (((input_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) !=
                 ((slice_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U)) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                        depth + 1U,
                                        &place) ||
                place.is_place || (place.value == R_MIR_VALUE_ID_INVALID)) {
                return false;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_SLICE;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.symbol = node->symbol;
            instruction.borrow_origin = node->borrow_origin;
            instruction.borrow_origin_set = node->borrow_origin_set;
            instruction.borrow_origin_multiple = node->borrow_origin_multiple;
            instruction.operation = R_TOKEN_KW_ARRAY;
            instruction.operand0 = place.value;
            instruction.integer_value = UINT64_MAX;
            if (!r_mir_new_value(build, &instruction.result) ||
                !r_mir_append_instruction(build, instruction)) {
                return false;
            }
            result->value = instruction.result;
            return true;
        }

        if (((node->child_count != UINT32_C(1)) && (node->child_count != UINT32_C(2)) &&
             (node->child_count != UINT32_C(3))) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
            !place.is_place) {
            return false;
        }
        (void)memset(&lower, 0, sizeof(lower));
        (void)memset(&upper, 0, sizeof(upper));
        /* Two children describe `a[lo..]`: the upper bound is the length of the base. */
        if ((node->child_count >= UINT32_C(2)) &&
            (!r_mir_lower_expression(
                 build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U, &lower) ||
             lower.is_place || (lower.value == R_MIR_VALUE_ID_INVALID))) {
            return false;
        }
        if ((node->child_count == UINT32_C(3)) &&
            (!r_mir_lower_expression(
                 build, r_mir_hir_child(build->frontend, node, UINT32_C(2)), depth + 1U, &upper) ||
             upper.is_place || (upper.value == R_MIR_VALUE_ID_INVALID))) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_SLICE;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = place.place_symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = place.place_projection;
        instruction.operand1 = lower.value;
        instruction.operand2 = upper.value;
        instruction.place_ordinal = place.place_ordinal;
        instruction.place_is_parameter = place.place_is_parameter;
        instruction.integer_value = node->integer_value;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_LENGTH) {
        RMirExpressionResult place;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
            !place.is_place) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.integer_value = node->integer_value;
        if (node->integer_value != UINT64_MAX) {
            /* A dependent fixed-array length is known after closing the generic body.
             * Lower the place first so index checks and updates still occur once. */
            const RHirNode *base =
                r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, 0U));
            RTypeId base_type = base == NULL ? 0U : base->type;
            const RSemanticType *type = NULL;
            for (uint32_t level = 0U; level <= r_frontend_tree_depth_limit(build->frontend);
                 ++level) {
                if (base_type == 0U || base_type > build->frontend->semantic_type_count)
                    break;
                type = &build->frontend->semantic_types[base_type - 1U];
                if (type->kind != R_SEMANTIC_TYPE_CONST)
                    break;
                base_type = type->base;
            }
            if (type == NULL || type->kind != R_SEMANTIC_TYPE_FIXED_ARRAY || type->second != 0U ||
                type->length == 0U || type->length != node->integer_value)
                return false;
            instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
        } else {
            instruction.kind = R_MIR_INSTRUCTION_LENGTH;
            instruction.symbol = place.place_symbol;
            instruction.operand0 = place.place_projection;
            instruction.place_ordinal = place.place_ordinal;
            instruction.place_is_parameter = place.place_is_parameter;
        }
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_AGGREGATE_INIT) {
        RMirValueVector values = {0};
        const size_t pending_value_base = build->pending_aggregate_values.count;
        uint32_t *members = NULL;
        uint32_t child_index;
        bool success = true;
        if (node->child_count != UINT32_C(0)) {
            members =
                r_context_allocate(build->frontend, (size_t)node->child_count * sizeof(*members));
            if (members == NULL) {
                return false;
            }
        }
        for (child_index = 0U; child_index < node->child_count; ++child_index) {
            const RHirNode *field_init = r_mir_hir_node(
                build->frontend, r_mir_hir_child(build->frontend, node, child_index));
            RMirExpressionResult value;
            const RHirNode *value_node =
                field_init == NULL
                    ? NULL
                    : r_mir_hir_node(build->frontend,
                                     r_mir_hir_child(build->frontend, field_init, UINT32_C(0)));
            if ((field_init == NULL) || (field_init->kind != R_HIR_FIELD_INIT) ||
                (field_init->child_count != UINT32_C(1)) ||
                (field_init->aggregate_member == UINT32_C(0)) ||
                !r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, field_init, UINT32_C(0)),
                                        depth + 1U,
                                        &value) ||
                value.is_place) {
                success = false;
                break;
            }
            if ((value.value == R_MIR_VALUE_ID_INVALID) && (value_node != NULL) &&
                r_mir_type_is_never(build->frontend, value_node->type)) {
                /* R-TYPE-0007: an aggregate with a never field is never constructed. */
                success = r_mir_lower_never_operand_completion(build, node, result);
                build->pending_aggregate_values.count = pending_value_base;
                r_context_free(build->frontend, members);
                r_context_free(build->frontend, values.items);
                return success;
            }
            if ((value.value == R_MIR_VALUE_ID_INVALID) ||
                !r_mir_value_vector_push(build, &values, value.value) ||
                !r_mir_value_vector_push(build, &build->pending_aggregate_values, value.value)) {
                success = false;
                break;
            }
            members[values.count - 1U] = field_init->aggregate_member;
        }
        if (success) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_AGGREGATE;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.operand_count = (uint32_t)values.count;
            success = r_mir_append_aggregate_operands(
                          build, &values, members, &instruction.first_operand) &&
                      r_mir_new_value(build, &instruction.result) &&
                      r_mir_append_instruction(build, instruction);
            if (success) {
                result->value = instruction.result;
            }
        }
        build->pending_aggregate_values.count = pending_value_base;
        r_context_free(build->frontend, members);
        r_context_free(build->frontend, values.items);
        return success;
    }
    if (node->kind == R_HIR_NEW) {
        RMirExpressionResult payload;

        if ((node->child_count != UINT32_C(1)) ||
            ((node->operation != R_TOKEN_KW_OWN) && (node->operation != R_TOKEN_KW_ARC) &&
             (node->operation != R_TOKEN_KW_RC)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &payload) ||
            payload.is_place || (payload.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_NEW;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.auxiliary_type = node->auxiliary_type;
        instruction.operation = node->operation;
        instruction.operand0 = payload.value;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_TRY) {
        const RHirNode *operand_node =
            r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
        const RSemanticType *tagged_type;
        RMirExpressionResult tagged;
        RTypeId u32_type;
        RTypeId bool_type;
        RMirValueId tag_value;
        RMirValueId failure_tag;
        RMirValueId failure_condition;
        size_t failure_block;
        size_t success_block;
        RMirInstruction branch;

        if ((node->child_count == UINT32_C(0)) || (operand_node == NULL) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &tagged) ||
            tagged.is_place || (tagged.value == R_MIR_VALUE_ID_INVALID) ||
            !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_U32, &u32_type) ||
            !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_BOOL, &bool_type)) {
            return false;
        }
        tagged_type = r_semantic_type(build->frontend, operand_node->type);
        if ((tagged_type == NULL) || ((tagged_type->kind != R_SEMANTIC_TYPE_OPTION) &&
                                      (tagged_type->kind != R_SEMANTIC_TYPE_RESULT))) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_VARIANT_TAG;
        instruction.span = node->span;
        instruction.type = u32_type;
        instruction.operand0 = tagged.value;
        if (!r_mir_new_value(build, &tag_value)) {
            return false;
        }
        instruction.result = tag_value;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
        instruction.span = node->span;
        instruction.type = u32_type;
        instruction.integer_value =
            tagged_type->kind == R_SEMANTIC_TYPE_RESULT ? UINT64_C(1) : UINT64_C(0);
        if (!r_mir_new_value(build, &failure_tag)) {
            return false;
        }
        instruction.result = failure_tag;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BINARY;
        instruction.span = node->span;
        instruction.type = bool_type;
        instruction.operation = R_TOKEN_EQUAL_EQUAL;
        instruction.operand0 = tag_value;
        instruction.operand1 = failure_tag;
        if (!r_mir_new_value(build, &failure_condition)) {
            return false;
        }
        instruction.result = failure_condition;
        if (!r_mir_append_instruction(build, instruction) ||
            !r_mir_add_block(build, &failure_block) || !r_mir_add_block(build, &success_block)) {
            return false;
        }
        (void)memset(&branch, 0, sizeof(branch));
        branch.kind = R_MIR_INSTRUCTION_BRANCH;
        branch.span = node->span;
        branch.operand0 = failure_condition;
        branch.target0 = (RMirBlockId)(failure_block + 1U);
        branch.target1 = (RMirBlockId)(success_block + 1U);
        if (!r_mir_append_instruction(build, branch)) {
            return false;
        }
        build->current_block = failure_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_VARIANT;
        instruction.span = node->span;
        instruction.type = node->auxiliary_type;
        instruction.integer_value =
            tagged_type->kind == R_SEMANTIC_TYPE_RESULT ? UINT64_C(1) : UINT64_C(0);
        if (tagged_type->kind == R_SEMANTIC_TYPE_RESULT) {
            RMirInstruction payload;
            RMirValueId payload_value;
            (void)memset(&payload, 0, sizeof(payload));
            payload.kind = R_MIR_INSTRUCTION_VARIANT_PAYLOAD;
            payload.span = node->span;
            payload.type = tagged_type->second;
            payload.operand0 = tagged.value;
            payload.integer_value = UINT64_C(1);
            if (!r_mir_new_value(build, &payload_value)) {
                return false;
            }
            payload.result = payload_value;
            if (!r_mir_append_instruction(build, payload)) {
                return false;
            }
            instruction.operand0 = payload_value;
        }
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        {
            uint32_t cleanup_index;
            for (cleanup_index = UINT32_C(1); cleanup_index < node->child_count; ++cleanup_index) {
                const RHirNodeId cleanup_id = r_mir_hir_child(build->frontend, node, cleanup_index);
                const RHirNode *cleanup = r_mir_hir_node(build->frontend, cleanup_id);
                if ((cleanup == NULL) || (cleanup->kind != R_HIR_DROP) ||
                    !r_mir_lower_statement(build, cleanup_id, depth + 1U)) {
                    return false;
                }
            }
        }
        {
            RMirInstruction return_instruction;
            (void)memset(&return_instruction, 0, sizeof(return_instruction));
            return_instruction.kind = R_MIR_INSTRUCTION_RETURN;
            return_instruction.span = node->span;
            return_instruction.type = node->auxiliary_type;
            return_instruction.operand0 = instruction.result;
            if (!r_mir_append_instruction(build, return_instruction)) {
                return false;
            }
        }
        build->current_block = success_block;
        if ((tagged_type->kind == R_SEMANTIC_TYPE_RESULT) &&
            (r_semantic_type(build->frontend, tagged_type->base)->kind == R_SEMANTIC_TYPE_VOID)) {
            result->value = R_MIR_VALUE_ID_INVALID;
            return true;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_VARIANT_PAYLOAD;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.operand0 = tagged.value;
        instruction.integer_value =
            tagged_type->kind == R_SEMANTIC_TYPE_RESULT ? UINT64_C(0) : UINT64_C(1);
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_UNARY) {
        RMirExpressionResult operand;
        if (!r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &operand) ||
            operand.is_place || (operand.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_UNARY;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.operation = node->operation;
        instruction.operand0 = operand.value;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_CAST) {
        const RHirNodeId operand_id = r_mir_hir_child(build->frontend, node, UINT32_C(0));
        const RHirNode *operand_node = r_mir_hir_node(build->frontend, operand_id);
        const RSemanticType *target_type = r_semantic_type(build->frontend, node->type);

        if ((node->child_count == UINT32_C(1)) && (target_type != NULL) &&
            (target_type->kind == R_SEMANTIC_TYPE_RAW) && (operand_node != NULL) &&
            (operand_node->kind == R_HIR_BORROW) && (operand_node->child_count == UINT32_C(1))) {
            RMirExpressionResult place;

            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, operand_node, UINT32_C(0)),
                                        depth + UINT32_C(1),
                                        &place) ||
                !place.is_place) {
                return false;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_RAW_ADDRESS;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.symbol = place.place_symbol;
            instruction.operand0 = place.place_projection;
            instruction.place_ordinal = place.place_ordinal;
            instruction.place_is_parameter = place.place_is_parameter;
            if (!r_mir_new_value(build, &instruction.result) ||
                !r_mir_append_instruction(build, instruction)) {
                return false;
            }
            result->value = instruction.result;
            return true;
        }
        RMirExpressionResult operand;
        const RSemanticType *operand_type =
            operand_node == NULL ? NULL : r_semantic_type(build->frontend, operand_node->type);
        if ((operand_type != NULL) && (operand_type->kind == R_SEMANTIC_TYPE_NEVER) &&
            (node->operation == R_TOKEN_INVALID)) {
            /* R-EXPR-0015: never converts to any type. The operand produces no value, so the
               conversion defines an unreachable placeholder without an operand. */
            if (!r_mir_lower_expression(build, operand_id, depth + 1U, &operand) ||
                operand.is_place || (operand.value != R_MIR_VALUE_ID_INVALID)) {
                return false;
            }
            if (build->blocks[build->current_block].terminated) {
                size_t unreachable_block;

                /* The rest of the enclosing expression lowers into a block without entries. */
                if (!r_mir_add_block(build, &unreachable_block)) {
                    return false;
                }
                build->current_block = unreachable_block;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_CAST;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.operation = R_TOKEN_INVALID;
            if (!r_mir_new_value(build, &instruction.result) ||
                !r_mir_append_instruction(build, instruction)) {
                return false;
            }
            result->value = instruction.result;
            return true;
        }
        if (!r_mir_lower_expression(build, operand_id, depth + 1U, &operand) || operand.is_place ||
            (operand.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_CAST;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operation = node->operation;
        instruction.operand0 = operand.value;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_DISCARD) {
        const RHirNode *operand_node;
        RMirExpressionResult operand;

        if (node->child_count != UINT32_C(1)) {
            return false;
        }
        operand_node =
            r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
        if ((operand_node == NULL) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &operand) ||
            operand.is_place || (operand.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_DISCARD;
        instruction.span = node->span;
        instruction.type = operand_node->type;
        instruction.operand0 = operand.value;
        return r_mir_append_instruction(build, instruction);
    }
    if (node->kind == R_HIR_CONDITIONAL) {
        return r_mir_lower_conditional(build, node, depth, result);
    }
    if (node->kind == R_HIR_BINARY) {
        RMirExpressionResult left;
        RMirExpressionResult right;
        if ((node->operation == R_TOKEN_AMP_AMP) || (node->operation == R_TOKEN_PIPE_PIPE)) {
            return r_mir_lower_short_circuit(build, node, depth, result);
        }
        if (!r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &left) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U, &right) ||
            left.is_place || right.is_place || (left.value == R_MIR_VALUE_ID_INVALID) ||
            (right.value == R_MIR_VALUE_ID_INVALID)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BINARY;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.operation = node->operation;
        instruction.operand0 = left.value;
        instruction.operand1 = right.value;
        if (!r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if (node->kind == R_HIR_FUNCTION_ADDRESS) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_FUNCTION_ADDRESS;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = node->symbol;
        if (node->child_count != UINT32_C(0) || !r_mir_new_value(build, &instruction.result) ||
            !r_mir_append_instruction(build, instruction)) {
            return false;
        }
        result->value = instruction.result;
        return true;
    }
    if ((node->kind == R_HIR_CALL) || (node->kind == R_HIR_INDIRECT_CALL) ||
        (node->kind == R_HIR_ASYNC_START)) {
        return r_mir_lower_call(build, node, depth, result);
    }
    if (node->kind == R_HIR_TASK_SCOPE_WAIT || node->kind == R_HIR_TASK_SCOPE_ENTER ||
        node->kind == R_HIR_TASK_SCOPE_CLOSE)
        return r_mir_lower_task_scope(build, node, depth, result);
    if (node->kind == R_HIR_AWAIT) {
        return r_mir_lower_await(build, node, depth, result);
    }
    if (node->kind == R_HIR_STANDARD_CALL) {
        return r_mir_lower_standard_call(build, node, depth, result);
    }
    if ((node->kind == R_HIR_ASSIGN) || (node->kind == R_HIR_COMPOUND_ASSIGN)) {
        return r_mir_lower_assignment(build, node, depth, result);
    }
    return false;
}

static bool r_mir_lower_statement(RMirBuildContext *build, RHirNodeId node_id, uint32_t depth);

static bool r_mir_lower_block_range(RMirBuildContext *build,
                                    const RHirNode *node,
                                    uint32_t first_child,
                                    uint32_t end_child,
                                    uint32_t depth) {
    uint32_t child_index;

    if ((node == NULL) || (node->kind != R_HIR_BLOCK) || (first_child > end_child) ||
        (end_child > node->child_count)) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (child_index = first_child; child_index < end_child; ++child_index) {
        if (build->blocks[build->current_block].terminated) {
            break;
        }
        if (!r_mir_lower_statement(
                build, r_mir_hir_child(build->frontend, node, child_index), depth + 1U)) {
            return false;
        }
    }
    return true;
}

static uint32_t r_mir_block_trailing_drop_start(const RMirBuildContext *build,
                                                const RHirNode *node) {
    uint32_t child_index = node == NULL ? UINT32_C(0) : node->child_count;

    /* Stage the outgoing completion before both implicit and explicit terminal cleanup drops. */
    while (child_index != UINT32_C(0)) {
        const RHirNode *child = r_mir_hir_node(
            build->frontend, r_mir_hir_child(build->frontend, node, child_index - UINT32_C(1)));

        if ((child == NULL) || (child->kind != R_HIR_DROP)) {
            break;
        }
        child_index -= UINT32_C(1);
    }
    return child_index;
}

static bool r_mir_lower_block(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    return r_mir_lower_block_range(build, node, UINT32_C(0), node->child_count, depth);
}

static bool r_mir_lower_local(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    RMirInstruction instruction;
    uint32_t ordinal;

    if (build->next_local == UINT32_MAX) {
        build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    ordinal = build->next_local;
    build->next_local += 1U;
    if (!r_mir_push_place(build, node->symbol, ordinal, false)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_LOCAL;
    instruction.span = node->span;
    instruction.type = node->type;
    instruction.symbol = node->symbol;
    instruction.task_scope = node->task_scope;
    instruction.borrow_origin = node->borrow_origin;
    instruction.borrow_origin_set = node->borrow_origin_set;
    instruction.borrow_origin_multiple = node->borrow_origin_multiple;
    instruction.place_ordinal = ordinal;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    if (build->frontend->semantic_symbols[node->symbol - 1U].is_static) {
        return true;
    }
    if (node->child_count != 0U) {
        RMirExpressionResult initializer;
        /* R-TYPE-0007: the initializer of a never object does not complete; nothing is stored. */
        const bool never_object = r_mir_type_is_never(build->frontend, node->type);

        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(build,
                                    r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                    depth + 1U,
                                    &initializer) ||
            initializer.is_place ||
            ((initializer.value == R_MIR_VALUE_ID_INVALID) != never_object)) {
            return false;
        }
        if (never_object) {
            return true;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STORE;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = node->symbol;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        instruction.operand0 = initializer.value;
        instruction.place_ordinal = ordinal;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
    }
    return true;
}

static bool r_mir_lower_if(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    RMirExpressionResult condition;
    RMirInstruction branch;
    const RHirNode *then_node;
    const RHirNode *else_node = NULL;
    size_t then_block;
    size_t else_block;
    size_t merge_block;
    bool merge_reachable;

    if ((node->child_count < UINT32_C(2)) || (node->child_count > UINT32_C(3)) ||
        !r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &condition) ||
        condition.is_place || (condition.value == R_MIR_VALUE_ID_INVALID) ||
        !r_mir_add_block(build, &then_block)) {
        return false;
    }
    if (node->child_count == UINT32_C(3)) {
        if (!r_mir_add_block(build, &else_block)) {
            return false;
        }
        merge_reachable = false;
    } else {
        else_block = SIZE_MAX;
        merge_reachable = true;
    }
    if (!r_mir_add_block(build, &merge_block)) {
        return false;
    }
    (void)memset(&branch, 0, sizeof(branch));
    branch.kind = R_MIR_INSTRUCTION_BRANCH;
    branch.span = node->span;
    branch.operand0 = condition.value;
    branch.target0 = (RMirBlockId)(then_block + 1U);
    branch.target1 = (RMirBlockId)(((else_block == SIZE_MAX) ? merge_block : else_block) + 1U);
    if (!r_mir_append_instruction(build, branch)) {
        return false;
    }
    then_node =
        r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(1)));
    if (then_node == NULL) {
        return false;
    }
    build->current_block = then_block;
    if (!r_mir_lower_statement(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U)) {
        return false;
    }
    if (!build->blocks[build->current_block].terminated) {
        if (!r_mir_append_jump(build, then_node->span, merge_block)) {
            return false;
        }
        merge_reachable = true;
    }
    if (else_block != SIZE_MAX) {
        else_node =
            r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(2)));
        if (else_node == NULL) {
            return false;
        }
        build->current_block = else_block;
        if (!r_mir_lower_statement(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(2)), depth + 1U)) {
            return false;
        }
        if (!build->blocks[build->current_block].terminated) {
            if (!r_mir_append_jump(build, else_node->span, merge_block)) {
                return false;
            }
            merge_reachable = true;
        }
    }
    build->current_block = merge_block;
    if (!merge_reachable) {
        RMirInstruction unreachable;

        (void)memset(&unreachable, 0, sizeof(unreachable));
        unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
        unreachable.span = node->span;
        return r_mir_append_instruction(build, unreachable);
    }
    return true;
}

static bool r_mir_push_loop(RMirBuildContext *build, size_t break_target, size_t continue_target) {
    if (!r_grow_array(build->frontend,
                      (void **)&build->loops,
                      &build->loop_capacity,
                      sizeof(*build->loops),
                      build->loop_count + 1U)) {
        return false;
    }
    build->loops[build->loop_count].break_target = break_target;
    build->loops[build->loop_count].continue_target = continue_target;
    build->loops[build->loop_count].finally_count = build->finally_count;
    build->loop_count += 1U;
    return true;
}

static bool r_mir_lower_while(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    RMirExpressionResult condition;
    RMirInstruction branch;
    const RHirNode *body_node;
    size_t condition_block;
    size_t body_block;
    size_t exit_block;
    const size_t previous_break_target = build->break_target;
    const size_t previous_continue_target = build->continue_target;
    const size_t previous_break_finally_count = build->break_finally_count;
    const size_t previous_continue_finally_count = build->continue_finally_count;
    bool body_lowered;

    if ((node->child_count != UINT32_C(2)) || !r_mir_add_block(build, &condition_block) ||
        !r_mir_add_block(build, &body_block) || !r_mir_add_block(build, &exit_block) ||
        !r_mir_append_jump(build, node->span, condition_block)) {
        return false;
    }
    build->current_block = condition_block;
    if (!r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &condition) ||
        condition.is_place || (condition.value == R_MIR_VALUE_ID_INVALID)) {
        return false;
    }
    (void)memset(&branch, 0, sizeof(branch));
    branch.kind = R_MIR_INSTRUCTION_BRANCH;
    branch.span = node->span;
    branch.operand0 = condition.value;
    branch.target0 = (RMirBlockId)(body_block + 1U);
    branch.target1 = (RMirBlockId)(exit_block + 1U);
    if (!r_mir_append_instruction(build, branch)) {
        return false;
    }
    body_node =
        r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(1)));
    if (body_node == NULL) {
        return false;
    }
    build->current_block = body_block;
    build->break_target = exit_block;
    build->continue_target = condition_block;
    build->break_finally_count = build->finally_count;
    build->continue_finally_count = build->finally_count;
    body_lowered = r_mir_push_loop(build, exit_block, condition_block) &&
                   r_mir_lower_statement(
                       build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U);
    build->loop_count -= build->loop_count != 0U ? 1U : 0U;
    build->break_target = previous_break_target;
    build->continue_target = previous_continue_target;
    build->break_finally_count = previous_break_finally_count;
    build->continue_finally_count = previous_continue_finally_count;
    if (!body_lowered || (!build->blocks[build->current_block].terminated &&
                          !r_mir_append_jump(build, body_node->span, condition_block))) {
        return false;
    }
    build->current_block = exit_block;
    return true;
}

static bool r_mir_lower_for(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    RMirExpressionResult condition;
    RMirInstruction branch;
    const RHirNode *body_node;
    const RHirNode *iteration_node;
    size_t condition_block;
    size_t body_block;
    size_t iteration_block;
    size_t exit_block;
    const size_t previous_break_target = build->break_target;
    const size_t previous_continue_target = build->continue_target;
    const size_t previous_break_finally_count = build->break_finally_count;
    const size_t previous_continue_finally_count = build->continue_finally_count;
    bool body_lowered;

    if ((node->child_count != UINT32_C(3)) || !r_mir_add_block(build, &condition_block) ||
        !r_mir_add_block(build, &body_block) || !r_mir_add_block(build, &iteration_block) ||
        !r_mir_add_block(build, &exit_block) ||
        !r_mir_append_jump(build, node->span, condition_block)) {
        return false;
    }
    build->current_block = condition_block;
    if (!r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &condition) ||
        condition.is_place || (condition.value == R_MIR_VALUE_ID_INVALID)) {
        return false;
    }
    (void)memset(&branch, 0, sizeof(branch));
    branch.kind = R_MIR_INSTRUCTION_BRANCH;
    branch.span = node->span;
    branch.operand0 = condition.value;
    branch.target0 = (RMirBlockId)(body_block + 1U);
    branch.target1 = (RMirBlockId)(exit_block + 1U);
    if (!r_mir_append_instruction(build, branch)) {
        return false;
    }
    body_node =
        r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(1)));
    iteration_node =
        r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(2)));
    if ((body_node == NULL) || (iteration_node == NULL)) {
        return false;
    }
    build->current_block = body_block;
    build->break_target = exit_block;
    build->continue_target = iteration_block;
    build->break_finally_count = build->finally_count;
    build->continue_finally_count = build->finally_count;
    body_lowered = r_mir_push_loop(build, exit_block, iteration_block) &&
                   r_mir_lower_statement(
                       build, r_mir_hir_child(build->frontend, node, UINT32_C(1)), depth + 1U);
    build->loop_count -= build->loop_count != 0U ? 1U : 0U;
    build->break_target = previous_break_target;
    build->continue_target = previous_continue_target;
    build->break_finally_count = previous_break_finally_count;
    build->continue_finally_count = previous_continue_finally_count;
    if (!body_lowered || (!build->blocks[build->current_block].terminated &&
                          !r_mir_append_jump(build, body_node->span, iteration_block))) {
        return false;
    }
    build->current_block = iteration_block;
    if (!r_mir_lower_statement(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(2)), depth + 1U) ||
        (!build->blocks[build->current_block].terminated &&
         !r_mir_append_jump(build, iteration_node->span, condition_block))) {
        return false;
    }
    build->current_block = exit_block;
    return true;
}

static bool r_mir_type_kind_is_integer(RSemanticTypeKind kind) {
    return (kind >= R_SEMANTIC_TYPE_I8) && (kind <= R_SEMANTIC_TYPE_USIZE);
}

static bool r_mir_lower_switch_binding(RMirBuildContext *build,
                                       const RHirNode *case_node,
                                       RMirValueId tagged_value) {
    RMirInstruction instruction;
    if (case_node->symbol != R_SYMBOL_ID_INVALID) {
        const RSemanticType *binding_type = r_semantic_type(build->frontend, case_node->type);
        uint32_t ordinal;
        RMirValueId payload_value;
        if ((binding_type == NULL) ||
            /* A moved payload may hold views of any shape (R-STMT-0010, R-BORROW-0018); the
               semantic pass has proven it a supported Copy or Move value. */
            (case_node->is_move
                 ? (case_node->type != case_node->auxiliary_type)
                 : ((binding_type->kind != R_SEMANTIC_TYPE_BORROW) ||
                    ((binding_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U) ||
                    ((binding_type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) ||
                    (binding_type->base != case_node->auxiliary_type) ||
                    /* A payload that holds views borrows from every origin of the scrutinee and
                       of its content; a generic instance keeps only that origin set. */
                    ((case_node->borrow_origin == R_SYMBOL_ID_INVALID) &&
                     (case_node->borrow_origin_set == UINT32_C(0)) &&
                     !case_node->borrow_origin_multiple)))) {
            return false;
        }
        if (build->next_local == UINT32_MAX) {
            build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
            return false;
        }
        ordinal = build->next_local;
        build->next_local += 1U;
        if (!r_mir_push_place(build, case_node->symbol, ordinal, false)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_LOCAL;
        instruction.span = case_node->span;
        instruction.type = case_node->type;
        instruction.symbol = case_node->symbol;
        instruction.borrow_origin = case_node->borrow_origin;
        instruction.borrow_origin_set = case_node->borrow_origin_set;
        instruction.borrow_origin_multiple = case_node->borrow_origin_multiple;
        instruction.place_ordinal = ordinal;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_VARIANT_PAYLOAD;
        instruction.span = case_node->span;
        instruction.type = case_node->type;
        instruction.borrow_origin = case_node->borrow_origin;
        instruction.borrow_origin_set = case_node->borrow_origin_set;
        instruction.borrow_origin_multiple = case_node->borrow_origin_multiple;
        instruction.operand0 = tagged_value;
        instruction.integer_value = case_node->integer_value;
        instruction.operation = case_node->is_move ? R_TOKEN_KW_MOVE : R_TOKEN_AMP;
        if (!r_mir_new_value(build, &payload_value)) {
            return false;
        }
        instruction.result = payload_value;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STORE;
        instruction.span = case_node->span;
        instruction.type = case_node->type;
        instruction.symbol = case_node->symbol;
        instruction.borrow_origin = case_node->borrow_origin;
        instruction.borrow_origin_set = case_node->borrow_origin_set;
        instruction.borrow_origin_multiple = case_node->borrow_origin_multiple;
        instruction.operand0 = payload_value;
        instruction.place_ordinal = ordinal;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
    }
    return true;
}

/* R-STMT-0006: a fieldless enum the standard library implements natively; its hidden schema
   is an untagged enum aggregate of that standard type. */
static bool r_mir_type_is_standard_fieldless_enum(const RFrontendContext *context,
                                                  RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD)) {
        return false;
    }
    for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if ((aggregate->type == type_id) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
            !aggregate->is_tagged) {
            return true;
        }
    }
    return false;
}

static bool
r_mir_lower_integer_switch(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    RMirExpressionResult value;
    const RSemanticType *switch_type = r_semantic_type(build->frontend, node->auxiliary_type);
    RTypeId bool_type;
    RTypeId comparison_type = node->auxiliary_type;
    const bool user_tagged = r_semantic_tagged_enum(build->frontend, node->auxiliary_type) != NULL;
    RMirValueId tagged_value = R_MIR_VALUE_ID_INVALID;
    RMirScalarCase *cases = NULL;
    size_t case_count = 0U;
    size_t case_capacity = 0U;
    size_t constant_count = 0U;
    size_t default_block = SIZE_MAX;
    size_t merge_block;
    size_t case_index;
    size_t comparison_index = 0U;
    const size_t previous_break_target = build->break_target;
    const size_t previous_break_finally_count = build->break_finally_count;
    bool success = false;
    uint32_t child_index;

    if ((switch_type == NULL) ||
        (!r_mir_type_kind_is_integer(switch_type->kind) &&
         (switch_type->kind != R_SEMANTIC_TYPE_CHAR) &&
         (switch_type->kind != R_SEMANTIC_TYPE_ENUM) &&
         !r_mir_type_is_standard_fieldless_enum(build->frontend, node->auxiliary_type)) ||
        (node->child_count < UINT32_C(2)) ||
        !r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &value) ||
        value.is_place || (value.value == R_MIR_VALUE_ID_INVALID) ||
        !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_BOOL, &bool_type)) {
        goto cleanup;
    }
    if (user_tagged) {
        RMirInstruction instruction = {0};
        tagged_value = value.value;
        if (!r_semantic_type_from_token(build->frontend, R_TOKEN_KW_U32, &comparison_type) ||
            !r_mir_new_value(build, &value.value)) {
            goto cleanup;
        }
        instruction.kind = R_MIR_INSTRUCTION_VARIANT_TAG;
        instruction.span = node->span;
        instruction.type = comparison_type;
        instruction.operand0 = tagged_value;
        instruction.result = value.value;
        if (!r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
    }
    for (child_index = UINT32_C(1); child_index < node->child_count; ++child_index) {
        const RHirNodeId case_id = r_mir_hir_child(build->frontend, node, child_index);
        const RHirNode *case_node = r_mir_hir_node(build->frontend, case_id);
        size_t block;

        if ((case_node == NULL) || (case_node->kind != R_HIR_CASE) ||
            (case_node->child_count != UINT32_C(1)) ||
            ((case_node->operation != R_TOKEN_KW_BREAK) &&
             (case_node->operation != R_TOKEN_KW_CONTINUE) &&
             (case_node->operation != R_TOKEN_KW_RETURN) &&
             (case_node->operation != R_TOKEN_KW_THROW) &&
             (case_node->operation != R_TOKEN_KW_FALLTHROUGH)) ||
            (!user_tagged && (case_node->symbol != R_SYMBOL_ID_INVALID)) ||
            ((case_node->case_pattern !=
              (user_tagged ? R_HIR_CASE_PATTERN_VARIANT : R_HIR_CASE_PATTERN_CONSTANT)) &&
             (case_node->case_pattern != R_HIR_CASE_PATTERN_DEFAULT)) ||
            ((case_node->case_pattern == R_HIR_CASE_PATTERN_CONSTANT) &&
             (case_node->auxiliary_type != node->auxiliary_type)) ||
            !r_mir_add_block(build, &block) ||
            !r_grow_array(build->frontend,
                          (void **)&cases,
                          &case_capacity,
                          sizeof(*cases),
                          case_count + 1U)) {
            goto cleanup;
        }
        cases[case_count].node = case_id;
        cases[case_count].block = block;
        case_count += 1U;
        if (case_node->case_pattern == R_HIR_CASE_PATTERN_DEFAULT) {
            if (default_block != SIZE_MAX) {
                goto cleanup;
            }
            default_block = block;
        } else {
            constant_count += 1U;
        }
    }
    /* A checked exhaustive nominal enum, including a fieldless enum of the library, has no
       other inhabited value. */
    if ((default_block == SIZE_MAX) &&
        ((switch_type->kind == R_SEMANTIC_TYPE_ENUM) ||
         r_mir_type_is_standard_fieldless_enum(build->frontend, node->auxiliary_type)) &&
        (case_count != 0U)) {
        default_block = cases[case_count - 1U].block;
    }
    if ((default_block == SIZE_MAX) || !r_mir_add_block(build, &merge_block)) {
        goto cleanup;
    }
    if (constant_count == 0U) {
        if (!r_mir_append_jump(build, node->span, default_block)) {
            goto cleanup;
        }
    } else {
        for (case_index = 0U; case_index < case_count; ++case_index) {
            const RHirNode *case_node = r_mir_hir_node(build->frontend, cases[case_index].node);
            RMirInstruction instruction;
            RMirValueId constant;
            RMirValueId equal;
            size_t next_test = default_block;

            if ((case_node == NULL) ||
                (case_node->case_pattern !=
                 (user_tagged ? R_HIR_CASE_PATTERN_VARIANT : R_HIR_CASE_PATTERN_CONSTANT))) {
                continue;
            }
            comparison_index += 1U;
            if ((comparison_index < constant_count) && !r_mir_add_block(build, &next_test)) {
                goto cleanup;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
            instruction.span = case_node->span;
            instruction.type = comparison_type;
            instruction.integer_value = case_node->integer_value;
            if (!r_mir_new_value(build, &constant)) {
                goto cleanup;
            }
            instruction.result = constant;
            if (!r_mir_append_instruction(build, instruction)) {
                goto cleanup;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_BINARY;
            instruction.span = case_node->span;
            instruction.type = bool_type;
            instruction.operation = R_TOKEN_EQUAL_EQUAL;
            instruction.operand0 = value.value;
            instruction.operand1 = constant;
            if (!r_mir_new_value(build, &equal)) {
                goto cleanup;
            }
            instruction.result = equal;
            if (!r_mir_append_instruction(build, instruction)) {
                goto cleanup;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_BRANCH;
            instruction.span = case_node->span;
            instruction.operand0 = equal;
            instruction.target0 = (RMirBlockId)(cases[case_index].block + 1U);
            instruction.target1 = (RMirBlockId)(next_test + 1U);
            if (!r_mir_append_instruction(build, instruction)) {
                goto cleanup;
            }
            if (comparison_index < constant_count) {
                build->current_block = next_test;
            }
        }
    }
    build->break_target = merge_block;
    build->break_finally_count = build->finally_count;
    for (case_index = 0U; case_index < case_count; ++case_index) {
        const RHirNode *case_node = r_mir_hir_node(build->frontend, cases[case_index].node);
        const RHirNode *block_node;

        build->current_block = cases[case_index].block;
        block_node = case_node == NULL
                         ? NULL
                         : r_mir_hir_node(build->frontend,
                                          r_mir_hir_child(build->frontend, case_node, UINT32_C(0)));
        if ((case_node == NULL) || (block_node == NULL) ||
            ((case_node->operation == R_TOKEN_KW_FALLTHROUGH) &&
             ((case_index + 1U) >= case_count)) ||
            (user_tagged && !r_mir_lower_switch_binding(build, case_node, tagged_value)) ||
            !r_mir_lower_statement(
                build, r_mir_hir_child(build->frontend, case_node, UINT32_C(0)), depth + 1U) ||
            (!build->blocks[build->current_block].terminated &&
             !r_mir_append_jump(build,
                                block_node->span,
                                case_node->operation == R_TOKEN_KW_FALLTHROUGH
                                    ? cases[case_index + 1U].block
                                    : merge_block))) {
            goto cleanup;
        }
    }
    build->current_block = merge_block;
    success = true;

cleanup:
    build->break_target = previous_break_target;
    build->break_finally_count = previous_break_finally_count;
    r_context_free(build->frontend, cases);
    return success;
}

static bool
r_mir_lower_tagged_switch(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    const RSemanticType *tagged_type = r_semantic_type(build->frontend, node->auxiliary_type);
    const uint32_t standard_variant_count =
        r_mir_standard_outcome_variant_count(build->frontend, tagged_type);
    const uint32_t variant_count =
        standard_variant_count == UINT32_C(0) ? UINT32_C(2) : standard_variant_count;
    RMirExpressionResult tagged;
    RTypeId u32_type;
    RTypeId bool_type;
    RMirValueId tag_value;
    RMirValueId zero_value;
    RMirValueId is_zero;
    RHirNodeId cases[4];
    size_t case_blocks[4];
    size_t case_count = 0U;
    size_t merge_block;
    size_t tag0_block = SIZE_MAX;
    size_t tag1_block = SIZE_MAX;
    size_t tag2_block = SIZE_MAX;
    size_t tag3_block = SIZE_MAX;
    size_t default_block = SIZE_MAX;
    uint32_t child_index;
    RMirInstruction instruction;

    if ((tagged_type == NULL) ||
        ((tagged_type->kind != R_SEMANTIC_TYPE_OPTION) &&
         (tagged_type->kind != R_SEMANTIC_TYPE_RESULT) &&
         (standard_variant_count == UINT32_C(0))) ||
        (node->child_count < UINT32_C(2)) ||
        ((standard_variant_count == UINT32_C(0)) && (node->child_count > UINT32_C(4))) ||
        ((standard_variant_count != UINT32_C(0)) &&
         (node->child_count != (standard_variant_count + UINT32_C(1)))) ||
        !r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &tagged) ||
        tagged.is_place || (tagged.value == R_MIR_VALUE_ID_INVALID) ||
        !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_U32, &u32_type) ||
        !r_semantic_type_from_token(build->frontend, R_TOKEN_KW_BOOL, &bool_type)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_VARIANT_TAG;
    instruction.span = node->span;
    instruction.type = u32_type;
    instruction.operand0 = tagged.value;
    if (!r_mir_new_value(build, &tag_value)) {
        return false;
    }
    instruction.result = tag_value;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
    instruction.span = node->span;
    instruction.type = u32_type;
    if (!r_mir_new_value(build, &zero_value)) {
        return false;
    }
    instruction.result = zero_value;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = R_MIR_INSTRUCTION_BINARY;
    instruction.span = node->span;
    instruction.type = bool_type;
    instruction.operation = R_TOKEN_EQUAL_EQUAL;
    instruction.operand0 = tag_value;
    instruction.operand1 = zero_value;
    if (!r_mir_new_value(build, &is_zero)) {
        return false;
    }
    instruction.result = is_zero;
    if (!r_mir_append_instruction(build, instruction)) {
        return false;
    }
    for (child_index = UINT32_C(1); child_index < node->child_count; ++child_index) {
        const RHirNodeId case_id = r_mir_hir_child(build->frontend, node, child_index);
        const RHirNode *case_node = r_mir_hir_node(build->frontend, case_id);
        size_t block_index;
        if ((case_node == NULL) || (case_node->kind != R_HIR_CASE) ||
            (case_count >= (sizeof(cases) / sizeof(cases[0]))) ||
            !r_mir_add_block(build, &block_index)) {
            return false;
        }
        cases[case_count] = case_id;
        case_blocks[case_count] = block_index;
        case_count += 1U;
        if ((case_node->case_pattern == R_HIR_CASE_PATTERN_VARIANT) &&
            (case_node->integer_value == UINT64_C(0))) {
            if (tag0_block != SIZE_MAX) {
                return false;
            }
            tag0_block = block_index;
        } else if ((case_node->case_pattern == R_HIR_CASE_PATTERN_VARIANT) &&
                   (case_node->integer_value == UINT64_C(1))) {
            if (tag1_block != SIZE_MAX) {
                return false;
            }
            tag1_block = block_index;
        } else if ((case_node->case_pattern == R_HIR_CASE_PATTERN_VARIANT) &&
                   (case_node->integer_value == UINT64_C(2)) && (variant_count >= UINT32_C(3))) {
            if (tag2_block != SIZE_MAX) {
                return false;
            }
            tag2_block = block_index;
        } else if (case_node->case_pattern == R_HIR_CASE_PATTERN_VARIANT &&
                   case_node->integer_value == UINT64_C(3) && variant_count == UINT32_C(4)) {
            if (tag3_block != SIZE_MAX)
                return false;
            tag3_block = block_index;
        } else if (case_node->case_pattern == R_HIR_CASE_PATTERN_DEFAULT) {
            if ((standard_variant_count != UINT32_C(0)) || (default_block != SIZE_MAX)) {
                return false;
            }
            default_block = block_index;
        } else {
            return false;
        }
    }
    if ((tag0_block == SIZE_MAX) && (default_block != SIZE_MAX)) {
        tag0_block = default_block;
    }
    if ((tag1_block == SIZE_MAX) && (default_block != SIZE_MAX)) {
        tag1_block = default_block;
    }
    if (variant_count == 1U) {
        tag1_block = tag0_block;
    }
    if ((tag0_block == SIZE_MAX) || (tag1_block == SIZE_MAX) ||
        ((variant_count >= UINT32_C(3)) && (tag2_block == SIZE_MAX)) ||
        ((variant_count == UINT32_C(4)) && (tag3_block == SIZE_MAX)) ||
        !r_mir_add_block(build, &merge_block)) {
        return false;
    }
    if (variant_count >= UINT32_C(3)) {
        RMirValueId one_value;
        RMirValueId is_one;
        size_t nonzero_block;
        size_t remaining_block = tag2_block;
        if (variant_count == UINT32_C(4) && !r_mir_add_block(build, &remaining_block))
            return false;

        if (!r_mir_add_block(build, &nonzero_block)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BRANCH;
        instruction.span = node->span;
        instruction.operand0 = is_zero;
        instruction.target0 = (RMirBlockId)(tag0_block + 1U);
        instruction.target1 = (RMirBlockId)(nonzero_block + 1U);
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        build->current_block = nonzero_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
        instruction.span = node->span;
        instruction.type = u32_type;
        instruction.integer_value = UINT64_C(1);
        if (!r_mir_new_value(build, &one_value)) {
            return false;
        }
        instruction.result = one_value;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BINARY;
        instruction.span = node->span;
        instruction.type = bool_type;
        instruction.operation = R_TOKEN_EQUAL_EQUAL;
        instruction.operand0 = tag_value;
        instruction.operand1 = one_value;
        if (!r_mir_new_value(build, &is_one)) {
            return false;
        }
        instruction.result = is_one;
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BRANCH;
        instruction.span = node->span;
        instruction.operand0 = is_one;
        instruction.target0 = (RMirBlockId)(tag1_block + 1U);
        instruction.target1 = (RMirBlockId)(remaining_block + 1U);
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
        if (variant_count == UINT32_C(4)) {
            RMirValueId two;
            RMirValueId is_two;
            build->current_block = remaining_block;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_CONSTANT;
            instruction.span = node->span;
            instruction.type = u32_type;
            instruction.integer_value = UINT64_C(2);
            if (!r_mir_new_value(build, &two))
                return false;
            instruction.result = two;
            if (!r_mir_append_instruction(build, instruction))
                return false;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_BINARY;
            instruction.span = node->span;
            instruction.type = bool_type;
            instruction.operation = R_TOKEN_EQUAL_EQUAL;
            instruction.operand0 = tag_value;
            instruction.operand1 = two;
            if (!r_mir_new_value(build, &is_two))
                return false;
            instruction.result = is_two;
            if (!r_mir_append_instruction(build, instruction))
                return false;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_BRANCH;
            instruction.span = node->span;
            instruction.operand0 = is_two;
            instruction.target0 = (RMirBlockId)(tag2_block + 1U);
            instruction.target1 = (RMirBlockId)(tag3_block + 1U);
            if (!r_mir_append_instruction(build, instruction))
                return false;
        }
    } else {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_BRANCH;
        instruction.span = node->span;
        instruction.operand0 = is_zero;
        instruction.target0 = (RMirBlockId)(tag0_block + 1U);
        instruction.target1 = (RMirBlockId)(tag1_block + 1U);
        if (!r_mir_append_instruction(build, instruction)) {
            return false;
        }
    }
    for (child_index = 0U; child_index < (uint32_t)case_count; ++child_index) {
        const RHirNode *case_node = r_mir_hir_node(build->frontend, cases[child_index]);
        const RHirNode *block_node;
        build->current_block = case_blocks[child_index];
        if ((case_node == NULL) || (case_node->child_count != UINT32_C(1))) {
            return false;
        }
        if (!r_mir_lower_switch_binding(build, case_node, tagged.value)) {
            return false;
        }
        block_node = r_mir_hir_node(build->frontend,
                                    r_mir_hir_child(build->frontend, case_node, UINT32_C(0)));
        /* R-STMT-0007: fallthrough enters the next clause, which binds no payload. */
        if ((block_node == NULL) ||
            ((case_node->operation == R_TOKEN_KW_FALLTHROUGH) &&
             ((size_t)child_index + 1U >= case_count)) ||
            !r_mir_lower_statement(
                build, r_mir_hir_child(build->frontend, case_node, UINT32_C(0)), depth + 1U) ||
            (!build->blocks[build->current_block].terminated &&
             !r_mir_append_jump(build,
                                block_node->span,
                                case_node->operation == R_TOKEN_KW_FALLTHROUGH
                                    ? case_blocks[child_index + 1U]
                                    : merge_block))) {
            return false;
        }
    }
    build->current_block = merge_block;
    return true;
}

static bool r_mir_lower_switch(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    const RSemanticType *switch_type = r_semantic_type(build->frontend, node->auxiliary_type);

    if ((switch_type != NULL) &&
        (r_mir_type_kind_is_integer(switch_type->kind) ||
         (switch_type->kind == R_SEMANTIC_TYPE_CHAR) ||
         (switch_type->kind == R_SEMANTIC_TYPE_ENUM) ||
         r_mir_type_is_standard_fieldless_enum(build->frontend, node->auxiliary_type))) {
        return r_mir_lower_integer_switch(build, node, depth);
    }
    return r_mir_lower_tagged_switch(build, node, depth);
}

static bool r_mir_lower_throw(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    RMirExpressionResult payload;
    RMirInstruction instruction;
    RMirPendingCompletion pending;
    size_t finally_count;

    if ((node->child_count == UINT32_C(0)) || (node->auxiliary_type == R_TYPE_ID_INVALID) ||
        !r_mir_lower_expression(
            build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &payload) ||
        payload.is_place || (payload.value == R_MIR_VALUE_ID_INVALID)) {
        return false;
    }
    if (!r_mir_prepare_throw_value(build,
                                   node->span,
                                   node->auxiliary_type,
                                   payload.value,
                                   &instruction,
                                   &pending,
                                   &finally_count) ||
        !r_mir_lower_pending_cleanup_segments(
            build, node, UINT32_C(1), finally_count, node->span, &pending, depth)) {
        return false;
    }
    return r_mir_append_instruction(build, instruction);
}

static bool r_mir_lower_try(RMirBuildContext *build, const RHirNode *node, uint32_t depth) {
    const uint32_t catch_count = (uint32_t)node->integer_value;
    const size_t handler_base = build->effect_handler_count;
    const size_t finally_base = build->finally_count;
    const bool has_finally = node->child_count == catch_count + UINT32_C(2);
    size_t *catch_blocks = NULL;
    RMirFinally shared_finally = {0};
    RSourceSpan finally_span = {0};
    const RHirNode *try_block;
    uint32_t try_cleanup_start;
    size_t merge_block;
    uint32_t catch_index;
    bool merge_reachable = false;
    bool success = false;

    if ((node->child_count < catch_count + UINT32_C(1)) ||
        (node->child_count > catch_count + UINT32_C(2))) {
        return false;
    }
    if (has_finally) {
        const RHirNodeId finally_id =
            r_mir_hir_child(build->frontend, node, catch_count + UINT32_C(1));
        const RHirNode *finally_node = r_mir_hir_node(build->frontend, finally_id);
        size_t finally_entry;
        uint32_t finally_identifier;
        RMirInstruction marker;

        if ((finally_node == NULL) || (finally_node->kind != R_HIR_FINALLY) ||
            (finally_node->child_count != UINT32_C(1)) || !r_mir_add_block(build, &finally_entry) ||
            !r_mir_push_finally(build, finally_id, finally_entry, &finally_identifier)) {
            goto cleanup;
        }
        shared_finally = build->finalies[build->finally_count - 1U];
        finally_span = finally_node->span;
        (void)memset(&marker, 0, sizeof(marker));
        marker.kind = R_MIR_INSTRUCTION_FINALLY_PUSH;
        marker.span = finally_node->span;
        marker.target0 = (RMirBlockId)(finally_entry + 1U);
        marker.integer_value = (uint64_t)finally_identifier;
        marker.place_ordinal = (uint32_t)finally_base;
        if (!r_mir_append_instruction(build, marker)) {
            goto cleanup;
        }
    }
    if (catch_count != UINT32_C(0)) {
        catch_blocks =
            r_context_allocate(build->frontend, (size_t)catch_count * sizeof(*catch_blocks));
        if (catch_blocks == NULL) {
            goto cleanup;
        }
    }
    for (catch_index = UINT32_C(0); catch_index < catch_count; ++catch_index) {
        const RHirNode *catch_node = r_mir_hir_node(
            build->frontend, r_mir_hir_child(build->frontend, node, catch_index + UINT32_C(1)));

        if ((catch_node == NULL) || (catch_node->kind != R_HIR_CATCH) ||
            (catch_node->symbol == R_SYMBOL_ID_INVALID) ||
            !r_mir_add_block(build, &catch_blocks[catch_index]) ||
            !r_mir_push_effect_handler(build,
                                       catch_node->type,
                                       catch_blocks[catch_index],
                                       build->finally_count,
                                       handler_base)) {
            goto cleanup;
        }
    }
    try_block =
        r_mir_hir_node(build->frontend, r_mir_hir_child(build->frontend, node, UINT32_C(0)));
    try_cleanup_start = r_mir_block_trailing_drop_start(build, try_block);
    if ((try_block == NULL) || (try_block->kind != R_HIR_BLOCK) ||
        !r_mir_add_block(build, &merge_block) ||
        !r_mir_lower_block_range(
            build, try_block, UINT32_C(0), try_cleanup_start, depth + UINT32_C(1))) {
        goto cleanup;
    }
    build->effect_handler_count = handler_base;
    if (!build->blocks[build->current_block].terminated) {
        const RMirPendingCompletion pending =
            r_mir_pending_completion(R_MIR_PENDING_COMPLETION_NORMAL,
                                     R_TYPE_ID_INVALID,
                                     R_MIR_VALUE_ID_INVALID,
                                     (RMirBlockId)(merge_block + 1U));
        size_t resume_block;

        if (!r_mir_begin_pending_completion(
                build, finally_base, node->span, &pending, &resume_block) ||
            !r_mir_lower_block_range(
                build, try_block, try_cleanup_start, try_block->child_count, depth + UINT32_C(1)) ||
            !r_mir_finish_pending_completion(
                build, finally_base, node->span, &pending, resume_block) ||
            !r_mir_append_jump(build, node->span, merge_block)) {
            goto cleanup;
        }
        merge_reachable = true;
    }
    for (catch_index = UINT32_C(0); catch_index < catch_count; ++catch_index) {
        const RHirNode *catch_node = r_mir_hir_node(
            build->frontend, r_mir_hir_child(build->frontend, node, catch_index + UINT32_C(1)));
        const RSemanticSymbol *binding;
        const RHirNode *catch_block;
        RMirInstruction instruction;
        RMirValueId payload;
        uint32_t ordinal;
        uint32_t catch_cleanup_start;

        if ((catch_node == NULL) || (catch_node->child_count != UINT32_C(1)) ||
            (catch_node->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)catch_node->symbol > build->frontend->semantic_symbol_count) ||
            (build->next_local == UINT32_MAX)) {
            goto cleanup;
        }
        binding = &build->frontend->semantic_symbols[(size_t)catch_node->symbol - 1U];
        catch_block = r_mir_hir_node(build->frontend,
                                     r_mir_hir_child(build->frontend, catch_node, UINT32_C(0)));
        catch_cleanup_start = r_mir_block_trailing_drop_start(build, catch_block);
        if ((catch_block == NULL) || (catch_block->kind != R_HIR_BLOCK)) {
            goto cleanup;
        }
        ordinal = build->next_local;
        build->next_local += UINT32_C(1);
        build->current_block = catch_blocks[catch_index];
        if (!r_mir_push_place(build, catch_node->symbol, ordinal, false)) {
            goto cleanup;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_LOCAL;
        instruction.span = catch_node->span;
        instruction.type = catch_node->type;
        instruction.symbol = catch_node->symbol;
        instruction.borrow_origin = binding->borrow_origin;
        instruction.borrow_origin_set = binding->borrow_origin_set;
        instruction.borrow_origin_multiple = binding->borrow_origin_multiple;
        instruction.place_ordinal = ordinal;
        if (!r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_EFFECT_PAYLOAD;
        instruction.span = catch_node->span;
        instruction.type = catch_node->type;
        instruction.symbol = catch_node->symbol;
        instruction.integer_value =
            r_mir_effect_tag(build->frontend, node->auxiliary_type, catch_node->type);
        if (!r_mir_new_value(build, &payload)) {
            goto cleanup;
        }
        instruction.result = payload;
        if (!r_mir_append_instruction(build, instruction)) {
            goto cleanup;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_STORE;
        instruction.span = catch_node->span;
        instruction.type = catch_node->type;
        instruction.symbol = catch_node->symbol;
        instruction.borrow_origin = binding->borrow_origin;
        instruction.borrow_origin_set = binding->borrow_origin_set;
        instruction.borrow_origin_multiple = binding->borrow_origin_multiple;
        instruction.operand0 = payload;
        instruction.place_ordinal = ordinal;
        if (!r_mir_append_instruction(build, instruction) ||
            !r_mir_lower_block_range(
                build, catch_block, UINT32_C(0), catch_cleanup_start, depth + UINT32_C(1))) {
            goto cleanup;
        }
        if (!build->blocks[build->current_block].terminated) {
            const RMirPendingCompletion pending =
                r_mir_pending_completion(R_MIR_PENDING_COMPLETION_NORMAL,
                                         R_TYPE_ID_INVALID,
                                         R_MIR_VALUE_ID_INVALID,
                                         (RMirBlockId)(merge_block + 1U));
            size_t resume_block;

            if (!r_mir_begin_pending_completion(
                    build, finally_base, catch_node->span, &pending, &resume_block) ||
                !r_mir_lower_block_range(build,
                                         catch_block,
                                         catch_cleanup_start,
                                         catch_block->child_count,
                                         depth + UINT32_C(1)) ||
                !r_mir_finish_pending_completion(
                    build, finally_base, catch_node->span, &pending, resume_block) ||
                !r_mir_append_jump(build, catch_node->span, merge_block)) {
                goto cleanup;
            }
            merge_reachable = true;
        }
    }
    build->finally_count = finally_base;
    if (has_finally) {
        const RHirNode *finally_node = r_mir_hir_node(build->frontend, shared_finally.node);
        const RMirBlockId outer_entry =
            finally_base == 0U ? R_MIR_BLOCK_ID_INVALID
                               : (RMirBlockId)(build->finalies[finally_base - 1U].entry_block + 1U);
        RMirInstruction marker;

        if ((finally_node == NULL) || (finally_node->kind != R_HIR_FINALLY) ||
            (finally_node->child_count != UINT32_C(1)) ||
            (shared_finally.entry_block >= build->block_count) ||
            (shared_finally.identifier == UINT32_C(0))) {
            goto cleanup;
        }
        build->current_block = shared_finally.entry_block;
        (void)memset(&marker, 0, sizeof(marker));
        marker.kind = R_MIR_INSTRUCTION_FINALLY_ENTER;
        marker.span = finally_span;
        marker.target0 = outer_entry;
        marker.integer_value = (uint64_t)shared_finally.identifier;
        marker.place_ordinal = (uint32_t)finally_base;
        if (!r_mir_append_instruction(build, marker) ||
            !r_mir_lower_statement(build,
                                   r_mir_hir_child(build->frontend, finally_node, UINT32_C(0)),
                                   depth + UINT32_C(1)) ||
            build->blocks[build->current_block].terminated) {
            goto cleanup;
        }
        (void)memset(&marker, 0, sizeof(marker));
        marker.kind = R_MIR_INSTRUCTION_FINALLY_EXIT;
        marker.span = finally_span;
        marker.target0 = outer_entry;
        marker.integer_value = (uint64_t)shared_finally.identifier;
        marker.place_ordinal = (uint32_t)finally_base;
        if (!r_mir_append_instruction(build, marker)) {
            goto cleanup;
        }
    }
    build->current_block = merge_block;
    if (!merge_reachable) {
        RMirInstruction unreachable;

        (void)memset(&unreachable, 0, sizeof(unreachable));
        unreachable.kind = R_MIR_INSTRUCTION_UNREACHABLE;
        unreachable.span = node->span;
        if (!r_mir_append_instruction(build, unreachable)) {
            goto cleanup;
        }
    }
    success = true;

cleanup:
    build->effect_handler_count = handler_base;
    build->finally_count = finally_base;
    r_context_free(build->frontend, catch_blocks);
    return success;
}

static bool r_mir_lower_statement(RMirBuildContext *build, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_mir_hir_node(build->frontend, node_id);
    RMirInstruction instruction;

    if ((node == NULL) || (depth > r_frontend_tree_depth_limit(build->frontend))) {
        if (node != NULL) {
            build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        }
        return false;
    }
    if (node->kind == R_HIR_BLOCK) {
        return r_mir_lower_block(build, node, depth);
    }
    if (node->kind == R_HIR_LOCAL) {
        return r_mir_lower_local(build, node, depth);
    }
    if (node->kind == R_HIR_TASK_SCOPE_WAIT || node->kind == R_HIR_TASK_SCOPE_ENTER ||
        node->kind == R_HIR_TASK_SCOPE_CLOSE) {
        RMirExpressionResult ignored;
        return r_mir_lower_task_scope(build, node, depth, &ignored);
    }
    if (node->kind == R_HIR_EXPRESSION_STATEMENT) {
        RMirExpressionResult value;
        if (node->child_count == UINT32_C(0)) {
            return true;
        }
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &value) ||
            value.is_place) {
            return false;
        }
        if (value.value != R_MIR_VALUE_ID_INVALID) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_DISCARD;
            instruction.span = node->span;
            instruction.type = node->type;
            instruction.operand0 = value.value;
            return r_mir_append_instruction(build, instruction);
        }
        return true;
    }
    if (node->kind == R_HIR_DROP) {
        RMirExpressionResult place;
        if ((node->child_count != UINT32_C(1)) ||
            !r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, UINT32_C(0)), depth + 1U, &place) ||
            !place.is_place || (place.place_symbol != node->symbol)) {
            return false;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_DROP;
        instruction.operation = node->operation;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.symbol = place.place_symbol;
        instruction.place_ordinal = place.place_ordinal;
        instruction.place_is_parameter = place.place_is_parameter;
        instruction.operand1 = place.place_projection;
        return r_mir_append_instruction(build, instruction);
    }
    if (node->kind == R_HIR_RETURN) {
        const RSemanticType *return_type = r_semantic_type(build->frontend, node->type);
        const bool has_value = (return_type != NULL) && (return_type->kind != R_SEMANTIC_TYPE_VOID);
        RMirPendingCompletion pending;
        const uint32_t cleanup_start = has_value ? UINT32_C(1) : UINT32_C(0);
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = R_MIR_INSTRUCTION_RETURN;
        instruction.span = node->span;
        instruction.type = node->type;
        instruction.borrow_origin = node->borrow_origin;
        instruction.borrow_origin_set = node->borrow_origin_set;
        instruction.borrow_origin_multiple = node->borrow_origin_multiple;
        if (has_value) {
            RMirExpressionResult value;
            if (node->child_count == UINT32_C(0)) {
                return false;
            }
            if (!r_mir_lower_expression(build,
                                        r_mir_hir_child(build->frontend, node, UINT32_C(0)),
                                        depth + 1U,
                                        &value) ||
                value.is_place || (value.value == R_MIR_VALUE_ID_INVALID)) {
                return false;
            }
            instruction.operand0 = value.value;
        }
        if (return_type == NULL) {
            return false;
        }
        pending = r_mir_pending_completion(R_MIR_PENDING_COMPLETION_RETURN,
                                           node->type,
                                           instruction.operand0,
                                           R_MIR_BLOCK_ID_INVALID);
        RHirNode cleanup = *node;
        while (cleanup.child_count > cleanup_start &&
               r_mir_hir_node(build->frontend,
                              r_mir_hir_child(build->frontend, node, cleanup.child_count - 1U))
                   ->out_commit)
            --cleanup.child_count;
        if (!r_mir_lower_pending_cleanup_segments(
                build, &cleanup, cleanup_start, 0U, node->span, &pending, depth)) {
            return false;
        }
        for (uint32_t output = cleanup.child_count; output < node->child_count; ++output)
            if (!r_mir_lower_statement(
                    build, r_mir_hir_child(build->frontend, node, output), depth + 1U))
                return false;
        return r_mir_append_instruction(build, instruction);
    }
    if (node->kind == R_HIR_IF) {
        return r_mir_lower_if(build, node, depth);
    }
    if (node->kind == R_HIR_WHILE) {
        return r_mir_lower_while(build, node, depth);
    }
    if (node->kind == R_HIR_FOR) {
        return r_mir_lower_for(build, node, depth);
    }
    if ((node->kind == R_HIR_BREAK) || (node->kind == R_HIR_CONTINUE)) {
        const RMirLoopTarget *loop = (node->loop_target != 0U) &&
                                             (node->loop_target <= build->loop_count)
                                         ? &build->loops[build->loop_count - node->loop_target]
                                         : NULL;
        if ((node->loop_target != 0U) && (loop == NULL)) {
            return false;
        }
        const size_t target = loop != NULL ? (node->kind == R_HIR_BREAK ? loop->break_target
                                                                        : loop->continue_target)
                              : node->kind == R_HIR_BREAK ? build->break_target
                                                          : build->continue_target;
        const size_t finally_count = loop != NULL ? loop->finally_count
                                     : node->kind == R_HIR_BREAK ? build->break_finally_count
                                                                 : build->continue_finally_count;
        const RMirPendingCompletionReason reason = node->kind == R_HIR_BREAK
                                                       ? R_MIR_PENDING_COMPLETION_BREAK
                                                       : R_MIR_PENDING_COMPLETION_CONTINUE;
        const RMirPendingCompletion pending = r_mir_pending_completion(
            reason,
            R_TYPE_ID_INVALID,
            R_MIR_VALUE_ID_INVALID,
            target < UINT32_MAX ? (RMirBlockId)(target + 1U) : R_MIR_BLOCK_ID_INVALID);

        if ((target == SIZE_MAX) || (target >= UINT32_MAX)) {
            return false;
        }
        return r_mir_lower_pending_cleanup_segments(
                   build, node, UINT32_C(0), finally_count, node->span, &pending, depth) &&
               r_mir_append_jump(build, node->span, target);
    }
    if (node->kind == R_HIR_THROW) {
        return r_mir_lower_throw(build, node, depth);
    }
    if (node->kind == R_HIR_TRY) {
        return r_mir_lower_try(build, node, depth);
    }
    if (node->kind == R_HIR_SWITCH) {
        return r_mir_lower_switch(build, node, depth);
    }
    return false;
}

static void r_mir_destroy_build(RMirBuildContext *build) {
    size_t index;

    for (index = 0U; index < build->block_count; ++index) {
        r_context_free(build->frontend, build->blocks[index].instructions);
    }
    r_context_free(build->frontend, build->blocks);
    r_context_free(build->frontend, build->loops);
    r_context_free(build->frontend, build->places);
    r_context_free(build->frontend, build->effect_handlers);
    r_context_free(build->frontend, build->pending_aggregate_values.items);
    r_context_free(build->frontend, build->finalies);
    (void)memset(build, 0, sizeof(*build));
}

static bool r_mir_append_function_storage(RFrontendContext *context,
                                          const RMirBuildContext *build,
                                          RSymbolId symbol,
                                          RTypeId return_type) {
    RMirFunction function;
    size_t block_index;

    if ((context->mir_function_count >= UINT32_MAX) || (context->mir_block_count > UINT32_MAX) ||
        (build->block_count > ((size_t)UINT32_MAX - context->mir_block_count)) ||
        !r_grow_array(context,
                      (void **)&context->mir_functions,
                      &context->mir_function_capacity,
                      sizeof(*context->mir_functions),
                      context->mir_function_count + 1U) ||
        !r_grow_array(context,
                      (void **)&context->mir_blocks,
                      &context->mir_block_capacity,
                      sizeof(*context->mir_blocks),
                      context->mir_block_count + build->block_count)) {
        if (context->resource_status == R_FRONTEND_OK) {
            context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        }
        return false;
    }
    (void)memset(&function, 0, sizeof(function));
    function.symbol = symbol;
    function.return_type = return_type;
    function.first_block = (uint32_t)context->mir_block_count;
    function.block_count = (uint32_t)build->block_count;
    for (block_index = 0U; block_index < build->block_count; ++block_index) {
        const RMirTemporaryBlock *temporary = &build->blocks[block_index];
        RMirBlock *block = &context->mir_blocks[context->mir_block_count];
        if ((context->mir_instruction_count > UINT32_MAX) ||
            (temporary->instruction_count > UINT32_MAX) ||
            (temporary->instruction_count >
             ((size_t)UINT32_MAX - context->mir_instruction_count)) ||
            !r_grow_array(context,
                          (void **)&context->mir_instructions,
                          &context->mir_instruction_capacity,
                          sizeof(*context->mir_instructions),
                          context->mir_instruction_count + temporary->instruction_count)) {
            if (context->resource_status == R_FRONTEND_OK) {
                context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
            }
            return false;
        }
        block->first_instruction = (uint32_t)context->mir_instruction_count;
        block->instruction_count = (uint32_t)temporary->instruction_count;
        if (temporary->instruction_count != 0U) {
            (void)memcpy(context->mir_instructions + context->mir_instruction_count,
                         temporary->instructions,
                         temporary->instruction_count * sizeof(*temporary->instructions));
            context->mir_instruction_count += temporary->instruction_count;
        }
        context->mir_block_count += 1U;
    }
    context->mir_functions[context->mir_function_count] = function;
    context->mir_function_count += 1U;
    return true;
}

static bool r_mir_await_place_slot(const RMirBuildContext *build,
                                   const RMirInstruction *instruction,
                                   size_t *slot) {
    if (instruction->place_is_parameter) {
        if (instruction->place_ordinal >= build->next_parameter) {
            return false;
        }
        *slot = (size_t)instruction->place_ordinal;
        return true;
    }
    if (instruction->place_ordinal >= build->next_local) {
        return false;
    }
    *slot = (size_t)build->next_parameter + (size_t)instruction->place_ordinal;
    return true;
}

enum {
    R_MIR_AWAIT_LIVENESS_BORROW = UINT32_C(1) << 0U,
    R_MIR_AWAIT_LIVENESS_NON_SEND = UINT32_C(1) << 1U,
    R_MIR_AWAIT_LIVENESS_SYNC_MUTEX = UINT32_C(1) << 2U,
    R_MIR_AWAIT_LIVENESS_SYNC_RW_LOCK = UINT32_C(1) << 3U
};

static void r_mir_await_bit_set(uint64_t *bits, size_t slot) {
    bits[slot / UINT64_C(64)] |= UINT64_C(1) << (slot % UINT64_C(64));
}

static void r_mir_await_bit_clear(uint64_t *bits, size_t slot) {
    bits[slot / UINT64_C(64)] &= ~(UINT64_C(1) << (slot % UINT64_C(64)));
}

static bool r_mir_await_transfer_instruction(const RMirBuildContext *build,
                                             const RMirInstruction *instruction,
                                             const unsigned char *eligible,
                                             uint64_t *live) {
    bool use = false;
    bool kill = false;
    size_t slot;

    switch (instruction->kind) {
    case R_MIR_INSTRUCTION_PARAMETER:
    case R_MIR_INSTRUCTION_LOCAL:
        kill = true;
        break;
    case R_MIR_INSTRUCTION_STORE:
        use = instruction->operand1 != R_MIR_VALUE_ID_INVALID;
        kill = !use;
        break;
    case R_MIR_INSTRUCTION_MOVE:
    case R_MIR_INSTRUCTION_DROP:
    case R_MIR_INSTRUCTION_AWAIT:
        use = true;
        kill = instruction->operand1 == R_MIR_VALUE_ID_INVALID;
        break;
    case R_MIR_INSTRUCTION_FIELD:
    case R_MIR_INSTRUCTION_INDEX:
    case R_MIR_INSTRUCTION_DEREF:
    case R_MIR_INSTRUCTION_BORROW:
    case R_MIR_INSTRUCTION_RAW_ADDRESS:
    case R_MIR_INSTRUCTION_LENGTH:
    case R_MIR_INSTRUCTION_LOAD:
        use = true;
        break;
    case R_MIR_INSTRUCTION_STANDARD_CALL:
        if ((instruction->standard_operation != R_STANDARD_CALL_ARC_CLONE) &&
            (instruction->standard_operation != R_STANDARD_CALL_RC_CLONE)) {
            return true;
        }
        if (instruction->operand_count == UINT32_C(1)) {
            return true;
        }
        if (instruction->operand_count != UINT32_C(0)) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        use = true;
        break;
    case R_MIR_INSTRUCTION_SLICE:
        if (instruction->operation == R_TOKEN_KW_ARRAY) {
            return true;
        }
        use = true;
        break;
    default:
        return true;
    }
    if (!r_mir_await_place_slot(build, instruction, &slot)) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (eligible[slot] == 0U) {
        return true;
    }
    if (kill) {
        r_mir_await_bit_clear(live, slot);
    }
    if (use) {
        r_mir_await_bit_set(live, slot);
    }
    return true;
}

static bool r_mir_await_union_successors(const RMirBuildContext *build,
                                         size_t block_index,
                                         const uint64_t *live_in,
                                         size_t word_count,
                                         uint64_t *live) {
    const RMirTemporaryBlock *block = &build->blocks[block_index];
    const RMirInstruction *terminator;
    RMirBlockId targets[2];
    size_t target_count = 0U;
    size_t target_index;

    (void)memset(live, 0, word_count * sizeof(*live));
    if (block->instruction_count == 0U) {
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    terminator = &block->instructions[block->instruction_count - 1U];
    if (terminator->kind == R_MIR_INSTRUCTION_FINALLY_EXIT) {
        size_t scan_block;

        if (terminator->target0 != R_MIR_BLOCK_ID_INVALID) {
            const size_t outer = (size_t)terminator->target0 - 1U;
            size_t word;

            if (outer >= build->block_count) {
                build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            for (word = 0U; word < word_count; ++word) {
                live[word] |= live_in[outer * word_count + word];
            }
        }
        /*
         * The resume state is selected from the hidden pending record. Conservatively union every
         * recorded resume block; this preserves await liveness without cloning finally CFG bodies.
         */
        for (scan_block = 0U; scan_block < build->block_count; ++scan_block) {
            const RMirTemporaryBlock *candidate = &build->blocks[scan_block];
            size_t instruction_index;

            for (instruction_index = 0U; instruction_index < candidate->instruction_count;
                 ++instruction_index) {
                const RMirInstruction *instruction = &candidate->instructions[instruction_index];
                size_t successor;
                size_t word;

                if (instruction->kind != R_MIR_INSTRUCTION_PENDING_SET) {
                    continue;
                }
                successor = instruction->target1 == R_MIR_BLOCK_ID_INVALID
                                ? SIZE_MAX
                                : (size_t)instruction->target1 - 1U;
                if (successor >= build->block_count) {
                    build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                    return false;
                }
                for (word = 0U; word < word_count; ++word) {
                    live[word] |= live_in[successor * word_count + word];
                }
            }
        }
        return true;
    }
    switch (terminator->kind) {
    case R_MIR_INSTRUCTION_BRANCH:
        targets[0] = terminator->target0;
        targets[1] = terminator->target1;
        target_count = 2U;
        break;
    case R_MIR_INSTRUCTION_JUMP:
        targets[0] = terminator->target0;
        target_count = 1U;
        break;
    case R_MIR_INSTRUCTION_AWAIT:
    case R_MIR_INSTRUCTION_TASK_SCOPE_WAIT:
        targets[0] = terminator->target0;
        targets[1] = terminator->target1;
        target_count = 2U;
        break;
    case R_MIR_INSTRUCTION_RETURN:
    case R_MIR_INSTRUCTION_THROW:
    case R_MIR_INSTRUCTION_CANCEL:
    case R_MIR_INSTRUCTION_UNREACHABLE:
        break;
    default:
        build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (target_index = 0U; target_index < target_count; ++target_index) {
        const RMirBlockId target = targets[target_index];
        const size_t successor = target == R_MIR_BLOCK_ID_INVALID ? SIZE_MAX : (size_t)target - 1U;
        size_t word;

        if (successor >= build->block_count) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (word = 0U; word < word_count; ++word) {
            live[word] |= live_in[successor * word_count + word];
        }
    }
    return true;
}

static bool r_mir_await_bits_any(const uint64_t *bits, size_t word_count) {
    size_t word;

    for (word = 0U; word < word_count; ++word) {
        if (bits[word] != UINT64_C(0)) {
            return true;
        }
    }
    return false;
}

static bool r_mir_await_bits_have_category(const uint64_t *bits,
                                           const unsigned char *eligible,
                                           size_t slot_count,
                                           unsigned char category) {
    size_t slot;

    for (slot = 0U; slot < slot_count; ++slot) {
        if (((eligible[slot] & category) != 0U) &&
            ((bits[slot / UINT64_C(64)] & (UINT64_C(1) << (slot % (size_t)UINT64_C(64)))) !=
             UINT64_C(0))) {
            return true;
        }
    }
    return false;
}

/* L30.4 (R-FUNC-0011): a std.sync lock guard, or a lock outcome that carries one, is reported
   together with the std.async lock whose guards may cross await. */
static uint32_t r_mir_std_sync_guard_category(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    const char *guard = NULL;
    size_t index;

    if ((type == NULL) || (type->base == R_TYPE_ID_INVALID) || (type->second != R_TYPE_ID_INVALID)) {
        return 0U;
    }
    for (index = 0U; r_standard_sync_outcome_at(index) != NULL; ++index) {
        const RStandardSyncOutcome *schema = r_standard_sync_outcome_at(index);

        if (r_mir_standard_type_name_equal(context, type, schema->name)) {
            guard = schema->guard;
            break;
        }
    }
    if (guard == NULL) {
        guard = r_mir_standard_type_name_equal(context, type, "std.sync::mutex_guard")
                    ? "std.sync::mutex_guard"
                : r_mir_standard_type_name_equal(context, type, "std.sync::rw_read_guard")
                    ? "std.sync::rw_read_guard"
                : r_mir_standard_type_name_equal(context, type, "std.sync::rw_write_guard")
                    ? "std.sync::rw_write_guard"
                    : NULL;
    }
    if (guard == NULL) {
        return 0U;
    }
    if (strcmp(guard, "std.sync::mutex_guard") == 0) {
        return R_MIR_AWAIT_LIVENESS_SYNC_MUTEX;
    }
    if ((strcmp(guard, "std.sync::rw_read_guard") == 0) ||
        (strcmp(guard, "std.sync::rw_write_guard") == 0)) {
        return R_MIR_AWAIT_LIVENESS_SYNC_RW_LOCK;
    }
    return 0U;
}

static bool r_mir_validate_await_liveness(RMirBuildContext *build,
                                          const RSemanticSymbol *function) {
    const size_t slot_count = (size_t)build->next_parameter + (size_t)build->next_local;
    const size_t word_count = (slot_count + (size_t)UINT64_C(63)) / (size_t)UINT64_C(64);
    const RSource *function_source = r_get_source_const(build->frontend, function->module_source);
    RSymbolId startup_parameter = R_SYMBOL_ID_INVALID;
    unsigned char *eligible = NULL;
    uint64_t *live_in = NULL;
    uint64_t *live = NULL;
    size_t iteration = 0U;
    size_t iteration_limit;
    size_t place_index;
    bool changed = true;
    bool success = false;

    if ((slot_count == 0U) || (build->block_count == 0U)) {
        return true;
    }
    if ((word_count > (SIZE_MAX / build->block_count)) ||
        ((word_count * build->block_count) > (SIZE_MAX / sizeof(*live_in))) ||
        (build->block_count > (SIZE_MAX / (slot_count + 1U)))) {
        build->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    iteration_limit = build->block_count * (slot_count + 1U);
    eligible = r_context_allocate(build->frontend, slot_count);
    live_in =
        r_context_allocate(build->frontend, build->block_count * word_count * sizeof(*live_in));
    live = r_context_allocate(build->frontend, word_count * sizeof(*live));
    if ((eligible == NULL) || (live_in == NULL) || (live == NULL)) {
        goto cleanup;
    }
    (void)memset(eligible, 0, slot_count);
    (void)memset(live_in, 0, build->block_count * word_count * sizeof(*live_in));
    if (!function->is_protected && (function->parameter_count == UINT32_C(1)) &&
        (function_source != NULL) &&
        r_source_text_equal(function_source, function->name_span, "main")) {
        for (place_index = 0U; place_index < build->place_count; ++place_index) {
            const RMirPlace *place = &build->places[place_index];

            if (place->is_parameter && (place->ordinal == UINT32_C(0))) {
                startup_parameter = place->symbol;
                break;
            }
        }
    }
    for (place_index = 0U; place_index < build->place_count; ++place_index) {
        const RMirPlace *place = &build->places[place_index];
        const RSemanticSymbol *symbol;
        size_t slot;

        if ((place->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)place->symbol > build->frontend->semantic_symbol_count)) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        symbol = &build->frontend->semantic_symbols[(size_t)place->symbol - 1U];
        slot = place->is_parameter ? (size_t)place->ordinal
                                   : (size_t)build->next_parameter + (size_t)place->ordinal;
        if (slot >= slot_count) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        if ((place->symbol != startup_parameter) &&
            ((startup_parameter == R_SYMBOL_ID_INVALID) ||
             (symbol->borrow_origin != startup_parameter))) {
            const uint32_t sync_guard = r_mir_std_sync_guard_category(build->frontend, symbol->type);

            if (sync_guard != 0U) {
                eligible[slot] |= (unsigned char)sync_guard;
            } else if (!function->is_scoped &&
                r_semantic_type_crosses_await(build->frontend, symbol->type)) {
                eligible[slot] |= R_MIR_AWAIT_LIVENESS_BORROW;
            } else if (!r_semantic_type_is_send(build->frontend, symbol->type)) {
                eligible[slot] |= R_MIR_AWAIT_LIVENESS_NON_SEND;
            }
        }
    }
    while (changed) {
        size_t block_index = build->block_count;

        if (iteration >= iteration_limit) {
            build->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        iteration += 1U;
        changed = false;
        while (block_index != 0U) {
            const RMirTemporaryBlock *block;
            size_t instruction_index;
            uint64_t *block_live_in;

            block_index -= 1U;
            block = &build->blocks[block_index];
            if (!r_mir_await_union_successors(build, block_index, live_in, word_count, live)) {
                goto cleanup;
            }
            instruction_index = block->instruction_count;
            while (instruction_index != 0U) {
                instruction_index -= 1U;
                if (!r_mir_await_transfer_instruction(
                        build, &block->instructions[instruction_index], eligible, live)) {
                    goto cleanup;
                }
            }
            block_live_in = &live_in[block_index * word_count];
            if (memcmp(block_live_in, live, word_count * sizeof(*live)) != 0) {
                (void)memcpy(block_live_in, live, word_count * sizeof(*live));
                changed = true;
            }
        }
    }
    for (place_index = 0U; place_index < build->block_count; ++place_index) {
        const RMirTemporaryBlock *block = &build->blocks[place_index];
        size_t instruction_index = block->instruction_count;

        if (!r_mir_await_union_successors(build, place_index, live_in, word_count, live)) {
            goto cleanup;
        }
        while (instruction_index != 0U) {
            const RMirInstruction *instruction;

            instruction_index -= 1U;
            instruction = &block->instructions[instruction_index];
            if ((instruction->kind == R_MIR_INSTRUCTION_AWAIT ||
                 instruction->kind == R_MIR_INSTRUCTION_TASK_SCOPE_WAIT) &&
                r_mir_await_bits_any(live, word_count)) {
                if (r_mir_await_bits_have_category(
                        live, eligible, slot_count, R_MIR_AWAIT_LIVENESS_BORROW) &&
                    !r_add_diagnostic_phase(build->frontend,
                                            R_DIAGNOSTIC_PHASE_SEMANTIC,
                                            "R-DIAG-ASYNC-001",
                                            "R-BORROW-0024",
                                            "borrow, slice, or runtime str may remain live across "
                                            "this await",
                                            R_DIAGNOSTIC_ERROR,
                                            instruction->span)) {
                    goto cleanup;
                }
                if (r_mir_await_bits_have_category(
                        live, eligible, slot_count, R_MIR_AWAIT_LIVENESS_NON_SEND) &&
                    !r_add_diagnostic_phase(build->frontend,
                                            R_DIAGNOSTIC_PHASE_SEMANTIC,
                                            "R-DIAG-ASYNC-001",
                                            "R-FUNC-0011",
                                            "non-Send value may remain live across this await",
                                            R_DIAGNOSTIC_ERROR,
                                            instruction->span)) {
                    goto cleanup;
                }
                if (r_mir_await_bits_have_category(
                        live, eligible, slot_count, R_MIR_AWAIT_LIVENESS_SYNC_MUTEX) &&
                    !r_add_diagnostic_phase(build->frontend,
                                            R_DIAGNOSTIC_PHASE_SEMANTIC,
                                            "R-DIAG-ASYNC-001",
                                            "R-FUNC-0011",
                                            "std.sync mutex guard may remain live across this "
                                            "await; use std.async::mutex, whose guard may",
                                            R_DIAGNOSTIC_ERROR,
                                            instruction->span)) {
                    goto cleanup;
                }
                if (r_mir_await_bits_have_category(
                        live, eligible, slot_count, R_MIR_AWAIT_LIVENESS_SYNC_RW_LOCK) &&
                    !r_add_diagnostic_phase(build->frontend,
                                            R_DIAGNOSTIC_PHASE_SEMANTIC,
                                            "R-DIAG-ASYNC-001",
                                            "R-FUNC-0011",
                                            "std.sync rw_lock guard may remain live across this "
                                            "await; use std.async::rw_lock, whose guards may",
                                            R_DIAGNOSTIC_ERROR,
                                            instruction->span)) {
                    goto cleanup;
                }
            }
            if (!r_mir_await_transfer_instruction(build, instruction, eligible, live)) {
                goto cleanup;
            }
        }
    }
    success = true;

cleanup:
    r_context_free(build->frontend, live);
    r_context_free(build->frontend, live_in);
    r_context_free(build->frontend, eligible);
    return success;
}

static bool r_mir_lower_function(RFrontendContext *context, RHirNodeId node_id) {
    const RHirNode *node = r_mir_hir_node(context, node_id);
    const RSemanticSymbol *symbol;
    RMirBuildContext build;
    uint32_t child_index;
    bool success = false;

    if ((node == NULL) || (node->kind != R_HIR_FUNCTION) || (node->symbol == R_SYMBOL_ID_INVALID) ||
        ((size_t)node->symbol > context->semantic_symbol_count)) {
        return false;
    }
    symbol = &context->semantic_symbols[(size_t)node->symbol - 1U];
    (void)memset(&build, 0, sizeof(build));
    build.frontend = context;
    build.break_target = SIZE_MAX;
    build.continue_target = SIZE_MAX;
    build.break_finally_count = 0U;
    build.continue_finally_count = 0U;
    build.function_effects = symbol->throws_type;
    if (symbol->has_definition && !r_mir_add_block(&build, &build.current_block)) {
        goto cleanup;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        RHirNodeId child_id = r_mir_hir_child(context, node, child_index);
        const RHirNode *child = r_mir_hir_node(context, child_id);
        if (child == NULL) {
            goto cleanup;
        }
        if (child->kind == R_HIR_PARAMETER) {
            RMirInstruction instruction;
            uint32_t ordinal;
            if (!symbol->has_definition) {
                continue;
            }
            if (build.next_parameter == UINT32_MAX) {
                context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
                goto cleanup;
            }
            ordinal = build.next_parameter;
            build.next_parameter += 1U;
            if (!r_mir_push_place(&build, child->symbol, ordinal, true)) {
                goto cleanup;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = R_MIR_INSTRUCTION_PARAMETER;
            instruction.span = child->span;
            instruction.type = child->type;
            instruction.symbol = child->symbol;
            instruction.borrow_origin = child->borrow_origin;
            instruction.borrow_origin_set = child->borrow_origin_set;
            instruction.borrow_origin_multiple = child->borrow_origin_multiple;
            instruction.place_ordinal = ordinal;
            instruction.place_is_parameter = true;
            if (!r_mir_append_instruction(&build, instruction)) {
                goto cleanup;
            }
        } else if ((child->kind == R_HIR_BLOCK) && symbol->has_definition) {
            if (!r_mir_lower_statement(&build, child_id, UINT32_C(0))) {
                goto cleanup;
            }
        }
    }
    if (symbol->has_definition && !build.blocks[build.current_block].terminated) {
        const RSemanticType *return_type = r_semantic_type(context, node->type);
        RMirInstruction terminator;
        if (return_type == NULL) {
            goto cleanup;
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = return_type->kind == R_SEMANTIC_TYPE_VOID ? R_MIR_INSTRUCTION_RETURN
                                                                    : R_MIR_INSTRUCTION_UNREACHABLE;
        terminator.span = node->span;
        terminator.type = node->type;
        if (!r_mir_append_instruction(&build, terminator)) {
            goto cleanup;
        }
    }
    if ((symbol->is_async && !r_mir_validate_await_liveness(&build, symbol)) ||
        !r_mir_append_function_storage(context, &build, node->symbol, node->type)) {
        goto cleanup;
    }
    success = true;

cleanup:
    r_mir_destroy_build(&build);
    return success;
}

static int r_mir_compare_text(const uint8_t *left,
                              size_t left_length,
                              const uint8_t *right,
                              size_t right_length) {
    size_t common = left_length < right_length ? left_length : right_length;
    int comparison = common == 0U ? 0 : memcmp(left, right, common);

    if (comparison != 0) {
        return comparison;
    }
    if (left_length < right_length) {
        return -1;
    }
    return left_length > right_length ? 1 : 0;
}

static int
r_mir_compare_symbols(const RFrontendContext *context, RSymbolId left_id, RSymbolId right_id) {
    const RSemanticSymbol *left_symbol;
    const RSemanticSymbol *right_symbol;
    const RSource *left_source;
    const RSource *right_source;
    const char *left_module;
    const char *right_module;
    int comparison;

    if ((left_id == R_SYMBOL_ID_INVALID) || (right_id == R_SYMBOL_ID_INVALID) ||
        ((size_t)left_id > context->semantic_symbol_count) ||
        ((size_t)right_id > context->semantic_symbol_count)) {
        return 0;
    }
    left_symbol = &context->semantic_symbols[(size_t)left_id - 1U];
    right_symbol = &context->semantic_symbols[(size_t)right_id - 1U];
    left_source = r_get_source_const(context, left_symbol->module_source);
    right_source = r_get_source_const(context, right_symbol->module_source);
    if ((left_source == NULL) || (right_source == NULL)) {
        return 0;
    }
    left_module = left_source->module_name == NULL ? "" : left_source->module_name;
    right_module = right_source->module_name == NULL ? "" : right_source->module_name;
    comparison = strcmp(left_module, right_module);
    if (comparison != 0) {
        return comparison;
    }
    return r_mir_compare_text(
        (const uint8_t *)context
            ->intern_entries[(left_symbol->is_overloaded && left_symbol->generic_origin == 0U
                                  ? left_symbol->overload_identity
                                  : left_symbol->name_intern_id) -
                             1U]
            .bytes,
        context
            ->intern_entries[(left_symbol->is_overloaded && left_symbol->generic_origin == 0U
                                  ? left_symbol->overload_identity
                                  : left_symbol->name_intern_id) -
                             1U]
            .length,
        (const uint8_t *)context
            ->intern_entries[(right_symbol->is_overloaded && right_symbol->generic_origin == 0U
                                  ? right_symbol->overload_identity
                                  : right_symbol->name_intern_id) -
                             1U]
            .bytes,
        context
            ->intern_entries[(right_symbol->is_overloaded && right_symbol->generic_origin == 0U
                                  ? right_symbol->overload_identity
                                  : right_symbol->name_intern_id) -
                             1U]
            .length);
}

static int
r_mir_compare_functions(const RFrontendContext *context, RHirNodeId left_id, RHirNodeId right_id) {
    const RHirNode *left_node = r_mir_hir_node(context, left_id);
    const RHirNode *right_node = r_mir_hir_node(context, right_id);

    if ((left_node == NULL) || (right_node == NULL)) {
        return 0;
    }
    return r_mir_compare_symbols(context, left_node->symbol, right_node->symbol);
}

static bool
r_mir_collect_functions(RFrontendContext *context, RHirNodeId **functions, size_t *function_count) {
    const RHirNode *program = r_mir_hir_node(context, context->hir_root);
    size_t capacity = 0U;
    uint32_t module_index;

    *functions = NULL;
    *function_count = 0U;
    if ((program == NULL) || (program->kind != R_HIR_PROGRAM)) {
        return false;
    }
    for (module_index = 0U; module_index < program->child_count; ++module_index) {
        const RHirNode *module =
            r_mir_hir_node(context, r_mir_hir_child(context, program, module_index));
        uint32_t function_index;
        if ((module == NULL) || (module->kind != R_HIR_MODULE)) {
            return false;
        }
        for (function_index = 0U; function_index < module->child_count; ++function_index) {
            RHirNodeId function_id = r_mir_hir_child(context, module, function_index);
            const RHirNode *function = r_mir_hir_node(context, function_id);
            size_t insert_index;
            if ((function == NULL) || (function->kind != R_HIR_FUNCTION) ||
                !r_grow_array(context,
                              (void **)functions,
                              &capacity,
                              sizeof(**functions),
                              *function_count + 1U)) {
                return false;
            }
            insert_index = *function_count;
            while ((insert_index != 0U) &&
                   (r_mir_compare_functions(context, (*functions)[insert_index - 1U], function_id) >
                    0)) {
                (*functions)[insert_index] = (*functions)[insert_index - 1U];
                insert_index -= 1U;
            }
            (*functions)[insert_index] = function_id;
            *function_count += 1U;
        }
    }
    return true;
}

static bool
r_mir_hir_is_supported(const RFrontendContext *context,
                       RHirNodeId node_id,
                       uint32_t depth,
                       bool *too_deep) {
    const RHirNode *node = r_mir_hir_node(context, node_id);
    uint32_t child_index;

    if (node == NULL) {
        return false;
    }
    if (depth > r_frontend_tree_depth_limit(context)) {
        *too_deep = true;
        return false;
    }
    switch (node->kind) {
    case R_HIR_PROGRAM:
    case R_HIR_MODULE:
    case R_HIR_FUNCTION:
    case R_HIR_PARAMETER:
    case R_HIR_BLOCK:
    case R_HIR_LOCAL:
    case R_HIR_RETURN:
    case R_HIR_TYPE_QUERY:
    case R_HIR_LITERAL:
    case R_HIR_VALUE_SCOPE:
    case R_HIR_STRING_LITERAL:
    case R_HIR_ENUM_CONSTANT:
    case R_HIR_DEFAULT_VALUE:
    case R_HIR_NEW:
    case R_HIR_FIELD_INIT:
    case R_HIR_AGGREGATE_INIT:
    case R_HIR_ARRAY_INIT:
    case R_HIR_VARIANT:
    case R_HIR_PLACE:
    case R_HIR_FIELD_PLACE:
    case R_HIR_INDEX_PLACE:
    case R_HIR_DEREF_PLACE:
    case R_HIR_BORROW:
    case R_HIR_SLICE:
    case R_HIR_LENGTH:
    case R_HIR_LOAD:
    case R_HIR_MOVE:
    case R_HIR_VARIANT_TAG:
    case R_HIR_VARIANT_PAYLOAD:
    case R_HIR_DROP:
    case R_HIR_UNARY:
    case R_HIR_CAST:
    case R_HIR_DISCARD:
    case R_HIR_CONDITIONAL:
    case R_HIR_BINARY:
    case R_HIR_FUNCTION_ADDRESS:
    case R_HIR_INDIRECT_CALL:
    case R_HIR_CALL:
    case R_HIR_ASYNC_START:
    case R_HIR_AWAIT:
    case R_HIR_TASK_SCOPE_ENTER:
    case R_HIR_TASK_SCOPE_WAIT:
    case R_HIR_TASK_SCOPE_CLOSE:
    case R_HIR_STANDARD_CALL:
    case R_HIR_ASSIGN:
    case R_HIR_COMPOUND_ASSIGN:
    case R_HIR_EXPRESSION_STATEMENT:
    case R_HIR_IF:
    case R_HIR_WHILE:
    case R_HIR_FOR:
    case R_HIR_BREAK:
    case R_HIR_CONTINUE:
    case R_HIR_TRY:
    case R_HIR_CATCH:
    case R_HIR_FINALLY:
    case R_HIR_THROW:
    case R_HIR_SWITCH:
    case R_HIR_CASE:
        break;
    case R_HIR_INVALID:
    default:
        return false;
    }
    if ((node->symbol != R_SYMBOL_ID_INVALID) &&
        (((size_t)node->symbol > context->semantic_symbol_count) ||
         context->semantic_symbols[(size_t)node->symbol - 1U].poisoned)) {
        return false;
    }
    if ((node->borrow_origin != R_SYMBOL_ID_INVALID) &&
        (size_t)node->borrow_origin > context->semantic_symbol_count) {
        return false;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        if (!r_mir_hir_is_supported(
                context, r_mir_hir_child(context, node, child_index), depth + 1U, too_deep)) {
            return false;
        }
    }
    return true;
}

RFrontendStatus r_frontend_lower_mir(RFrontendContext *context) {
    RHirNodeId *functions = NULL;
    size_t function_count = 0U;
    size_t function_index;
    size_t diagnostic_count;
    RFrontendStatus status;
    bool too_deep = false;

    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    if (context->mir_lowered) {
        return R_FRONTEND_OK;
    }
    if (!context->semantic_analyzed) {
        status = r_frontend_analyze(context);
        if (status != R_FRONTEND_OK) {
            return status;
        }
    }
    if ((context->diagnostic_count != 0U) || (context->hir_root == R_HIR_NODE_ID_INVALID)) {
        return R_FRONTEND_NOT_LOWERABLE;
    }
    if (!r_mir_hir_is_supported(context, context->hir_root, UINT32_C(0), &too_deep)) {
        if (too_deep) {
            /* R-LIMIT-0003: a tree deeper than a lowering walk admits is a translation limit. */
            context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
            r_frontend_diagnose_limit(context, "MIR lowering exceeded the nesting limit");
            return R_FRONTEND_LIMIT_EXCEEDED;
        }
        return R_FRONTEND_NOT_LOWERABLE;
    }
    diagnostic_count = context->diagnostic_count;
    context->mir_function_count = 0U;
    context->mir_block_count = 0U;
    context->mir_instruction_count = 0U;
    context->mir_operand_count = 0U;
    if (!r_mir_collect_functions(context, &functions, &function_count)) {
        r_context_free(context, functions);
        return context->resource_status == R_FRONTEND_OK ? R_FRONTEND_INTERNAL_ERROR
                                                         : context->resource_status;
    }
    for (function_index = 0U; function_index < function_count; ++function_index) {
        if (!r_mir_lower_function(context, functions[function_index])) {
            r_context_free(context, functions);
            context->mir_function_count = 0U;
            context->mir_block_count = 0U;
            context->mir_instruction_count = 0U;
            context->mir_operand_count = 0U;
            if (context->resource_status == R_FRONTEND_LIMIT_EXCEEDED) {
                r_frontend_diagnose_limit(context, "MIR lowering exceeded a translation limit");
            }
            return context->resource_status == R_FRONTEND_OK ? R_FRONTEND_INTERNAL_ERROR
                                                             : context->resource_status;
        }
    }
    r_context_free(context, functions);
    if (context->diagnostic_count != diagnostic_count) {
        context->mir_function_count = 0U;
        context->mir_block_count = 0U;
        context->mir_instruction_count = 0U;
        context->mir_operand_count = 0U;
        return R_FRONTEND_NOT_LOWERABLE;
    }
    context->mir_lowered = true;
    return R_FRONTEND_OK;
}

const char *r_mir_instruction_kind_name(RMirInstructionKind kind) {
    switch (kind) {
    case R_MIR_INSTRUCTION_PARAMETER:
        return "parameter";
    case R_MIR_INSTRUCTION_LOCAL:
        return "local";
    case R_MIR_INSTRUCTION_TYPE_QUERY:
        return "type_query";
    case R_MIR_INSTRUCTION_CONSTANT:
        return "constant";
    case R_MIR_INSTRUCTION_STRING:
        return "string";
    case R_MIR_INSTRUCTION_AGGREGATE:
        return "aggregate";
    case R_MIR_INSTRUCTION_NEW:
        return "new";
    case R_MIR_INSTRUCTION_ARRAY:
        return "array";
    case R_MIR_INSTRUCTION_VARIANT:
        return "variant";
    case R_MIR_INSTRUCTION_VARIANT_TAG:
        return "variant_tag";
    case R_MIR_INSTRUCTION_VARIANT_PAYLOAD:
        return "variant_payload";
    case R_MIR_INSTRUCTION_FIELD:
        return "field";
    case R_MIR_INSTRUCTION_INDEX:
        return "index";
    case R_MIR_INSTRUCTION_DEREF:
        return "deref";
    case R_MIR_INSTRUCTION_BORROW:
        return "borrow";
    case R_MIR_INSTRUCTION_RAW_ADDRESS:
        return "raw_address";
    case R_MIR_INSTRUCTION_SLICE:
        return "slice";
    case R_MIR_INSTRUCTION_LENGTH:
        return "length";
    case R_MIR_INSTRUCTION_LOAD:
        return "load";
    case R_MIR_INSTRUCTION_MOVE:
        return "move";
    case R_MIR_INSTRUCTION_DROP:
        return "drop";
    case R_MIR_INSTRUCTION_UNARY:
        return "unary";
    case R_MIR_INSTRUCTION_CAST:
        return "cast";
    case R_MIR_INSTRUCTION_BINARY:
        return "binary";
    case R_MIR_INSTRUCTION_FUNCTION_ADDRESS:
        return "function_address";
    case R_MIR_INSTRUCTION_INDIRECT_CALL:
        return "indirect_call";
    case R_MIR_INSTRUCTION_CALL:
        return "call";
    case R_MIR_INSTRUCTION_ASYNC_START:
        return "async_start";
    case R_MIR_INSTRUCTION_AWAIT:
        return "await";
    case R_MIR_INSTRUCTION_TASK_SCOPE_ENTER:
        return "task_scope_enter";
    case R_MIR_INSTRUCTION_TASK_SCOPE_WAIT:
        return "task_scope_wait";
    case R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE:
        return "task_scope_close";
    case R_MIR_INSTRUCTION_STANDARD_CALL:
        return "standard_call";
    case R_MIR_INSTRUCTION_STORE:
        return "store";
    case R_MIR_INSTRUCTION_PHI:
        return "phi";
    case R_MIR_INSTRUCTION_DISCARD:
        return "discard";
    case R_MIR_INSTRUCTION_BRANCH:
        return "branch";
    case R_MIR_INSTRUCTION_JUMP:
        return "jump";
    case R_MIR_INSTRUCTION_RETURN:
        return "return";
    case R_MIR_INSTRUCTION_THROW:
        return "throw";
    case R_MIR_INSTRUCTION_EFFECT_TAG:
        return "effect_tag";
    case R_MIR_INSTRUCTION_EFFECT_PAYLOAD:
        return "effect_payload";
    case R_MIR_INSTRUCTION_PENDING_SET:
        return "pending_set";
    case R_MIR_INSTRUCTION_PENDING_RESUME:
        return "pending_resume";
    case R_MIR_INSTRUCTION_FINALLY_PUSH:
        return "finally_push";
    case R_MIR_INSTRUCTION_FINALLY_ENTER:
        return "finally_enter";
    case R_MIR_INSTRUCTION_FINALLY_EXIT:
        return "finally_exit";
    case R_MIR_INSTRUCTION_CANCEL:
        return "cancel";
    case R_MIR_INSTRUCTION_UNREACHABLE:
        return "unreachable";
    case R_MIR_INSTRUCTION_INVALID:
    default:
        return "invalid";
    }
}

static const char *r_mir_pending_completion_reason_name(RMirPendingCompletionReason reason) {
    switch (reason) {
    case R_MIR_PENDING_COMPLETION_NORMAL:
        return "normal";
    case R_MIR_PENDING_COMPLETION_RETURN:
        return "return";
    case R_MIR_PENDING_COMPLETION_CHECKED_ERROR:
        return "checked_error";
    case R_MIR_PENDING_COMPLETION_BREAK:
        return "break";
    case R_MIR_PENDING_COMPLETION_CONTINUE:
        return "continue";
    case R_MIR_PENDING_COMPLETION_FALLTHROUGH:
        return "fallthrough";
    case R_MIR_PENDING_COMPLETION_CANCEL:
        return "cancel";
    case R_MIR_PENDING_COMPLETION_PANIC_UNWIND:
        return "panic_unwind";
    case R_MIR_PENDING_COMPLETION_INVALID:
    default:
        return NULL;
    }
}

static bool r_mir_write_indent(RFrontendWriteFn writer, void *user_data, uint32_t depth) {
    uint32_t index;
    for (index = 0U; index < depth; ++index) {
        if (!r_write_text(writer, user_data, "  ")) {
            return false;
        }
    }
    return true;
}

static bool r_mir_write_uint64(RFrontendWriteFn writer, void *user_data, uint64_t value) {
    char digits[20];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (value % UINT64_C(10)));
        count += 1U;
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    while (count != 0U) {
        char digit = digits[count - 1U];
        count -= 1U;
        if (!writer(user_data, &digit, 1U)) {
            return false;
        }
    }
    return true;
}

static const char *r_mir_type_constructor_name(const RSemanticType *type) {
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

static bool r_mir_write_opaque_type(const RFrontendContext *,
                                    const RGenericParameter *,
                                    RFrontendWriteFn,
                                    void *);
static bool r_mir_write_type(const RFrontendContext *context,
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
        const char *prefix = function_value ? "(fn parameters=("
                             : (type->flags & R_SEMANTIC_TYPE_FLAG_CALLABLE) == 0U
                                 ? "(raw_fn parameters=("
                             : (type->flags & R_SEMANTIC_TYPE_FLAG_CALL_ONCE) != 0U
                                 ? "(callable mode=once parameters=("
                             : (type->flags & R_SEMANTIC_TYPE_FLAG_CALL_MUT) != 0U
                                 ? "(callable mode=mut parameters=("
                                 : "(callable mode=shared parameters=(";
        if (!r_write_text(writer, user_data, prefix)) {
            return false;
        }
        for (index = UINT64_C(0); index < type->length; ++index) {
            const RSemanticType *parameter = r_semantic_type(context, parameter_id);
            /* R-TYPE-0053 (L18.4): `P...` expands a pack as the last parameter. */
            const bool expands = (parameter != NULL) && (parameter->length == UINT64_C(1));
            if (parameter == NULL || parameter->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER ||
                (index != UINT64_C(0) && !r_write_text(writer, user_data, " ")) ||
                (expands && !r_write_text(writer, user_data, "(expand ")) ||
                !r_mir_write_type(context, parameter->base, writer, user_data) ||
                (expands && !r_write_text(writer, user_data, ")"))) {
                return false;
            }
            parameter_id = parameter->second;
        }
        return parameter_id == R_TYPE_ID_INVALID && r_write_text(writer, user_data, ") return=") &&
               r_mir_write_type(context, return_id, writer, user_data) &&
               (effects == R_TYPE_ID_INVALID ||
                (r_write_text(writer, user_data, " throws=") &&
                 r_mir_write_type(context, effects, writer, user_data))) &&
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
               r_mir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, ")");
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
        if (parameter->opaque_declaration.source != 0U)
            return r_mir_write_opaque_type(context, parameter, writer, user_data);
        const RInternEntry *name = &context->intern_entries[parameter->name_intern_id - 1U];
        if (parameter->pack_base != R_TYPE_ID_INVALID) {
            /* R-TYPE-0053 (L18.4): an element of a pack, or the pack of a prefix followed by the
               elements of a pack from an offset on. */
            RTypeId link = parameter->pack_prefix;
            if (!parameter->pack_derived) {
                return r_write_text(writer, user_data, "(pack_element ") &&
                       r_mir_write_type(context, parameter->pack_base, writer, user_data) &&
                       r_write_text(writer, user_data, " ") &&
                       r_mir_write_uint64(writer, user_data, parameter->pack_index) &&
                       r_write_text(writer, user_data, ")");
            }
            if (!r_write_text(writer, user_data, "(pack prefix=(")) {
                return false;
            }
            while (link != R_TYPE_ID_INVALID) {
                const RSemanticType *part = r_semantic_type(context, link);
                if ((part == NULL) ||
                    ((link != parameter->pack_prefix) && !r_write_text(writer, user_data, " ")) ||
                    !r_mir_write_type(context, part->base, writer, user_data)) {
                    return false;
                }
                link = part->second;
            }
            return r_write_text(writer, user_data, ") base=") &&
                   r_mir_write_type(context, parameter->pack_base, writer, user_data) &&
                   r_write_text(writer, user_data, " from=") &&
                   r_mir_write_uint64(writer, user_data, parameter->pack_index) &&
                   r_write_text(writer, user_data, ")");
        }
        if (parameter->projection_base != R_TYPE_ID_INVALID) {
            /* R-TYPE-0045: `P::Name` is written as a projection of its base. */
            return r_write_text(writer, user_data, "(projection ") &&
                   r_mir_write_type(context, parameter->projection_base, writer, user_data) &&
                   r_write_text(writer, user_data, " ") &&
                   r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) &&
                   r_write_text(writer, user_data, ")");
        }
        if (parameter->application_base != R_TYPE_ID_INVALID) {
            /* R-TYPE-0045 (L16.3): `A<T...>` is written as its base and its arguments. */
            RTypeId link = parameter->application_arguments;
            if (!r_write_text(writer, user_data, "(application ") ||
                !r_mir_write_type(context, parameter->application_base, writer, user_data)) {
                return false;
            }
            while (link != R_TYPE_ID_INVALID) {
                const RSemanticType *part = r_semantic_type(context, link);
                if ((part == NULL) || !r_write_text(writer, user_data, " ") ||
                    !r_mir_write_type(context, part->base, writer, user_data)) {
                    return false;
                }
                link = part->second;
            }
            return r_write_text(writer, user_data, ")");
        }
        return r_write_text(writer, user_data, "(parameter ") &&
               r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) &&
               r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR && type->flags == (uint32_t)R_TOKEN_DOT) {
        /* R-TYPE-0050: an associated constant is its trait, its name and its owner. */
        const RSemanticTrait *trait = &context->semantic_traits[(type->length >> 32U) - 1U];
        const RSource *source = r_get_source_const(context, trait->module_source);
        const RInternEntry *trait_name = &context->intern_entries[trait->name_intern_id - 1U];
        const RInternEntry *constant_name =
            &context
                 ->intern_entries[context
                                      ->semantic_associated_constants[trait->first_constant +
                                                                      (type->length & UINT32_MAX)]
                                      .name_intern_id -
                                  1U];
        const char *module = trait->is_core ? "core"
                             : (source == NULL || source->module_name == NULL)
                                 ? ""
                                 : source->module_name;
        RTypeId link = type->base;
        if (!r_write_text(writer, user_data, "(constant associated ") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)module, strlen(module)) ||
            !r_write_text(writer, user_data, " ") ||
            !r_write_escaped(
                writer, user_data, (const uint8_t *)trait_name->bytes, trait_name->length) ||
            !r_write_text(writer, user_data, " ") ||
            !r_write_escaped(
                writer, user_data, (const uint8_t *)constant_name->bytes, constant_name->length)) {
            return false;
        }
        while (link != R_TYPE_ID_INVALID) {
            const RSemanticType *part = r_semantic_type(context, link);
            if (part == NULL || !r_write_text(writer, user_data, " ") ||
                !r_mir_write_type(context, part->base, writer, user_data)) {
                return false;
            }
            link = part->second;
        }
        return r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR && type->flags == (uint32_t)R_TOKEN_LPAREN) {
        /* R-TYPE-0047: a computed bound is its formula and the arguments of its declaration. */
        const RInternEntry *formula = &context->intern_entries[type->length - 1U];
        RTypeId link = type->base;
        if (!r_write_text(writer, user_data, "(constant call ") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)formula->bytes, formula->length)) {
            return false;
        }
        while (link != R_TYPE_ID_INVALID) {
            const RSemanticType *part = r_semantic_type(context, link);
            if (part == NULL || !r_write_text(writer, user_data, " ") ||
                !r_mir_write_type(context, part->base, writer, user_data)) {
                return false;
            }
            link = part->second;
        }
        return r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR) {
        if (type->base == 0U && type->second == 0U && type->flags == (uint32_t)R_TOKEN_INVALID) {
            return r_write_text(writer, user_data, "(constant usize ") &&
                   r_mir_write_uint64(writer, user_data, type->length) &&
                   r_write_text(writer, user_data, ")");
        }
        if (type->base == 0U && type->second == 0U) {
            /* R-TYPE-0047: a typed constant is written with its type and value. */
            const RTokenKind token = (RTokenKind)type->flags;
            const uint32_t width = token == R_TOKEN_KW_I8                                   ? 8U
                                   : token == R_TOKEN_KW_I16                                ? 16U
                                   : token == R_TOKEN_KW_I32                                ? 32U
                                   : (token == R_TOKEN_KW_I64 || token == R_TOKEN_KW_ISIZE) ? 64U
                                                                                            : 0U;
            const uint64_t bits = type->length;
            const bool negative = width != 0U && (bits & (UINT64_C(1) << (width - 1U))) != 0U;
            const uint64_t mask = width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
            if (!r_write_text(writer, user_data, "(constant ") ||
                !r_write_text(writer, user_data, r_token_kind_name(token)) ||
                !r_write_text(writer, user_data, " ")) {
                return false;
            }
            if (token == R_TOKEN_KW_BOOL) {
                return r_write_text(writer, user_data, bits != 0U ? "true)" : "false)");
            }
            return (!negative || r_write_text(writer, user_data, "-")) &&
                   r_mir_write_uint64(
                       writer, user_data, negative ? (UINT64_C(0) - bits) & mask : bits) &&
                   r_write_text(writer, user_data, ")");
        }
        return r_write_text(writer, user_data, "(constant ") &&
               r_write_text(writer, user_data, r_token_kind_name((RTokenKind)type->flags)) &&
               r_write_text(writer, user_data, " ") &&
               r_mir_write_uint64(writer, user_data, type->length) &&
               (type->base == 0U || (r_write_text(writer, user_data, " ") &&
                                     r_mir_write_type(context, type->base, writer, user_data))) &&
               (type->second == 0U ||
                (r_write_text(writer, user_data, " ") &&
                 r_mir_write_type(context, type->second, writer, user_data))) &&
               r_write_text(writer, user_data, ")");
    }
    if ((type->kind == R_SEMANTIC_TYPE_CONST) || (type->kind == R_SEMANTIC_TYPE_ATOMIC)) {
        return r_write_text(writer,
                            user_data,
                            type->kind == R_SEMANTIC_TYPE_CONST ? "(const " : "(atomic ") &&
               r_mir_write_type(context, type->base, writer, user_data) &&
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
                !r_mir_write_type(context, type->base, writer, user_data)) {
                return false;
            }
            type = r_semantic_type(context, type->second);
        }
        return (type == NULL) && r_write_text(writer, user_data, ")");
    }
    if (type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
        return r_write_text(writer, user_data, "(carrier ") &&
               r_mir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, " ") &&
               r_mir_write_type(context, type->second, writer, user_data) &&
               r_write_text(writer, user_data, ")");
    }
    constructor_name = r_mir_type_constructor_name(type);
    if (constructor_name != NULL) {
        if (!r_write_text(writer, user_data, "(") ||
            !r_write_text(writer, user_data, constructor_name) ||
            !r_write_text(writer, user_data, " ") ||
            !r_mir_write_type(context, type->base, writer, user_data)) {
            return false;
        }
        if ((type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) &&
            (!r_write_text(writer, user_data, " ") ||
             !(type->second != 0U ? r_mir_write_type(context, type->second, writer, user_data)
                                  : r_mir_write_uint64(writer, user_data, type->length)))) {
            return false;
        }
        if ((type->kind == R_SEMANTIC_TYPE_TASK) && (type->second != R_TYPE_ID_INVALID) &&
            (!r_write_text(writer, user_data, " ") ||
             !r_mir_write_type(context, type->second, writer, user_data))) {
            return false;
        }
        if (((type->kind == R_SEMANTIC_TYPE_BORROW) || (type->kind == R_SEMANTIC_TYPE_OWN) ||
             (type->kind == R_SEMANTIC_TYPE_RAW) || (type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION)) &&
            ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) &&
            !r_write_text(writer, user_data, " nullable")) {
            return false;
        }
        return r_write_text(writer, user_data, ")");
    }
    if ((type->kind == R_SEMANTIC_TYPE_DICT) || (type->kind == R_SEMANTIC_TYPE_RESULT)) {
        return r_write_text(
                   writer, user_data, type->kind == R_SEMANTIC_TYPE_DICT ? "(dict " : "(result ") &&
               r_mir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, " ") &&
               r_mir_write_type(context, type->second, writer, user_data) &&
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
             !r_mir_write_type(context, type->base, writer, user_data))) {
            return false;
        }
        if ((type->second != R_TYPE_ID_INVALID) &&
            (!r_write_text(writer, user_data, " ") ||
             !r_mir_write_type(context, type->second, writer, user_data))) {
            return false;
        }
        return r_write_text(writer, user_data, ")");
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
                    !r_mir_write_type(
                        context,
                        context->generic_arguments[aggregate->first_generic_argument + index],
                        writer,
                        user_data)) {
                    return false;
                }
            }
            return r_write_text(writer, user_data, ")");
        }
        if (aggregate->generic_origin != 0U && r_generic_type_is_dependent(context, type_id, 0U)) {
            const RSemanticAggregate *origin =
                &context->semantic_aggregates[aggregate->generic_origin - 1U];
            const RSource *origin_source = r_get_source_const(context, origin->module_source);
            const RInternEntry *name = &context->intern_entries[origin->name_intern_id - 1U];
            uint32_t index,
                count = context->generic_schemas[aggregate->generic_schema - 1U].parameter_count;
            if (origin_source == NULL || !r_write_text(writer, user_data, "(apply ") ||
                !r_write_escaped(writer,
                                 user_data,
                                 (const uint8_t *)origin_source->module_name,
                                 strlen(origin_source->module_name)) ||
                !r_write_text(writer, user_data, "::") ||
                !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length)) {
                return false;
            }
            for (index = 0U; index < count; ++index) {
                if (!r_write_text(writer, user_data, " ") ||
                    !r_mir_write_type(
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
            /* R-AGG-0011 (L21.3): a value of an error with descendants is written as the family
               of that error; the family is closed when the program is built. */
            const RTypeId root =
                aggregate->error_family_of == R_SEMANTIC_STANDARD_ERROR_ROOT
                    ? context->standard_fault_type
                    : context->semantic_aggregates[aggregate->error_family_of - 1U].type;
            return r_write_text(writer, user_data, "(error_family ") &&
                   r_mir_write_type(context, root, writer, user_data) &&
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
    if (type->kind == R_SEMANTIC_TYPE_TYPE_FUNCTION) {
        /* R-TYPE-0045 (L16.3): a binding with parameters names them and its body. */
        const RGenericSchema *schema = &context->generic_schemas[type->length - 1U];
        if (!r_write_text(writer, user_data, "(type_function parameters=(")) {
            return false;
        }
        for (uint32_t index = 0U; index < schema->parameter_count; ++index) {
            const RInternEntry *name =
                &context->intern_entries
                     [context->generic_parameters[schema->first_parameter + index].name_intern_id -
                      1U];
            if (((index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length)) {
                return false;
            }
        }
        return r_write_text(writer, user_data, ") body=") &&
               r_mir_write_type(context, type->base, writer, user_data) &&
               r_write_text(writer, user_data, ")");
    }
    return r_write_text(writer, user_data, r_semantic_type_kind_name(type->kind));
}

static bool r_mir_write_symbol_name(const RFrontendContext *context,
                                    const RSemanticSymbol *symbol,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    const RSource *source;
    size_t name_length;

    if ((symbol == NULL) || (symbol->name_span.end < symbol->name_span.start)) {
        return false;
    }
    source = r_get_source_const(context, symbol->name_span.source);
    if ((source == NULL) || ((size_t)symbol->name_span.end > source->length)) {
        return false;
    }
    name_length = (size_t)(symbol->name_span.end - symbol->name_span.start);
    return r_write_escaped(writer, user_data, source->bytes + symbol->name_span.start, name_length);
}

static bool r_mir_write_overload(const RFrontendContext *context,
                                 const RSemanticSymbol *symbol,
                                 RFrontendWriteFn writer,
                                 void *user_data) {
    if (!symbol->is_overloaded) {
        return true;
    }
    const RInternEntry *identity = &context->intern_entries[symbol->overload_identity - 1U];
    return r_write_text(writer, user_data, " overload=") &&
           r_write_escaped(writer, user_data, (const uint8_t *)identity->bytes, identity->length);
}

static bool r_mir_write_qualified_symbol(const RFrontendContext *context,
                                         const RSemanticSymbol *symbol,
                                         RFrontendWriteFn writer,
                                         void *user_data) {
    const RSource *source;
    size_t name_length;
    size_t module_length;
    uint8_t separator[2] = {UINT8_C(':'), UINT8_C(':')};

    if ((symbol == NULL) || (symbol->name_span.end < symbol->name_span.start)) {
        return false;
    }
    source = r_get_source_const(context, symbol->module_source);
    if ((source == NULL) || (source->module_name == NULL) ||
        ((size_t)symbol->name_span.end > source->length)) {
        return false;
    }
    module_length = strlen(source->module_name);
    name_length = context->intern_entries[symbol->name_intern_id - 1U].length;
    if (!r_write_text(writer, user_data, "\"") ||
        !r_write_bytes(writer, user_data, source->module_name, module_length) ||
        !r_write_bytes(writer, user_data, (const char *)separator, sizeof(separator)) ||
        !r_write_bytes(writer,
                       user_data,
                       context->intern_entries[symbol->name_intern_id - 1U].bytes,
                       name_length) ||
        !r_write_text(writer, user_data, "\"")) {
        return false;
    }
    return true;
}

static bool r_mir_write_qualified_aggregate(const RFrontendContext *context,
                                            const RSemanticAggregate *aggregate,
                                            RFrontendWriteFn writer,
                                            void *user_data) {
    const RSource *source;
    size_t name_length;

    if ((aggregate == NULL) || (aggregate->name_span.end < aggregate->name_span.start)) {
        return false;
    }
    source = r_get_source_const(context, aggregate->module_source);
    if ((source == NULL) || (source->module_name == NULL) ||
        ((size_t)aggregate->name_span.end > source->length)) {
        return false;
    }
    name_length = context->intern_entries[aggregate->name_intern_id - 1U].length;
    return r_write_text(writer, user_data, "\"") &&
           r_write_text(writer, user_data, source->module_name) &&
           r_write_text(writer, user_data, "::") &&
           r_write_bytes(writer,
                         user_data,
                         context->intern_entries[aggregate->name_intern_id - 1U].bytes,
                         name_length) &&
           r_write_text(writer, user_data, "\"");
}

static bool r_mir_write_value(RFrontendWriteFn writer, void *user_data, RMirValueId value) {
    return r_write_text(writer, user_data, "%v") && r_write_uint32(writer, user_data, value - 1U);
}

static bool
r_mir_write_place(RFrontendWriteFn writer, void *user_data, const RMirInstruction *instruction) {
    return r_write_text(writer, user_data, instruction->place_is_parameter ? "%arg" : "%local") &&
           r_write_uint32(writer, user_data, instruction->place_ordinal);
}

static bool r_mir_write_block(RFrontendWriteFn writer, void *user_data, RMirBlockId block) {
    return (block != R_MIR_BLOCK_ID_INVALID) && r_write_text(writer, user_data, "bb") &&
           r_write_uint32(writer, user_data, block - 1U);
}

static bool r_mir_write_borrow_origin(const RFrontendContext *context,
                                      RSymbolId origin,
                                      RFrontendWriteFn writer,
                                      void *user_data) {
    size_t instruction_index;

    if ((origin != R_SYMBOL_ID_INVALID) && ((size_t)origin <= context->semantic_symbol_count) &&
        (context->semantic_symbols[(size_t)origin - 1U].content_parameter != R_SYMBOL_ID_INVALID)) {
        /* What a parameter holds is written as that parameter (R-BORROW-0018). */
        origin = context->semantic_symbols[(size_t)origin - 1U].content_parameter;
    }
    for (instruction_index = 0U; instruction_index < context->mir_instruction_count;
         ++instruction_index) {
        const RMirInstruction *candidate = &context->mir_instructions[instruction_index];

        if ((candidate->symbol == origin) && ((candidate->kind == R_MIR_INSTRUCTION_PARAMETER) ||
                                              (candidate->kind == R_MIR_INSTRUCTION_LOCAL))) {
            return r_mir_write_place(writer, user_data, candidate);
        }
    }
    if ((origin != R_SYMBOL_ID_INVALID) && ((size_t)origin <= context->semantic_symbol_count)) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[(size_t)origin - 1U];

        if ((symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) ||
            (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT)) {
            return r_mir_write_qualified_symbol(context, symbol, writer, user_data);
        }
        /* A hidden region such as the thread region of a thread_scope has no storage; it is
           written by its name (R-BORROW-0023). */
        if (symbol->is_hidden_local && (symbol->name_intern_id != 0U) &&
            ((size_t)symbol->name_intern_id <= context->intern_count)) {
            const RInternEntry *name = &context->intern_entries[symbol->name_intern_id - 1U];

            return writer(user_data, name->bytes, name->length);
        }
    }
    return false;
}

static bool r_mir_dump_instruction(const RFrontendContext *context,
                                   const RMirInstruction *instruction,
                                   RFrontendWriteFn writer,
                                   void *user_data) {
    const RSemanticSymbol *symbol = NULL;
    uint32_t operand_index;

    if ((instruction->symbol != R_SYMBOL_ID_INVALID) &&
        ((size_t)instruction->symbol <= context->semantic_symbol_count)) {
        symbol = &context->semantic_symbols[(size_t)instruction->symbol - 1U];
    }
    if (!r_mir_write_indent(writer, user_data, UINT32_C(3)) ||
        !r_write_text(writer, user_data, "(")) {
        return false;
    }
    if (instruction->result != R_MIR_VALUE_ID_INVALID) {
        if (!r_mir_write_value(writer, user_data, instruction->result) ||
            !r_write_text(writer, user_data, " = ")) {
            return false;
        }
    }
    if (!r_write_text(writer, user_data, r_mir_instruction_kind_name(instruction->kind))) {
        return false;
    }
    switch (instruction->kind) {
    case R_MIR_INSTRUCTION_PARAMETER:
    case R_MIR_INSTRUCTION_LOCAL:
        if (!r_write_text(writer, user_data, " place=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            !r_write_text(writer, user_data, " name=") ||
            !r_mir_write_symbol_name(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_DEREF:
        if (!r_write_text(writer, user_data, " pointer=") ||
            ((instruction->operand0 == R_MIR_VALUE_ID_INVALID)
                 ? !r_mir_write_place(writer, user_data, instruction)
                 : !r_mir_write_value(writer, user_data, instruction->operand0)) ||
            !r_write_text(writer,
                          user_data,
                          instruction->aggregate_member == UINT32_C(0) ? " shared=false type="
                                                                       : " shared=true type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_BORROW:
    case R_MIR_INSTRUCTION_RAW_ADDRESS:
        if (!r_write_text(writer, user_data, " place=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " projection=") ||
              !r_mir_write_value(writer, user_data, instruction->operand0))) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_TYPE_QUERY:
        if (!r_write_text(writer,
                          user_data,
                          instruction->operation == R_TOKEN_KW_SIZEOF ? " sizeof=" : " alignof=") ||
            !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data) ||
            !r_write_text(writer, user_data, " type=usize")) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_CONSTANT:
        if (!r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer, user_data, " value=") ||
            !r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
            ((instruction->aggregate_member != UINT32_C(0)) &&
             (!r_write_text(writer, user_data, " variant=") ||
              !r_write_uint32(writer, user_data, instruction->aggregate_member)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_STRING: {
        const RInternEntry *entry =
            (instruction->aggregate_member == UINT32_C(0)) ||
                    ((size_t)instruction->aggregate_member > context->intern_count)
                ? NULL
                : &context->intern_entries[(size_t)instruction->aggregate_member - 1U];
        if ((entry == NULL) || (entry->length != (size_t)instruction->integer_value) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer, user_data, " value=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)entry->bytes, entry->length)) {
            return false;
        }
        break;
    }
    case R_MIR_INSTRUCTION_AGGREGATE:
        if (!r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          instruction->integer_value != UINT64_C(0) ? " default=true fields=("
                                                                    : " default=false fields=(")) {
            return false;
        }
        for (operand_index = 0U; operand_index < instruction->operand_count; ++operand_index) {
            const size_t index = (size_t)instruction->first_operand + (size_t)operand_index;
            if ((index >= context->mir_operand_count) ||
                ((operand_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_write_text(writer, user_data, "(") ||
                !r_write_uint32(writer, user_data, context->mir_operand_members[index]) ||
                !r_write_text(writer, user_data, " ") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[index]) ||
                !r_write_text(writer, user_data, ")")) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ")")) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_NEW:
        if (((instruction->operation != R_TOKEN_KW_OWN) &&
             (instruction->operation != R_TOKEN_KW_ARC) &&
             (instruction->operation != R_TOKEN_KW_RC)) ||
            (instruction->operand0 == R_MIR_VALUE_ID_INVALID) ||
            !r_write_text(writer, user_data, " owner=") ||
            !r_write_text(writer, user_data, r_token_kind_name(instruction->operation)) ||
            !r_write_text(writer, user_data, " payload=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " payload_type=") ||
            !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_ARRAY:
        if (!r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer, user_data, " length=") ||
            !r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
            !r_write_text(writer, user_data, " elements=(")) {
            return false;
        }
        for (operand_index = 0U; operand_index < instruction->operand_count; ++operand_index) {
            const size_t index = (size_t)instruction->first_operand + (size_t)operand_index;
            if ((index >= context->mir_operand_count) ||
                ((operand_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_value(writer, user_data, context->mir_operands[index])) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ")")) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_VARIANT:
        if (!r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer, user_data, " tag=") ||
            !r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
            ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " payload=") ||
              !r_mir_write_value(writer, user_data, instruction->operand0)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_VARIANT_TAG:
        if (!r_write_text(writer, user_data, " value=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_VARIANT_PAYLOAD:
        if (!r_write_text(writer, user_data, " value=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " tag=") ||
            !r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_FIELD:
        if (!r_write_text(writer, user_data, " base=") ||
            ((instruction->operand0 == R_MIR_VALUE_ID_INVALID)
                 ? !r_mir_write_place(writer, user_data, instruction)
                 : !r_mir_write_value(writer, user_data, instruction->operand0)) ||
            !r_write_text(writer, user_data, " member=") ||
            !r_write_uint32(writer, user_data, instruction->aggregate_member) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_INDEX:
        if (!r_write_text(writer, user_data, " base=") ||
            ((instruction->operand0 == R_MIR_VALUE_ID_INVALID)
                 ? !r_mir_write_place(writer, user_data, instruction)
                 : !r_mir_write_value(writer, user_data, instruction->operand0)) ||
            !r_write_text(writer, user_data, " index=") ||
            !r_mir_write_value(writer, user_data, instruction->operand1) ||
            !r_write_text(writer,
                          user_data,
                          instruction->integer_value == UINT64_MAX ? " bound=dynamic checked=true"
                                                                   : " bound=") ||
            ((instruction->integer_value != UINT64_MAX) &&
             (!r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
              !r_write_text(writer, user_data, " checked=true"))) ||
            !r_write_text(writer,
                          user_data,
                          instruction->aggregate_member == UINT32_C(0) ? " readonly=false type="
                                                                       : " readonly=true type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_SLICE:
        if (!r_write_text(writer, user_data, " base=") ||
            ((instruction->operand0 == R_MIR_VALUE_ID_INVALID)
                 ? !r_mir_write_place(writer, user_data, instruction)
                 : !r_mir_write_value(writer, user_data, instruction->operand0)) ||
            ((instruction->operation == R_TOKEN_KW_ARRAY) &&
             !r_write_text(writer, user_data, " source=std.array")) ||
            ((instruction->operation != R_TOKEN_KW_ARRAY) &&
             (instruction->operand1 == R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " length=") ||
              !r_mir_write_uint64(writer, user_data, instruction->integer_value))) ||
            ((instruction->operation != R_TOKEN_KW_ARRAY) &&
             (instruction->operand1 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " lower=") ||
              !r_mir_write_value(writer, user_data, instruction->operand1) ||
              !r_write_text(writer, user_data, " upper=") ||
              !r_mir_write_value(writer, user_data, instruction->operand2) ||
              !r_write_text(writer,
                            user_data,
                            instruction->integer_value == UINT64_MAX ? " bound=dynamic checked=true"
                                                                     : " bound=") ||
              ((instruction->integer_value != UINT64_MAX) &&
               (!r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
                !r_write_text(writer, user_data, " checked=true"))))) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_LENGTH:
        if (!r_write_text(writer, user_data, " base=") ||
            ((instruction->operand0 == R_MIR_VALUE_ID_INVALID)
                 ? !r_mir_write_place(writer, user_data, instruction)
                 : !r_mir_write_value(writer, user_data, instruction->operand0)) ||
            !r_write_text(writer,
                          user_data,
                          instruction->integer_value == UINT64_MAX ? " bound=dynamic type="
                                                                   : " bound=") ||
            ((instruction->integer_value != UINT64_MAX) &&
             (!r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
              !r_write_text(writer, user_data, " type="))) ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_LOAD:
        if (!r_write_text(writer, user_data, " place=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            ((instruction->operand1 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " projection=") ||
              !r_mir_write_value(writer, user_data, instruction->operand1)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_MOVE:
        if (!r_write_text(writer, user_data, " source=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            (instruction->is_async_staged_move &&
             !r_write_text(writer, user_data, " async_staged=true")) ||
            ((instruction->operand1 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " projection=") ||
              !r_mir_write_value(writer, user_data, instruction->operand1)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_DROP:
        if (!r_write_text(writer, user_data, " place=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            ((instruction->operand1 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " projection=") ||
              !r_mir_write_value(writer, user_data, instruction->operand1)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_UNARY:
        if (!r_write_text(writer, user_data, " op=") ||
            !r_write_escaped(writer,
                             user_data,
                             (const uint8_t *)r_token_kind_name(instruction->operation),
                             strlen(r_token_kind_name(instruction->operation))) ||
            !r_write_text(writer, user_data, " operand=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_CAST:
        if (!r_write_text(writer, user_data, " operand=") ||
            ((instruction->operand0 == R_MIR_VALUE_ID_INVALID)
                 ? !r_write_text(writer, user_data, "never")
                 : !r_mir_write_value(writer, user_data, instruction->operand0)) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          instruction->operation == R_TOKEN_KW_AS ? " explicit" : " implicit")) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_BINARY:
        if (!r_write_text(writer, user_data, " op=") ||
            !r_write_escaped(writer,
                             user_data,
                             (const uint8_t *)r_token_kind_name(instruction->operation),
                             strlen(r_token_kind_name(instruction->operation))) ||
            !r_write_text(writer, user_data, " left=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " right=") ||
            !r_mir_write_value(writer, user_data, instruction->operand1) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_FUNCTION_ADDRESS:
        if (!r_write_text(writer, user_data, " function=") ||
            !r_mir_write_qualified_symbol(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_INDIRECT_CALL:
    case R_MIR_INSTRUCTION_CALL:
    case R_MIR_INSTRUCTION_ASYNC_START:
        if (!r_write_text(writer, user_data, " callee=") ||
            (instruction->kind != R_MIR_INSTRUCTION_INDIRECT_CALL &&
             !r_mir_write_qualified_symbol(context, symbol, writer, user_data)) ||
            !r_write_text(writer, user_data, " arguments=(")) {
            return false;
        }
        for (operand_index = 0U; operand_index < instruction->operand_count; ++operand_index) {
            size_t index = (size_t)instruction->first_operand + (size_t)operand_index;
            if ((index >= context->mir_operand_count) ||
                ((operand_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_value(writer, user_data, context->mir_operands[index])) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ")")) {
            return false;
        }
        if ((instruction->kind == R_MIR_INSTRUCTION_CALL) &&
            !r_mir_call_borrow_mask_equals(
                &instruction->call_borrow_mask, UINT64_C(0), UINT64_C(0))) {
            bool first_borrow = true;

            if (!r_write_text(writer, user_data, " call_bounded_borrows=(")) {
                return false;
            }
            for (operand_index = UINT32_C(0); operand_index < instruction->operand_count;
                 ++operand_index) {
                if (!r_mir_call_borrow_mask_has(&instruction->call_borrow_mask, operand_index)) {
                    continue;
                }
                if ((!first_borrow && !r_write_text(writer, user_data, " ")) ||
                    !r_write_text(writer, user_data, "arg") ||
                    !r_mir_write_uint64(writer, user_data, (uint64_t)operand_index)) {
                    return false;
                }
                first_borrow = false;
            }
            if (first_borrow || !r_write_text(writer, user_data, ")")) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            ((instruction->kind == R_MIR_INSTRUCTION_ASYNC_START) &&
             (!r_write_text(writer, user_data, " task=") ||
              !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_TASK_SCOPE_ENTER:
    case R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE:
        if (!r_write_text(writer, user_data, " scope=") ||
            !r_mir_write_uint64(writer, user_data, instruction->symbol))
            return false;
        break;
    case R_MIR_INSTRUCTION_TASK_SCOPE_WAIT:
        if (!r_write_text(writer, user_data, " scope=") ||
            !r_mir_write_uint64(writer, user_data, instruction->symbol) ||
            !r_write_text(writer, user_data, " resume=") ||
            !r_mir_write_block(writer, user_data, instruction->target0) ||
            !r_write_text(writer, user_data, " cancel=") ||
            !r_mir_write_block(writer, user_data, instruction->target1))
            return false;
        break;
    case R_MIR_INSTRUCTION_AWAIT:
        if (!r_write_text(writer, user_data, " source=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            ((instruction->operand1 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " projection=") ||
              !r_mir_write_value(writer, user_data, instruction->operand1))) ||
            !r_write_text(writer, user_data, " task=") ||
            !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            !r_write_text(writer, user_data, " resume=") ||
            !r_mir_write_block(writer, user_data, instruction->target0) ||
            !r_write_text(writer, user_data, " cancel=") ||
            !r_mir_write_block(writer, user_data, instruction->target1) ||
            !r_write_text(writer, user_data, " consuming")) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_STANDARD_CALL: {
        const char *operation_name = NULL;
        const RAsyncSyncDescriptor *async_sync =
            r_async_sync_descriptor(instruction->standard_operation);
        switch (instruction->standard_operation) {
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
            operation_name = instruction->integer_value == UINT64_C(1) ? "core::slice_from_raw_parts_in"
                                                                 : "core::slice_from_raw_parts";
            break;
        case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT:
            operation_name = instruction->integer_value == UINT64_C(1)
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
        case R_STANDARD_CALL_ERROR_ERASURE: {
            const RStandardErrorErasureDescriptor *descriptor =
                r_standard_error_erasure(instruction->integer_value);
            if (descriptor == NULL) {
                return false;
            }
            operation_name = descriptor->qualified_name;
            break;
        }
        case R_STANDARD_CALL_MATH_OPERATION: {
            const RStandardMathOperationDescriptor *descriptor =
                r_standard_math_operation(instruction->integer_value);
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
            ((instruction->standard_operation == R_STANDARD_CALL_MATH_OPERATION) &&
             !r_write_text(writer, user_data, "std.math::")) ||
            ((async_sync != NULL) &&
             (!r_write_text(writer, user_data, "std.") ||
              !r_write_text(writer, user_data, async_sync->module) ||
              !r_write_text(writer, user_data, "::"))) ||
            !r_write_text(writer, user_data, operation_name)) {
            return false;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE) ||
            (instruction->standard_operation == R_STANDARD_CALL_FS_AS_ERROR) ||
            (instruction->standard_operation == R_STANDARD_CALL_IO_AS_ERROR)) {
            const size_t source_index = (size_t)instruction->first_operand;
            const uint64_t expected_borrow_mask =
                instruction->standard_operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE
                    ? UINT64_C(1)
                    : UINT64_C(0);

            if ((instruction->operand_count != UINT32_C(1)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, expected_borrow_mask, UINT64_C(0)) ||
                (source_index >= context->mir_operand_count) ||
                !r_write_text(writer, user_data, " source=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[source_index]) ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " input=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_ARC_CLONE) ||
            (instruction->standard_operation == R_STANDARD_CALL_RC_CLONE)) {
            if ((instruction->operand_count == UINT32_C(0)) &&
                (!r_write_text(writer, user_data, " source=") ||
                 !r_mir_write_place(writer, user_data, instruction) ||
                 ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
                  (!r_write_text(writer, user_data, " projection=") ||
                   !r_mir_write_value(writer, user_data, instruction->operand0))))) {
                return false;
            }
            if (instruction->operand_count == UINT32_C(1)) {
                const size_t source_index = (size_t)instruction->first_operand;

                if ((source_index >= context->mir_operand_count) ||
                    !r_write_text(writer, user_data, " source=") ||
                    !r_mir_write_value(writer, user_data, context->mir_operands[source_index])) {
                    return false;
                }
            } else if (instruction->operand_count != UINT32_C(0)) {
                return false;
            }
            if (!r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " pointee=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_TIME_SLEEP_FOR) ||
            (instruction->standard_operation == R_STANDARD_CALL_TIME_SLEEP_UNTIL)) {
            const size_t argument_index = (size_t)instruction->first_operand;

            if ((instruction->operand_count != UINT32_C(1)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, UINT64_C(0), UINT64_C(0)) ||
                (argument_index >= context->mir_operand_count) ||
                !r_write_text(writer, user_data, " argument=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[argument_index]) ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if (instruction->standard_operation == R_STANDARD_CALL_IO_FLUSH) {
            const bool stream_is_value = instruction->operand_count == UINT32_C(2);
            const size_t stream_index = (size_t)instruction->first_operand;
            const size_t deadline_index = stream_index + (stream_is_value ? 1U : 0U);

            if ((!stream_is_value && (instruction->operand_count != UINT32_C(1))) ||
                !r_mir_call_borrow_mask_equals(&instruction->call_borrow_mask,
                                               stream_is_value ? UINT64_C(1) : UINT64_C(0),
                                               UINT64_C(0)) ||
                (deadline_index >= context->mir_operand_count) ||
                !r_write_text(writer, user_data, " stream=") ||
                (stream_is_value
                     ? ((stream_index >= context->mir_operand_count) ||
                        !r_mir_write_value(writer, user_data, context->mir_operands[stream_index]))
                     : (!r_mir_write_place(writer, user_data, instruction) ||
                        ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
                         (!r_write_text(writer, user_data, " projection=") ||
                          !r_mir_write_value(writer, user_data, instruction->operand0))))) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[deadline_index]) ||
                (stream_is_value &&
                 !r_write_text(writer, user_data, " call_bounded_borrows=(stream)")) ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_IO_CLOSE_INPUT) ||
            (instruction->standard_operation == R_STANDARD_CALL_IO_CLOSE_OUTPUT)) {
            const size_t first_index = (size_t)instruction->first_operand;

            if ((instruction->operand_count != UINT32_C(2)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, UINT64_C(0), UINT64_C(0)) ||
                (context->mir_operand_count < UINT32_C(2)) ||
                (first_index > (context->mir_operand_count - UINT32_C(2))) ||
                !r_write_text(writer, user_data, " stream=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index]) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 1U]) ||
                !r_write_text(writer, user_data, " staged_moves=(stream)") ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_IO_READ) ||
            (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE) ||
            (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE_ALL)) {
            const size_t first_index = (size_t)instruction->first_operand;

            if ((instruction->operand_count != UINT32_C(3)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
                (context->mir_operand_count < UINT32_C(3)) ||
                (first_index > (context->mir_operand_count - UINT32_C(3))) ||
                !r_write_text(writer, user_data, " stream=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index]) ||
                !r_write_text(writer, user_data, " buffer=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 1U]) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 2U]) ||
                !r_write_text(writer, user_data, " call_bounded_borrows=(stream)") ||
                !r_write_text(writer, user_data, " staged_moves=(buffer)") ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE_SHARED) {
            const size_t first_index = (size_t)instruction->first_operand;

            if ((instruction->operand_count != UINT32_C(5)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
                (context->mir_operand_count < UINT32_C(5)) ||
                (first_index > (context->mir_operand_count - UINT32_C(5))) ||
                !r_write_text(writer, user_data, " stream=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index]) ||
                !r_write_text(writer, user_data, " buffer=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 1U]) ||
                !r_write_text(writer, user_data, " offset=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 2U]) ||
                !r_write_text(writer, user_data, " length=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 3U]) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 4U]) ||
                !r_write_text(writer, user_data, " call_bounded_borrows=(stream)") ||
                !r_write_text(writer, user_data, " staged_moves=(buffer)") ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_FS_READ_FILE) ||
            (instruction->standard_operation == R_STANDARD_CALL_FS_OPEN_DIRECTORY) ||
            (instruction->standard_operation == R_STANDARD_CALL_FS_OPEN_FILE)) {
            const bool is_read_file =
                instruction->standard_operation == R_STANDARD_CALL_FS_READ_FILE;
            const bool is_open_file =
                instruction->standard_operation == R_STANDARD_CALL_FS_OPEN_FILE;
            const uint32_t expected_count =
                (is_read_file || is_open_file) ? UINT32_C(2) : UINT32_C(1);
            const bool path_is_value = instruction->operand_count == expected_count + UINT32_C(1);
            const size_t first_index = (size_t)instruction->first_operand;
            const size_t argument_index = first_index + (path_is_value ? 1U : 0U);
            const size_t deadline_index =
                argument_index + ((is_read_file || is_open_file) ? 1U : 0U);

            if ((!path_is_value && (instruction->operand_count != expected_count)) ||
                !r_mir_call_borrow_mask_equals(&instruction->call_borrow_mask,
                                               path_is_value ? UINT64_C(1) : UINT64_C(0),
                                               UINT64_C(0)) ||
                (deadline_index >= context->mir_operand_count) ||
                !r_write_text(writer, user_data, " path=") ||
                (path_is_value
                     ? ((first_index >= context->mir_operand_count) ||
                        !r_mir_write_value(writer, user_data, context->mir_operands[first_index]))
                     : (!r_mir_write_place(writer, user_data, instruction) ||
                        ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
                         (!r_write_text(writer, user_data, " projection=") ||
                          !r_mir_write_value(writer, user_data, instruction->operand0))))) ||
                (is_read_file &&
                 ((argument_index >= context->mir_operand_count) ||
                  !r_write_text(writer, user_data, " limit=") ||
                  !r_mir_write_value(writer, user_data, context->mir_operands[argument_index]))) ||
                (is_open_file &&
                 ((argument_index >= context->mir_operand_count) ||
                  !r_write_text(writer, user_data, " options=") ||
                  !r_mir_write_value(writer, user_data, context->mir_operands[argument_index]))) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[deadline_index]) ||
                (path_is_value &&
                 !r_write_text(writer, user_data, " call_bounded_borrows=(path)")) ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if (instruction->standard_operation == R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) {
            const size_t first_index = (size_t)instruction->first_operand;

            if ((instruction->operand_count != UINT32_C(4)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, UINT64_C(3), UINT64_C(0)) ||
                (context->mir_operand_count < UINT32_C(4)) ||
                (first_index > (context->mir_operand_count - UINT32_C(4))) ||
                !r_write_text(writer, user_data, " root=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index]) ||
                !r_write_text(writer, user_data, " relative=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 1U]) ||
                !r_write_text(writer, user_data, " recursive=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 2U]) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 3U]) ||
                !r_write_text(writer, user_data, " call_bounded_borrows=(root relative)") ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if (instruction->standard_operation ==
            R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH) {
            const size_t first_index = (size_t)instruction->first_operand;

            if ((instruction->operand_count != UINT32_C(4)) ||
                !r_mir_call_borrow_mask_equals(
                    &instruction->call_borrow_mask, UINT64_C(3), UINT64_C(0)) ||
                (context->mir_operand_count < UINT32_C(4)) ||
                (first_index > (context->mir_operand_count - UINT32_C(4))) ||
                !r_write_text(writer, user_data, " root=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index]) ||
                !r_write_text(writer, user_data, " relative=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 1U]) ||
                !r_write_text(writer, user_data, " data=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 2U]) ||
                !r_write_text(writer, user_data, " deadline=") ||
                !r_mir_write_value(writer, user_data, context->mir_operands[first_index + 3U]) ||
                !r_write_text(writer, user_data, " call_bounded_borrows=(root relative)") ||
                !r_write_text(writer, user_data, " staged_moves=(data)") ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, instruction->type, writer, user_data) ||
                !r_write_text(writer, user_data, " task=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            break;
        }
        if (!r_write_text(writer, user_data, " arguments=(")) {
            return false;
        }
    }
        for (operand_index = 0U; operand_index < instruction->operand_count; ++operand_index) {
            const size_t index = (size_t)instruction->first_operand + (size_t)operand_index;
            if ((index >= context->mir_operand_count) ||
                ((operand_index != UINT32_C(0)) && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_value(writer, user_data, context->mir_operands[index])) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ")") ||
            ((instruction->standard_operation == R_STANDARD_CALL_ARRAY_PUSH) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(target)"))) ||
            ((instruction->standard_operation == R_STANDARD_CALL_BYTES_APPEND) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(3), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(target source)"))) ||
            ((instruction->standard_operation == R_STANDARD_CALL_BYTES_EQUAL) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(3), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(left right)"))) ||
            (((instruction->standard_operation >= R_STANDARD_CALL_BYTES_APPEND_U8) &&
              (instruction->standard_operation <= R_STANDARD_CALL_BYTES_APPEND_U64_LE)) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(target)"))) ||
            (((instruction->standard_operation >= R_STANDARD_CALL_HASH_CRC32) &&
              (instruction->standard_operation <= R_STANDARD_CALL_HASH_SHA512)) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(source)"))) ||
            (((instruction->standard_operation == R_STANDARD_CALL_UTF8_IS_VALID) ||
              (instruction->standard_operation == R_STANDARD_CALL_UTF8_VALIDATE)) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(source)"))) ||
            ((instruction->standard_operation == R_STANDARD_CALL_NET_PARSE_IP) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(source)"))) ||
            (((instruction->standard_operation == R_STANDARD_CALL_THREAD_CLONE) ||
              (instruction->standard_operation == R_STANDARD_CALL_THREAD_UNPARK) ||
              (instruction->standard_operation == R_STANDARD_CALL_THREAD_PANIC_CATEGORY) ||
              (instruction->standard_operation == R_STANDARD_CALL_THREAD_PANIC_TEXT)) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(source)"))) ||
            ((instruction->standard_operation == R_STANDARD_CALL_BITS_READ) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(3), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(input reader)"))) ||
            ((instruction->standard_operation == R_STANDARD_CALL_BITS_ALIGN_BYTE) &&
             (!r_mir_call_borrow_mask_equals(
                  &instruction->call_borrow_mask, UINT64_C(1), UINT64_C(0)) ||
              !r_write_text(writer, user_data, " call_bounded_borrows=(reader)"))) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
            (instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT) ||
            (instruction->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_LOAD) ||
            (instruction->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_STORE) ||
            (instruction->standard_operation == R_STANDARD_CALL_CORE_ADOPT) ||
            (instruction->standard_operation == R_STANDARD_CALL_CORE_RELEASE) ||
            ((instruction->standard_operation >= R_STANDARD_CALL_CORE_CHECKED_ADD) &&
             (instruction->standard_operation <= R_STANDARD_CALL_CORE_SATURATING_MUL))) {
            if (!r_write_text(writer, user_data, " pointee=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_CORE_ENUM_NAME) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_CORE_VARIANT_NAME)) {
            if (!r_write_text(writer, user_data, " subject=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((instruction->standard_operation == R_STANDARD_CALL_CONVERT_PARSE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_INTEGER) ||
                   (instruction->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED) ||
                   (instruction->standard_operation == R_STANDARD_CALL_C_CHECKED)) {
            if (!r_write_text(writer, user_data, " destination=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data) ||
                !r_write_text(writer, user_data, " tag=") ||
                !r_mir_write_uint64(writer, user_data, instruction->integer_value)) {
                return false;
            }
        } else if ((instruction->standard_operation == R_STANDARD_CALL_ASYNC_CANCEL) ||
                   (instruction->standard_operation == R_STANDARD_CALL_ASYNC_DETACH)) {
            if (!r_write_text(writer, user_data, " task_result=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING) {
            if (!r_write_text(writer, user_data, " start_contract=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
                   (instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
                   (instruction->standard_operation == R_STANDARD_CALL_THREAD_JOIN) ||
                   (instruction->standard_operation == R_STANDARD_CALL_THREAD_DETACH)) {
            if (!r_write_text(writer, user_data, " thread_contract=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
            if ((instruction->standard_operation == R_STANDARD_CALL_THREAD_JOIN) &&
                (!r_write_text(writer, user_data, " completion_storage=") ||
                 !r_mir_write_type(context, instruction->runtime_type, writer, user_data))) {
                return false;
            }
            if (((instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
                 (instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED)) &&
                (!r_write_text(writer, user_data, " entry=") ||
                 !r_mir_write_symbol_name(context, symbol, writer, user_data))) {
                return false;
            }
            if ((instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) &&
                (!r_write_text(writer, user_data, " region=") ||
                 !r_mir_write_uint64(writer, user_data, instruction->integer_value))) {
                return false;
            }
        } else if ((instruction->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT)) {
            if (!r_write_text(writer, user_data, " sync_contract=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data) ||
                !r_write_text(writer, user_data, " initializer=") ||
                !r_mir_write_symbol_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, " callback_storage=") ||
                !r_mir_write_type(context, instruction->runtime_type, writer, user_data)) {
                return false;
            }
        } else if (instruction->standard_operation == R_STANDARD_CALL_IO_STDIN) {
            if (!r_write_text(writer, user_data, " input=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if ((instruction->standard_operation == R_STANDARD_CALL_IO_STDOUT) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_STDERR)) {
            if (!r_write_text(writer, user_data, " output=") ||
                !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
                return false;
            }
        } else if (!r_write_text(
                       writer,
                       user_data,
                       (instruction->standard_operation == R_STANDARD_CALL_ARRAY_CREATE) ||
                               (instruction->standard_operation == R_STANDARD_CALL_ARRAY_PUSH) ||
                               (instruction->standard_operation ==
                                R_STANDARD_CALL_ARRAY_WITH_CAPACITY) ||
                               (instruction->standard_operation == R_STANDARD_CALL_ARRAY_FILLED)
                           ? " element="
                           : " input=") ||
                   !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_STORE:
        if (!r_write_text(writer, user_data, " place=") ||
            !r_mir_write_place(writer, user_data, instruction) ||
            !r_write_text(writer, user_data, " value=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            ((instruction->operand1 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " projection=") ||
              !r_mir_write_value(writer, user_data, instruction->operand1)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_PHI:
        if (!r_write_text(writer, user_data, " incoming=((") ||
            !r_mir_write_block(writer, user_data, instruction->target0) ||
            !r_write_text(writer, user_data, " ") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            (instruction->operand1 != 0U &&
             (!r_write_text(writer, user_data, ") (") ||
              !r_mir_write_block(writer, user_data, instruction->target1) ||
              !r_write_text(writer, user_data, " ") ||
              !r_mir_write_value(writer, user_data, instruction->operand1))) ||
            !r_write_text(writer, user_data, ")) type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_DISCARD:
        if (!r_write_text(writer, user_data, " value=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_BRANCH:
        if (!r_write_text(writer, user_data, " condition=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " then=") ||
            !r_mir_write_block(writer, user_data, instruction->target0) ||
            !r_write_text(writer, user_data, " else=") ||
            !r_mir_write_block(writer, user_data, instruction->target1)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_JUMP:
        if (!r_write_text(writer, user_data, " target=") ||
            !r_mir_write_block(writer, user_data, instruction->target0)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_RETURN:
        if ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
            (!r_write_text(writer, user_data, " value=") ||
             !r_mir_write_value(writer, user_data, instruction->operand0))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_THROW:
        if (!r_write_text(writer, user_data, " value=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            ((instruction->runtime_type != R_TYPE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " relay=") ||
              !r_mir_write_type(context, instruction->runtime_type, writer, user_data))) ||
            !r_write_text(writer, user_data, " error=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data) ||
            ((instruction->target0 == R_MIR_BLOCK_ID_INVALID)
                 ? (!r_write_text(writer, user_data, " propagate_tag=") ||
                    !r_mir_write_uint64(writer, user_data, instruction->integer_value))
                 : (!r_write_text(writer, user_data, " catch=") ||
                    !r_mir_write_block(writer, user_data, instruction->target0)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_EFFECT_TAG:
        if (!r_write_text(writer, user_data, " carrier=") ||
            !r_mir_write_value(writer, user_data, instruction->operand0) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->auxiliary_type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_EFFECT_PAYLOAD:
        if (((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " carrier=") ||
              !r_mir_write_value(writer, user_data, instruction->operand0))) ||
            !r_write_text(writer, user_data, " tag=") ||
            !r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, instruction->type, writer, user_data)) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_PENDING_SET:
    case R_MIR_INSTRUCTION_PENDING_RESUME: {
        const char *reason = r_mir_pending_completion_reason_name(
            (RMirPendingCompletionReason)instruction->integer_value);

        if ((reason == NULL) || !r_write_text(writer, user_data, " reason=") ||
            !r_write_text(writer, user_data, reason) ||
            ((instruction->type != R_TYPE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " type=") ||
              !r_mir_write_type(context, instruction->type, writer, user_data))) ||
            ((instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
             (!r_write_text(writer, user_data, " payload=") ||
              !r_mir_write_value(writer, user_data, instruction->operand0))) ||
            ((instruction->target0 != R_MIR_BLOCK_ID_INVALID) &&
             (!r_write_text(writer, user_data, " target=") ||
              !r_mir_write_block(writer, user_data, instruction->target0))) ||
            !r_write_text(writer, user_data, " resume=") ||
            !r_mir_write_block(writer, user_data, instruction->target1) ||
            !r_write_text(writer, user_data, " stop_depth=") ||
            !r_mir_write_uint64(writer, user_data, (uint64_t)instruction->place_ordinal) ||
            !r_write_text(writer, user_data, " first_finally=") ||
            !r_mir_write_uint64(writer, user_data, (uint64_t)instruction->aggregate_member)) {
            return false;
        }
        break;
    }
    case R_MIR_INSTRUCTION_FINALLY_PUSH:
    case R_MIR_INSTRUCTION_FINALLY_ENTER:
    case R_MIR_INSTRUCTION_FINALLY_EXIT:
        if (!r_write_text(writer, user_data, " id=") ||
            !r_mir_write_uint64(writer, user_data, instruction->integer_value) ||
            !r_write_text(writer, user_data, " depth=") ||
            !r_mir_write_uint64(writer, user_data, (uint64_t)instruction->place_ordinal) ||
            ((instruction->target0 != R_MIR_BLOCK_ID_INVALID) &&
             (!r_write_text(writer, user_data, " target=") ||
              !r_mir_write_block(writer, user_data, instruction->target0)))) {
            return false;
        }
        break;
    case R_MIR_INSTRUCTION_CANCEL:
        break;
    case R_MIR_INSTRUCTION_UNREACHABLE:
        break;
    case R_MIR_INSTRUCTION_INVALID:
    default:
        return false;
    }
    if ((instruction->borrow_origin != R_SYMBOL_ID_INVALID) &&
        ((instruction->borrow_origin != instruction->symbol) ||
         (instruction->kind == R_MIR_INSTRUCTION_BORROW) ||
         (instruction->kind == R_MIR_INSTRUCTION_SLICE) ||
         (instruction->kind == R_MIR_INSTRUCTION_VARIANT_PAYLOAD) ||
         (instruction->kind == R_MIR_INSTRUCTION_CALL)) &&
        (!r_write_text(writer, user_data, " borrow_origin=") ||
         !r_mir_write_borrow_origin(context, instruction->borrow_origin, writer, user_data))) {
        return false;
    }
    return r_write_text(writer, user_data, ")\n");
}

RFrontendStatus
r_frontend_dump_mir(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data) {
    size_t function_index;

    if ((context == NULL) || (writer == NULL) || !context->mir_lowered) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_write_text(
            writer, user_data, "(mir version=1 core_revision=\"" R_FRONTEND_CORE_REVISION "\"\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    for (function_index = 0U; function_index < context->mir_function_count; ++function_index) {
        const RMirFunction *function = &context->mir_functions[function_index];
        const RSemanticSymbol *symbol;
        uint32_t block_index;
        if ((function->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)function->symbol > context->semantic_symbol_count)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        symbol = &context->semantic_symbols[(size_t)function->symbol - 1U];
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(function name=") ||
            !r_mir_write_qualified_symbol(context, symbol, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          symbol->is_protected ? " visibility=protected return="
                                               : " visibility=exported return=") ||
            !r_mir_write_type(context, function->return_type, writer, user_data) ||
            (symbol->is_async &&
             (!r_write_text(writer, user_data, " async=true start=") ||
              !r_mir_write_type(context, symbol->async_start_type, writer, user_data))) ||
            !r_write_text(writer,
                          user_data,
                          symbol->has_definition ? " state=definition\n" : " state=prototype\n")) {
            return R_FRONTEND_IO_ERROR;
        }
        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            size_t global_block = (size_t)function->first_block + (size_t)block_index;
            const RMirBlock *block;
            uint32_t instruction_index;
            if (global_block >= context->mir_block_count) {
                return R_FRONTEND_INTERNAL_ERROR;
            }
            block = &context->mir_blocks[global_block];
            if (!r_mir_write_indent(writer, user_data, UINT32_C(2)) ||
                !r_write_text(writer, user_data, "(block bb") ||
                !r_write_uint32(writer, user_data, block_index) ||
                !r_write_text(writer, user_data, "\n")) {
                return R_FRONTEND_IO_ERROR;
            }
            for (instruction_index = 0U; instruction_index < block->instruction_count;
                 ++instruction_index) {
                size_t index = (size_t)block->first_instruction + (size_t)instruction_index;
                if ((index >= context->mir_instruction_count) ||
                    !r_mir_dump_instruction(
                        context, &context->mir_instructions[index], writer, user_data)) {
                    return R_FRONTEND_IO_ERROR;
                }
            }
            if (!r_mir_write_indent(writer, user_data, UINT32_C(2)) ||
                !r_write_text(writer, user_data, ")\n")) {
                return R_FRONTEND_IO_ERROR;
            }
        }
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, ")\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (!r_write_text(writer, user_data, ")\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}

static bool r_mir_write_bool(RFrontendWriteFn writer, void *user_data, bool value) {
    return r_write_text(writer, user_data, value ? "true" : "false");
}

static bool r_mir_artifact_options_are_valid(const RFrontendArtifactOptions *options);
static bool r_mir_write_manifest_fingerprint(RFrontendWriteFn writer,
                                             void *user_data,
                                             const char *name,
                                             const uint8_t *bytes,
                                             size_t length);
static bool r_mir_write_abi_record_fingerprint(const RFrontendContext *context,
                                               RFrontendWriteFn writer,
                                               void *user_data);
static bool r_mir_write_checked_layout_sha256(const RFrontendContext *context,
                                              const RSemanticSymbol *symbol,
                                              const RFrontendArtifactOptions *options,
                                              RFrontendWriteFn writer,
                                              void *user_data);

static bool r_mir_write_checked_effect_descriptor(const RFrontendContext *context,
                                                  RTypeId value_type,
                                                  RTypeId effect_set,
                                                  const char *carrier,
                                                  RFrontendWriteFn writer,
                                                  void *user_data) {
    const uint32_t effect_count = r_semantic_effect_count(context, effect_set);
    uint32_t effect_index;

    if (!r_write_text(writer, user_data, "value=") ||
        !r_mir_write_type(context, value_type, writer, user_data) ||
        !r_write_text(writer, user_data, " errors=(")) {
        return false;
    }
    for (effect_index = UINT32_C(0); effect_index < effect_count; ++effect_index) {
        if (((effect_index != UINT32_C(0)) && !r_write_text(writer, user_data, " ")) ||
            !r_mir_write_type(context,
                              r_semantic_effect_at(context, effect_set, effect_index),
                              writer,
                              user_data)) {
            return false;
        }
    }
    if (!r_write_text(writer, user_data, ") tags=((tag=0 role=success type=") ||
        !r_mir_write_type(context, value_type, writer, user_data) ||
        !r_write_text(writer, user_data, ")")) {
        return false;
    }
    for (effect_index = UINT32_C(0); effect_index < effect_count; ++effect_index) {
        if (!r_write_text(writer, user_data, " (tag=") ||
            !r_write_uint32(writer, user_data, effect_index + UINT32_C(1)) ||
            !r_write_text(writer, user_data, " role=checked_error type=") ||
            !r_mir_write_type(context,
                              r_semantic_effect_at(context, effect_set, effect_index),
                              writer,
                              user_data) ||
            !r_write_text(writer, user_data, ")")) {
            return false;
        }
    }
    return r_write_text(writer, user_data, ") carrier=") &&
           r_write_text(writer, user_data, carrier);
}

static bool r_mir_write_checked_fields(const RFrontendContext *context,
                                       const RSemanticSymbol *symbol,
                                       RFrontendWriteFn writer,
                                       void *user_data) {
    const char *completion_carrier =
        symbol->throws_type == R_TYPE_ID_INVALID ? "none" : "explicit_output_parameter";

    if (!r_write_text(writer,
                      user_data,
                      symbol->is_async ? "declaration=async " : "declaration=synchronous ") ||
        !r_mir_write_checked_effect_descriptor(context,
                                               symbol->return_type,
                                               symbol->throws_type,
                                               symbol->is_async ? "task_completion"
                                                                : completion_carrier,
                                               writer,
                                               user_data)) {
        return false;
    }
    if (symbol->is_async) {
        const RSemanticType *start = r_semantic_type(context, symbol->async_start_type);

        if ((start == NULL) || (start->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            !r_write_text(writer, user_data, " invocation=(") ||
            !r_mir_write_checked_effect_descriptor(context,
                                                   start->base,
                                                   start->second,
                                                   "explicit_output_parameter",
                                                   writer,
                                                   user_data) ||
            !r_write_text(writer, user_data, ") completion=(") ||
            !r_mir_write_checked_effect_descriptor(context,
                                                   symbol->return_type,
                                                   symbol->throws_type,
                                                   "task_completion",
                                                   writer,
                                                   user_data) ||
            !r_write_text(writer, user_data, ")")) {
            return false;
        }
    }
    return true;
}

static bool r_mir_write_json_schema_sha256(const RFrontendContext *context,
                                           const RSemanticAggregate *aggregate,
                                           RFrontendWriteFn writer,
                                           void *user_data);

#include "json_interface.inc"

#include "borrow_interface.inc"

#include "generic_interface.inc"

static bool r_mir_write_c_export_metadata(const RFrontendContext *context,
                                          const RSemanticSymbol *symbol,
                                          RFrontendWriteFn writer,
                                          void *user_data) {
    RSourceSpan name;
    const RSource *source;
    const uint8_t *bytes;
    size_t length;
    if (!symbol->is_extern_c) {
        return true;
    }
    if (symbol->c_name_intern_id != UINT32_C(0)) {
        const RInternEntry *entry;
        if ((size_t)symbol->c_name_intern_id > context->intern_count) {
            return false;
        }
        entry = &context->intern_entries[symbol->c_name_intern_id - 1U];
        bytes = (const uint8_t *)entry->bytes;
        length = entry->length;
    } else {
        name = symbol->c_name_span.end > symbol->c_name_span.start ? symbol->c_name_span
                                                                   : symbol->name_span;
        source = r_get_source_const(context, name.source);
        if (source == NULL || name.end < name.start || name.end > source->length) {
            return false;
        }
        bytes = source->bytes + name.start;
        length = (size_t)(name.end - name.start);
    }
    return r_write_text(writer, user_data, " callback=") &&
           r_mir_write_bool(writer, user_data, symbol->is_callback) &&
           r_write_text(writer, user_data, " c_name=") &&
           r_write_escaped(writer, user_data, bytes, length);
}

/*
 * R-MOD-0005: the value of an exported object as a canonical term of its checked initializer:
 * scalars as their representation, strings quoted, aggregates and arrays as `{...}` of their
 * elements in declaration order. A changed exported const value changes the interface.
 */
static bool r_mir_write_constant_term(const RFrontendContext *context,
                                      RHirNodeId node_id,
                                      RFrontendWriteFn writer,
                                      void *user_data,
                                      uint32_t depth) {
    const RHirNode *node = r_mir_hir_node(context, node_id);
    uint32_t child_index;

    if ((node == NULL) || (depth > r_frontend_tree_depth_limit(context))) {
        return r_write_text(writer, user_data, "?");
    }
    switch (node->kind) {
    case R_HIR_LITERAL:
    case R_HIR_ENUM_CONSTANT:
        return r_mir_write_uint64(writer, user_data, node->integer_value);
    case R_HIR_STRING_LITERAL: {
        const RInternEntry *text =
            ((node->aggregate_member != 0U) && (node->aggregate_member <= context->intern_count))
                ? &context->intern_entries[node->aggregate_member - 1U]
                : NULL;
        return (text == NULL) ? r_write_text(writer, user_data, "\"\"")
                              : r_write_escaped(writer,
                                                user_data,
                                                (const uint8_t *)text->bytes,
                                                (size_t)node->integer_value);
    }
    case R_HIR_DEFAULT_VALUE:
        return r_write_text(writer, user_data, "default");
    case R_HIR_CAST:
    case R_HIR_FIELD_INIT:
        return (node->child_count == 1U) &&
               r_mir_write_constant_term(
                   context, r_mir_hir_child(context, node, 0U), writer, user_data, depth + 1U);
    case R_HIR_AGGREGATE_INIT:
    case R_HIR_ARRAY_INIT:
        if (!r_write_text(writer, user_data, "{")) {
            return false;
        }
        for (child_index = 0U; child_index < node->child_count; ++child_index) {
            if (((child_index != 0U) && !r_write_text(writer, user_data, ",")) ||
                !r_mir_write_constant_term(context,
                                           r_mir_hir_child(context, node, child_index),
                                           writer,
                                           user_data,
                                           depth + 1U)) {
                return false;
            }
        }
        return r_write_text(writer, user_data, "}");
    case R_HIR_VARIANT:
        /* L22.1: `tag:payload`, or the tag alone for a variant without payload. */
        return r_mir_write_uint64(writer, user_data, node->integer_value) &&
               ((node->child_count == 0U) ||
                (r_write_text(writer, user_data, ":") &&
                 r_mir_write_constant_term(
                     context, r_mir_hir_child(context, node, 0U), writer, user_data, depth + 1U)));
    default:
        return r_write_text(writer, user_data, "?");
    }
}

/* An object whose value is not one scalar: its value field is the constant term. */
static bool r_mir_object_has_constant_term(const RFrontendContext *context,
                                           const RSemanticSymbol *symbol) {
    const RHirNode *node = r_mir_hir_node(context, symbol->hir_node);
    return !symbol->is_import && (node != NULL) &&
           ((node->kind == R_HIR_AGGREGATE_INIT) || (node->kind == R_HIR_ARRAY_INIT) ||
            (node->kind == R_HIR_STRING_LITERAL) || (node->kind == R_HIR_VARIANT) ||
            ((node->kind == R_HIR_CAST) && (node->child_count == 1U) &&
             (r_mir_hir_node(context, r_mir_hir_child(context, node, 0U)) != NULL) &&
             (r_mir_hir_node(context, r_mir_hir_child(context, node, 0U))->kind ==
              R_HIR_STRING_LITERAL)));
}

RFrontendStatus r_frontend_dump_interface(const RFrontendContext *context,
                                          const RFrontendArtifactOptions *options,
                                          RFrontendWriteFn writer,
                                          void *user_data) {
    size_t function_index;
    size_t aggregate_index;
    size_t constant_count = 0U;
    size_t constant_index;
    size_t object_count = 0U;
    size_t object_index;
    RSymbolId previous_constant = R_SYMBOL_ID_INVALID;
    RSymbolId previous_object = R_SYMBOL_ID_INVALID;

    if ((context == NULL) || (writer == NULL) || !context->mir_lowered ||
        !r_mir_artifact_options_are_valid(options)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_write_text(writer,
                      user_data,
                      "(interface version=31 core_revision=\"" R_FRONTEND_CORE_REVISION "\"\n") ||
        !r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
        !r_write_text(writer, user_data, "(profile ") ||
        !r_write_escaped(
            writer, user_data, (const uint8_t *)options->profile, strlen(options->profile)) ||
        !r_write_text(writer, user_data, ")\n") ||
        !r_mir_write_manifest_fingerprint(writer,
                                          user_data,
                                          "target-manifest",
                                          options->target_manifest,
                                          options->target_manifest_length) ||
        !r_mir_write_abi_record_fingerprint(context, writer, user_data) ||
        !r_mir_write_trait_records(context, writer, user_data) ||
        !r_mir_write_impl_records(context, writer, user_data)) {
        return R_FRONTEND_IO_ERROR;
    }
    for (aggregate_index = 0U; aggregate_index < context->semantic_aggregate_count;
         ++aggregate_index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[aggregate_index];
        uint32_t member_index;
        if (aggregate->is_protected || aggregate->payload_parent != 0U || aggregate->is_tuple ||
            (aggregate->error_family_of != 0U) ||
            (aggregate->generic_origin != 0U &&
             r_generic_type_is_dependent(context, aggregate->type, 0U))) {
            continue;
        }
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(aggregate name=") ||
            !r_mir_write_qualified_aggregate(context, aggregate, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT
                              ? " kind=struct layout=declaration-order copy="
                              : (aggregate->is_tagged ? " kind=enum layout=tagged-union copy="
                                                      : " kind=enum layout=fieldless copy=")) ||
            !r_mir_write_bool(writer, user_data, aggregate->is_copy) ||
            !r_write_text(writer, user_data, " error=") ||
            !r_mir_write_bool(writer, user_data, aggregate->is_error) ||
            /* R-AGG-0011 (L21.1): the parent of an error with one. */
            ((aggregate->error_parent != 0U) &&
             (!r_write_text(writer, user_data, " parent=") ||
              !r_mir_write_type(context,
                                context->semantic_aggregates[aggregate->error_parent - 1U].type,
                                writer,
                                user_data))) ||
            (aggregate->is_must_use && !r_write_text(writer, user_data, " must_use=true")) ||
            /* R-AGG-0012 (L27): the derived capabilities; a derived clone is structural. */
            !r_mir_write_derived(aggregate->derived, writer, user_data) ||
            !r_write_text(writer, user_data, " repr_c=") ||
            !r_mir_write_bool(writer, user_data, aggregate->is_repr_c) ||
            !r_write_text(writer, user_data, " default=") ||
            !r_mir_write_bool(writer, user_data, aggregate->is_default_initializable) ||
            !r_mir_write_aggregate_schema(context, aggregate, writer, user_data)) {
            return R_FRONTEND_IO_ERROR;
        }
        if (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) {
            if (!r_write_text(writer, user_data, " fields=(")) {
                return R_FRONTEND_IO_ERROR;
            }
            for (member_index = 0U; member_index < aggregate->field_count; ++member_index) {
                const RSemanticField *field =
                    &context
                         ->semantic_fields[(size_t)aggregate->first_field + (size_t)member_index];
                const RSource *source = r_get_source_const(context, field->name_span.source);
                if ((source == NULL) ||
                    ((member_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                    !r_write_text(writer, user_data, "(index=") ||
                    !r_write_uint32(writer, user_data, field->layout_index) ||
                    !r_write_text(writer, user_data, " name=") ||
                    !r_write_escaped(writer,
                                     user_data,
                                     source->bytes + field->name_span.start,
                                     (size_t)(field->name_span.end - field->name_span.start)) ||
                    !r_write_text(writer, user_data, " type=") ||
                    !r_mir_write_type(context, field->type, writer, user_data) ||
                    !r_mir_write_json_field(context, field, writer, user_data) ||
                    /* R-INIT-0004 (L33): an initialization may omit the field. */
                    ((field->initializer.node != 0U) &&
                     !r_write_text(writer, user_data, " initializer=true")) ||
                    !r_write_text(writer,
                                  user_data,
                                  field->is_protected ? " visibility=protected)"
                                                      : " visibility=exported)")) {
                    return R_FRONTEND_IO_ERROR;
                }
            }
            if (!r_write_text(writer, user_data, "))\n")) {
                return R_FRONTEND_IO_ERROR;
            }
        } else {
            if (!r_write_text(writer, user_data, " underlying=") ||
                !r_mir_write_type(context, aggregate->enum_underlying_type, writer, user_data) ||
                !r_write_text(writer, user_data, " variants=(")) {
                return R_FRONTEND_IO_ERROR;
            }
            for (member_index = 0U; member_index < aggregate->variant_count; ++member_index) {
                const RSemanticVariant *variant =
                    &context->semantic_variants[(size_t)aggregate->first_variant +
                                                (size_t)member_index];
                const RSource *source = r_get_source_const(context, variant->name_span.source);
                if ((source == NULL) ||
                    ((member_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                    !r_write_text(writer, user_data, "(name=") ||
                    !r_write_escaped(writer,
                                     user_data,
                                     source->bytes + variant->name_span.start,
                                     (size_t)(variant->name_span.end - variant->name_span.start)) ||
                    !r_write_text(writer, user_data, " value=") ||
                    !r_mir_write_uint64(writer, user_data, variant->value) ||
                    !r_mir_write_payload_schema(context, variant, writer, user_data) ||
                    /* R-INIT-0005 (L33): the default variant of the enum. */
                    ((aggregate->default_variant == member_index + 1U) &&
                     !r_write_text(writer, user_data, " default=true")) ||
                    !r_write_text(writer, user_data, ")")) {
                    return R_FRONTEND_IO_ERROR;
                }
            }
            if (!r_write_text(writer, user_data, "))\n")) {
                return R_FRONTEND_IO_ERROR;
            }
        }
    }
    for (constant_index = 0U; constant_index < context->semantic_symbol_count; ++constant_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[constant_index];
        if ((symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) && !symbol->is_protected) {
            constant_count += 1U;
        }
    }
    for (constant_index = 0U; constant_index < constant_count; ++constant_index) {
        RSymbolId next_constant = R_SYMBOL_ID_INVALID;
        size_t symbol_index;
        const RSemanticSymbol *symbol;

        for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
            const RSemanticSymbol *candidate = &context->semantic_symbols[symbol_index];
            const RSymbolId candidate_id = (RSymbolId)(symbol_index + 1U);
            if ((candidate->kind != R_SEMANTIC_SYMBOL_MODULE_CONSTANT) || candidate->is_protected ||
                ((previous_constant != R_SYMBOL_ID_INVALID) &&
                 (r_mir_compare_symbols(context, previous_constant, candidate_id) >= 0)) ||
                ((next_constant != R_SYMBOL_ID_INVALID) &&
                 (r_mir_compare_symbols(context, candidate_id, next_constant) >= 0))) {
                continue;
            }
            next_constant = candidate_id;
        }
        if ((next_constant == R_SYMBOL_ID_INVALID) ||
            ((size_t)next_constant > context->semantic_symbol_count)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        symbol = &context->semantic_symbols[(size_t)next_constant - 1U];
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(constant name=") ||
            !r_mir_write_qualified_symbol(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, symbol->type, writer, user_data) ||
            !r_write_text(writer, user_data, " value=") ||
            !r_mir_write_uint64(writer, user_data, symbol->integer_value) ||
            !r_write_text(writer, user_data, ")\n")) {
            return R_FRONTEND_IO_ERROR;
        }
        previous_constant = next_constant;
    }
    for (object_index = 0U; object_index < context->semantic_symbol_count; ++object_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[object_index];
        if ((symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) && !symbol->is_protected) {
            object_count += 1U;
        }
    }
    for (object_index = 0U; object_index < object_count; ++object_index) {
        RSymbolId next_object = R_SYMBOL_ID_INVALID;
        size_t symbol_index;
        const RSemanticSymbol *symbol;

        for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
            const RSemanticSymbol *candidate = &context->semantic_symbols[symbol_index];
            const RSymbolId candidate_id = (RSymbolId)(symbol_index + 1U);
            if ((candidate->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT) || candidate->is_protected ||
                ((previous_object != R_SYMBOL_ID_INVALID) &&
                 (r_mir_compare_symbols(context, previous_object, candidate_id) >= 0)) ||
                ((next_object != R_SYMBOL_ID_INVALID) &&
                 (r_mir_compare_symbols(context, candidate_id, next_object) >= 0))) {
                continue;
            }
            next_object = candidate_id;
        }
        if ((next_object == R_SYMBOL_ID_INVALID) ||
            ((size_t)next_object > context->semantic_symbol_count)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        symbol = &context->semantic_symbols[(size_t)next_object - 1U];
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(object name=") ||
            !r_mir_write_qualified_symbol(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, symbol->type, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          symbol->is_import
                              ? (symbol->is_thread_local ? " storage=extern-thread value="
                                                         : " storage=extern value=")
                          : symbol->is_thread_local ? " storage=thread value="
                                                    : " storage=static value=") ||
            !(r_mir_object_has_constant_term(context, symbol)
                  ? r_mir_write_constant_term(context, symbol->hir_node, writer, user_data, 0U)
                  : r_mir_write_uint64(writer, user_data, symbol->integer_value)) ||
            !r_write_text(writer, user_data, ")\n")) {
            return R_FRONTEND_IO_ERROR;
        }
        previous_object = next_object;
    }
    if (!r_mir_write_generic_functions(context, writer, user_data) ||
        !r_mir_write_source_dependencies(context, writer, user_data)) {
        return R_FRONTEND_IO_ERROR;
    }
    for (function_index = 0U; function_index < context->mir_function_count; ++function_index) {
        const RMirFunction *function = &context->mir_functions[function_index];
        const RSemanticSymbol *symbol;
        uint32_t parameter_index;
        if ((function->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)function->symbol > context->semantic_symbol_count)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        symbol = &context->semantic_symbols[(size_t)function->symbol - 1U];
        if (symbol->is_protected) {
            continue;
        }
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(function name=") ||
            !r_mir_write_qualified_symbol(context, symbol, writer, user_data) ||
            !r_mir_write_overload(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " return=") ||
            !r_mir_write_type(context,
                              symbol->opaque_result_contract != 0U ? symbol->opaque_result_contract
                                                                   : symbol->return_type,
                              writer,
                              user_data) ||
            !r_write_text(writer, user_data, " parameters=(")) {
            return R_FRONTEND_IO_ERROR;
        }
        for (parameter_index = 0U; parameter_index < symbol->parameter_count; ++parameter_index) {
            size_t index = (size_t)symbol->first_parameter_type + (size_t)parameter_index;
            if ((index >= context->semantic_parameter_type_count) ||
                ((parameter_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_type(
                    context, context->semantic_parameter_types[index], writer, user_data)) {
                return R_FRONTEND_IO_ERROR;
            }
        }
        if (!r_write_text(writer, user_data, ")") ||
            (symbol->is_variadic_slice && !r_write_text(writer, user_data, " variadic=true"))) {
            return R_FRONTEND_IO_ERROR;
        }
        if (symbol->opaque_result_contract != 0U &&
            (!r_write_text(writer, user_data, " opaque_definition=\"") ||
             !r_mir_write_definition_digest(
                 context, symbol->module_source, symbol->declaration_ast, writer, user_data) ||
             !r_write_text(writer, user_data, "\"")))
            return R_FRONTEND_IO_ERROR;
        if ((symbol->return_borrow_parameters_low != UINT64_C(0)) ||
            (symbol->return_borrow_parameters_high != UINT64_C(0))) {
            uint32_t borrow_parameter;
            bool first_borrow_parameter = true;

            if (!r_write_text(writer, user_data, " return_borrow_parameters=(")) {
                return R_FRONTEND_IO_ERROR;
            }
            for (borrow_parameter = UINT32_C(0);
                 (borrow_parameter < symbol->parameter_count) && (borrow_parameter < UINT32_C(128));
                 ++borrow_parameter) {
                const bool present =
                    borrow_parameter < UINT32_C(64)
                        ? (symbol->return_borrow_parameters_low &
                           (UINT64_C(1) << borrow_parameter)) != UINT64_C(0)
                        : (symbol->return_borrow_parameters_high &
                           (UINT64_C(1) << (borrow_parameter - UINT32_C(64)))) != UINT64_C(0);

                if (present &&
                    ((!first_borrow_parameter && !r_write_text(writer, user_data, " ")) ||
                     !r_write_uint32(writer, user_data, borrow_parameter))) {
                    return R_FRONTEND_IO_ERROR;
                }
                if (present) {
                    first_borrow_parameter = false;
                }
            }
            if (!r_write_text(writer, user_data, ")")) {
                return R_FRONTEND_IO_ERROR;
            }
        } else if ((symbol->return_borrow_parameter != UINT32_C(0)) &&
                   (symbol->return_borrow_parameter != UINT32_MAX) &&
                   (!r_write_text(writer, user_data, " return_borrow_parameters=(") ||
                    !r_write_uint32(
                        writer, user_data, symbol->return_borrow_parameter - UINT32_C(1)) ||
                    !r_write_text(writer, user_data, ")"))) {
            return R_FRONTEND_IO_ERROR;
        }
        if (!r_mir_write_borrow_contract(context, function->symbol, writer, user_data) ||
            !r_write_text(writer, user_data, " checked=(") ||
            !r_mir_write_checked_fields(context, symbol, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          options->target_manifest == NULL
                              ? " layout=(target_bound=false sha256=\""
                              : " layout=(target_bound=true sha256=\"") ||
            !r_mir_write_checked_layout_sha256(context, symbol, options, writer, user_data) ||
            !r_write_text(writer, user_data, "\")) async=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_async) ||
            (symbol->is_scoped && !r_write_text(writer, user_data, " scoped=true")) ||
            !r_write_text(writer, user_data, " unsafe=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_unsafe) ||
            !r_write_text(writer, user_data, " noalloc=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_noalloc) ||
            (symbol->is_must_use && !r_write_text(writer, user_data, " must_use=true")) ||
            (symbol->is_discardable && !r_write_text(writer, user_data, " discardable=true")) ||
            (symbol->is_lending && !r_write_text(writer, user_data, " lending=true")) ||
            (symbol->is_chain && !r_write_text(writer, user_data, " chain=true")) ||
            !r_write_text(writer, user_data, " nonblocking=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_nonblocking) ||
            ((symbol->consteval_state == (uint8_t)R_CONSTEVAL_EVALUABLE) &&
             !r_write_text(writer, user_data, " consteval=true")) ||
            !r_write_text(writer, user_data, " extern_c=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_extern_c) ||
            !r_mir_write_c_export_metadata(context, symbol, writer, user_data) ||
            !r_write_text(writer,
                          user_data,
                          symbol->has_definition ? " state=definition)\n"
                                                 : " state=prototype)\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (!r_write_text(writer, user_data, ")\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}

static bool r_mir_profile_is_valid(const char *profile) {
    return (profile != NULL) &&
           ((strcmp(profile, "freestanding") == 0) || (strcmp(profile, "allocation") == 0) ||
            (strcmp(profile, "hosted") == 0) || (strcmp(profile, "hosted-thread") == 0) ||
            (strcmp(profile, "hosted-native-async") == 0));
}

static bool r_mir_artifact_options_are_valid(const RFrontendArtifactOptions *options) {
    return (options != NULL) && r_mir_profile_is_valid(options->profile) &&
           ((uintmax_t)options->target_manifest_length <= (UINT64_MAX / UINT64_C(8))) &&
           ((uintmax_t)options->link_manifest_length <= (UINT64_MAX / UINT64_C(8))) &&
           ((options->target_manifest == NULL) == (options->target_manifest_length == 0U)) &&
           ((options->link_manifest == NULL) == (options->link_manifest_length == 0U));
}

static bool r_mir_entry_matches(const RFrontendContext *context,
                                const RSemanticSymbol *symbol,
                                const char *entry) {
    const RSource *source = r_get_source_const(context, symbol->module_source);
    const char *separator = strstr(entry, "::");
    const uint8_t *name;
    size_t name_length;

    if ((source == NULL) || (source->module_name == NULL) ||
        (symbol->name_span.end < symbol->name_span.start) ||
        ((size_t)symbol->name_span.end > source->length)) {
        return false;
    }
    name = source->bytes + symbol->name_span.start;
    name_length = (size_t)(symbol->name_span.end - symbol->name_span.start);
    if (separator == NULL) {
        return (strcmp(source->module_name, entry) == 0) && (name_length == 4U) &&
               (memcmp(name, "main", 4U) == 0);
    }
    return ((size_t)(separator - entry) == strlen(source->module_name)) &&
           (memcmp(entry, source->module_name, (size_t)(separator - entry)) == 0) &&
           (strlen(separator + 2) == name_length) &&
           (memcmp(separator + 2, name, name_length) == 0);
}

static RFrontendStatus r_mir_validate_artifact_options(const RFrontendContext *context,
                                                       const RFrontendArtifactOptions *options) {
    size_t function_index;
    size_t match_count = 0U;

    if ((context == NULL) || !context->mir_lowered || !r_mir_artifact_options_are_valid(options) ||
        (options->entry == NULL) || (options->entry[0] == '\0') ||
        !r_mir_profile_is_valid(options->profile)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    for (function_index = 0U; function_index < context->mir_function_count; ++function_index) {
        const RMirFunction *function = &context->mir_functions[function_index];
        const RSemanticSymbol *symbol;
        if ((function->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)function->symbol > context->semantic_symbol_count)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        symbol = &context->semantic_symbols[(size_t)function->symbol - 1U];
        if (!symbol->is_protected && symbol->has_definition &&
            r_mir_entry_matches(context, symbol, options->entry)) {
            match_count += 1U;
        }
    }
    return match_count == 1U ? R_FRONTEND_OK : R_FRONTEND_NOT_LOWERABLE;
}

typedef struct RMirSha256 {
    uint32_t state[8];
    uint8_t block[64];
    size_t block_length;
    uint64_t transformed_bits;
} RMirSha256;

static uint32_t r_mir_sha256_rotate_right(uint32_t value, uint32_t amount) {
    return (value >> amount) | (value << (UINT32_C(32) - amount));
}

static uint32_t r_mir_sha256_load_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << UINT32_C(24)) | ((uint32_t)bytes[1] << UINT32_C(16)) |
           ((uint32_t)bytes[2] << UINT32_C(8)) | (uint32_t)bytes[3];
}

static void r_mir_sha256_transform(RMirSha256 *sha) {
    static const uint32_t constants[64] = {
        UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
        UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
        UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
        UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
        UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
        UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
        UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
        UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
        UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
        UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
        UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
        UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
        UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
        UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
        UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
        UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)};
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for (index = 0U; index < UINT32_C(16); ++index) {
        words[index] = r_mir_sha256_load_be32(sha->block + ((size_t)index * 4U));
    }
    for (index = UINT32_C(16); index < UINT32_C(64); ++index) {
        uint32_t previous15 = words[index - UINT32_C(15)];
        uint32_t previous2 = words[index - UINT32_C(2)];
        uint32_t sigma0 = r_mir_sha256_rotate_right(previous15, UINT32_C(7)) ^
                          r_mir_sha256_rotate_right(previous15, UINT32_C(18)) ^
                          (previous15 >> UINT32_C(3));
        uint32_t sigma1 = r_mir_sha256_rotate_right(previous2, UINT32_C(17)) ^
                          r_mir_sha256_rotate_right(previous2, UINT32_C(19)) ^
                          (previous2 >> UINT32_C(10));
        words[index] = words[index - UINT32_C(16)] + sigma0 + words[index - UINT32_C(7)] + sigma1;
    }
    a = sha->state[0];
    b = sha->state[1];
    c = sha->state[2];
    d = sha->state[3];
    e = sha->state[4];
    f = sha->state[5];
    g = sha->state[6];
    h = sha->state[7];
    for (index = 0U; index < UINT32_C(64); ++index) {
        uint32_t sum1 = r_mir_sha256_rotate_right(e, UINT32_C(6)) ^
                        r_mir_sha256_rotate_right(e, UINT32_C(11)) ^
                        r_mir_sha256_rotate_right(e, UINT32_C(25));
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + sum1 + choose + constants[index] + words[index];
        uint32_t sum0 = r_mir_sha256_rotate_right(a, UINT32_C(2)) ^
                        r_mir_sha256_rotate_right(a, UINT32_C(13)) ^
                        r_mir_sha256_rotate_right(a, UINT32_C(22));
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
    sha->state[5] += f;
    sha->state[6] += g;
    sha->state[7] += h;
}

static void r_mir_sha256_init(RMirSha256 *sha) {
    (void)memset(sha, 0, sizeof(*sha));
    sha->state[0] = UINT32_C(0x6a09e667);
    sha->state[1] = UINT32_C(0xbb67ae85);
    sha->state[2] = UINT32_C(0x3c6ef372);
    sha->state[3] = UINT32_C(0xa54ff53a);
    sha->state[4] = UINT32_C(0x510e527f);
    sha->state[5] = UINT32_C(0x9b05688c);
    sha->state[6] = UINT32_C(0x1f83d9ab);
    sha->state[7] = UINT32_C(0x5be0cd19);
}

static void r_mir_sha256_update(RMirSha256 *sha, const uint8_t *bytes, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        sha->block[sha->block_length] = bytes[index];
        sha->block_length += 1U;
        if (sha->block_length == sizeof(sha->block)) {
            r_mir_sha256_transform(sha);
            sha->transformed_bits += UINT64_C(512);
            sha->block_length = 0U;
        }
    }
}

static void r_mir_sha256_final(RMirSha256 *sha, uint8_t digest[32]) {
    uint64_t total_bits = sha->transformed_bits + ((uint64_t)sha->block_length * UINT64_C(8));
    size_t index = sha->block_length;
    uint32_t state_index;

    sha->block[index] = UINT8_C(0x80);
    index += 1U;
    if (index > 56U) {
        while (index < sizeof(sha->block)) {
            sha->block[index] = UINT8_C(0);
            index += 1U;
        }
        r_mir_sha256_transform(sha);
        index = 0U;
    }
    while (index < 56U) {
        sha->block[index] = UINT8_C(0);
        index += 1U;
    }
    for (index = 0U; index < 8U; ++index) {
        sha->block[63U - index] = (uint8_t)(total_bits >> (uint32_t)(index * 8U));
    }
    r_mir_sha256_transform(sha);
    for (state_index = 0U; state_index < UINT32_C(8); ++state_index) {
        uint32_t value = sha->state[state_index];
        digest[(size_t)state_index * 4U] = (uint8_t)(value >> UINT32_C(24));
        digest[((size_t)state_index * 4U) + 1U] = (uint8_t)(value >> UINT32_C(16));
        digest[((size_t)state_index * 4U) + 2U] = (uint8_t)(value >> UINT32_C(8));
        digest[((size_t)state_index * 4U) + 3U] = (uint8_t)value;
    }
}

void r_sha256_digest(const uint8_t *bytes, size_t length, uint8_t digest[32]) {
    RMirSha256 sha;

    r_mir_sha256_init(&sha);
    r_mir_sha256_update(&sha, bytes, length);
    r_mir_sha256_final(&sha, digest);
}

static bool r_mir_write_digest(RFrontendWriteFn writer, void *user_data, const uint8_t digest[32]) {
    static const char hexadecimal[] = "0123456789abcdef";
    char encoded[64];
    size_t index;

    for (index = 0U; index < 32U; ++index) {
        encoded[index * 2U] = hexadecimal[digest[index] >> UINT8_C(4)];
        encoded[(index * 2U) + 1U] = hexadecimal[digest[index] & UINT8_C(0x0f)];
    }
    return writer(user_data, encoded, sizeof(encoded));
}

static bool
r_mir_write_sha256(RFrontendWriteFn writer, void *user_data, const uint8_t *bytes, size_t length) {
    uint8_t digest[32];

    r_sha256_digest(bytes, length, digest);
    return r_mir_write_digest(writer, user_data, digest);
}

/*
 * R-FFI-0044: the loaded ABI record document and every header digest it carries enter the
 * interface and build fingerprints, so that a regenerated record changes both.
 */
static bool r_mir_write_abi_record_fingerprint(const RFrontendContext *context,
                                               RFrontendWriteFn writer,
                                               void *user_data) {
    size_t record_index;

    if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
        !r_write_text(writer, user_data, "(abi-record present=")) {
        return false;
    }
    if (!context->abi_record_digest_present) {
        return r_write_text(writer, user_data, "false)\n");
    }
    if (!r_write_text(writer, user_data, "true sha256=\"") ||
        !r_mir_write_digest(writer, user_data, context->abi_record_digest) ||
        !r_write_text(writer, user_data, "\")\n")) {
        return false;
    }
    for (record_index = 0U; record_index < context->abi_record_count; ++record_index) {
        const RAbiRecordEntry *record = &context->abi_records[record_index];
        size_t header_index;

        for (header_index = 0U; header_index < record->header_count; ++header_index) {
            const RAbiRecordHeader *header =
                &context->abi_headers[record->first_header + header_index];

            if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
                !r_write_text(writer, user_data, "(abi-header record=") ||
                !r_write_escaped(
                    writer, user_data, (const uint8_t *)record->name, record->name_length) ||
                !r_write_text(writer, user_data, " spelling=") ||
                !r_write_escaped(writer,
                                 user_data,
                                 (const uint8_t *)header->spelling,
                                 header->spelling_length) ||
                !r_write_text(writer, user_data, " sha256=") ||
                !r_write_escaped(
                    writer, user_data, (const uint8_t *)header->sha256, header->sha256_length) ||
                !r_write_text(writer, user_data, ")\n")) {
                return false;
            }
        }
    }
    return true;
}

typedef struct RMirCheckedHashWriter {
    RMirSha256 *sha;
} RMirCheckedHashWriter;

static bool r_mir_checked_hash_write(void *user_data, const char *bytes, size_t length) {
    RMirCheckedHashWriter *hash_writer = user_data;

    if ((hash_writer == NULL) || (hash_writer->sha == NULL) ||
        ((bytes == NULL) && (length != 0U))) {
        return false;
    }
    r_mir_sha256_update(hash_writer->sha, (const uint8_t *)bytes, length);
    return true;
}

typedef struct RMirJsonTypeVisit {
    RTypeId type;
    const struct RMirJsonTypeVisit *parent;
    uint32_t depth;
} RMirJsonTypeVisit;

/* Include child contracts, even through containers; nominal back references close cycles. */
static bool r_mir_write_json_type_contract(const RFrontendContext *context,
                                           RTypeId id,
                                           RFrontendWriteFn writer,
                                           void *user_data,
                                           const RMirJsonTypeVisit *parent) {
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || !r_mir_write_type(context, id, writer, user_data))
        return false;
    for (const RMirJsonTypeVisit *previous = parent; previous != NULL; previous = previous->parent)
        if (previous->type == id)
            return r_write_text(writer, user_data, " recursive");
    RMirJsonTypeVisit visit = {id, parent, parent == NULL ? 0U : parent->depth + 1U};
    if (visit.depth > context->options.limits.max_nesting)
        return false;
    const RSemanticAggregate *aggregate =
        ((type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) &&
         type->base != 0U && type->base <= context->semantic_aggregate_count)
            ? &context->semantic_aggregates[type->base - 1U]
            : NULL;
    if (aggregate == NULL) {
        switch (type->kind) {
        case R_SEMANTIC_TYPE_CONST:
        case R_SEMANTIC_TYPE_FIXED_ARRAY:
        case R_SEMANTIC_TYPE_ARRAY:
        case R_SEMANTIC_TYPE_LIST:
        case R_SEMANTIC_TYPE_SLICE:
        case R_SEMANTIC_TYPE_OPTION:
            return r_mir_write_json_type_contract(context, type->base, writer, user_data, &visit);
        case R_SEMANTIC_TYPE_DICT:
            return r_mir_write_json_type_contract(context, type->base, writer, user_data, &visit) &&
                   r_mir_write_json_type_contract(context, type->second, writer, user_data, &visit);
        default:
            return true;
        }
    }
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field = &context->semantic_fields[aggregate->first_field + i];
        const RInternEntry *name = &context->intern_entries[field->name_intern_id - 1U];
        if (!r_write_text(writer, user_data, "(field ") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            !r_mir_write_type(context, field->type, writer, user_data) ||
            !r_mir_write_json_field(context, field, writer, user_data) ||
            !r_mir_write_bool(writer, user_data, field->is_protected) ||
            !r_mir_write_json_type_contract(context, field->type, writer, user_data, &visit) ||
            !r_write_text(writer, user_data, ")"))
            return false;
    }
    if (!r_mir_write_hook(
            context, "json_marshal", aggregate->json_marshal_function, writer, user_data) ||
        !r_mir_write_hook(
            context, "json_unmarshal", aggregate->json_unmarshal_function, writer, user_data) ||
        !r_mir_write_hook(
            context, "json_is_zero", aggregate->json_is_zero_function, writer, user_data) ||
        !r_mir_write_hook(
            context, "json_schema", aggregate->json_schema_function, writer, user_data))
        return false;
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant = &context->semantic_variants[aggregate->first_variant + i];
        const RInternEntry *name = &context->intern_entries[variant->name_intern_id - 1U];
        if (!r_write_text(writer, user_data, "(variant ") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            !r_mir_write_uint64(writer, user_data, variant->value) ||
            (variant->payload_type != R_TYPE_ID_INVALID &&
             !r_mir_write_json_type_contract(
                 context, variant->payload_type, writer, user_data, &visit)) ||
            !r_write_text(writer, user_data, ")"))
            return false;
    }
    return true;
}

static bool r_mir_write_json_schema_sha256(const RFrontendContext *context,
                                           const RSemanticAggregate *aggregate,
                                           RFrontendWriteFn writer,
                                           void *user_data) {
    static const char domain[] = "R JSON field schema v2";
    static const char hexadecimal[] = "0123456789abcdef";
    RMirSha256 sha;
    RMirCheckedHashWriter sink = {&sha};
    uint8_t digest[32];
    char encoded[64];
    r_mir_sha256_init(&sha);
    r_mir_sha256_update(&sha, (const uint8_t *)domain, sizeof(domain) - 1U);
    if (!r_mir_write_type(context, aggregate->type, r_mir_checked_hash_write, &sink))
        return false;
    if (!r_mir_write_json_type_contract(
            context, aggregate->type, r_mir_checked_hash_write, &sink, NULL))
        return false;
    r_mir_sha256_final(&sha, digest);
    for (size_t i = 0U; i < sizeof(digest); ++i) {
        encoded[i * 2U] = hexadecimal[digest[i] >> 4U];
        encoded[i * 2U + 1U] = hexadecimal[digest[i] & 15U];
    }
    return writer(user_data, encoded, sizeof(encoded));
}

static bool r_mir_write_checked_layout_sha256(const RFrontendContext *context,
                                              const RSemanticSymbol *symbol,
                                              const RFrontendArtifactOptions *options,
                                              RFrontendWriteFn writer,
                                              void *user_data) {
    static const char domain[] = "R checked interface target layout v1";
    static const char hexadecimal[] = "0123456789abcdef";
    RMirSha256 sha;
    RMirCheckedHashWriter hash_writer;
    uint8_t manifest_length[8];
    uint8_t digest[32];
    char encoded[64];
    uint64_t length;
    size_t index;

    if ((context == NULL) || (symbol == NULL) || (options == NULL) || (writer == NULL)) {
        return false;
    }
    r_mir_sha256_init(&sha);
    hash_writer.sha = &sha;
    r_mir_sha256_update(&sha, (const uint8_t *)domain, sizeof(domain) - 1U);
    if (!r_mir_write_checked_fields(context, symbol, r_mir_checked_hash_write, &hash_writer)) {
        return false;
    }
    length = (uint64_t)options->target_manifest_length;
    for (index = 0U; index < sizeof(manifest_length); ++index) {
        manifest_length[sizeof(manifest_length) - index - 1U] =
            (uint8_t)(length >> (uint32_t)(index * 8U));
    }
    r_mir_sha256_update(&sha, manifest_length, sizeof(manifest_length));
    if (options->target_manifest != NULL) {
        r_mir_sha256_update(&sha, options->target_manifest, options->target_manifest_length);
    }
    r_mir_sha256_final(&sha, digest);
    for (index = 0U; index < sizeof(digest); ++index) {
        encoded[index * 2U] = hexadecimal[digest[index] >> UINT8_C(4)];
        encoded[(index * 2U) + 1U] = hexadecimal[digest[index] & UINT8_C(0x0f)];
    }
    return writer(user_data, encoded, sizeof(encoded));
}

static bool r_mir_write_manifest_fingerprint(RFrontendWriteFn writer,
                                             void *user_data,
                                             const char *name,
                                             const uint8_t *bytes,
                                             size_t length) {
    if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
        !r_write_text(writer, user_data, "(") || !r_write_text(writer, user_data, name)) {
        return false;
    }
    if (bytes == NULL) {
        return r_write_text(writer, user_data, " present=false)\n");
    }
    return r_write_text(writer, user_data, " present=true length=") &&
           r_mir_write_uint64(writer, user_data, (uint64_t)length) &&
           r_write_text(writer, user_data, " sha256=\"") &&
           r_mir_write_sha256(writer, user_data, bytes, length) &&
           r_write_text(writer, user_data, "\"") && r_write_text(writer, user_data, ")\n");
}

static bool
r_mir_dump_units(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data) {
    const char *previous = NULL;
    size_t emitted;

    for (emitted = 0U; emitted < context->source_count; ++emitted) {
        const char *selected = NULL;
        size_t source_index;
        for (source_index = 0U; source_index < context->source_count; ++source_index) {
            const char *candidate = context->sources[source_index].module_name;
            if ((candidate == NULL) || ((previous != NULL) && (strcmp(candidate, previous) <= 0))) {
                continue;
            }
            if ((selected == NULL) || (strcmp(candidate, selected) < 0)) {
                selected = candidate;
            }
        }
        if (selected == NULL) {
            return false;
        }
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(unit ") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)selected, strlen(selected)) ||
            !r_write_text(writer, user_data, ")\n")) {
            return false;
        }
        previous = selected;
    }
    return true;
}

typedef struct RMirLinkLibraries {
    bool std_alloc;
    bool std_arc;
    bool std_rc;
    bool std_array;
    bool std_list;
    bool std_dict;
    bool std_bits;
    bool std_bytes;
    bool std_hash;
    bool std_math;
    bool std_format;
    bool std_json;
    bool std_net;
    bool std_string;
    bool std_time;
    bool std_utf8;
    bool std_convert;
    bool std_env;
    bool std_async;
    bool std_thread;
    bool std_sync;
    bool std_io;
    bool std_fs;
    bool std_c;
    bool std_secret;
    bool std_random;
    bool std_test;
    bool std_error;
    bool std_process;
    bool std_signal;
} RMirLinkLibraries;

/* R-TYPE-0046 (L32): whether the standard formatting of `type_id` writes a std.net address. */
static bool r_mir_type_formats_net_address(const RFrontendContext *context,
                                           RTypeId type_id,
                                           uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    const RSemanticAggregate *aggregate;

    if ((type == NULL) || (depth > UINT32_C(64))) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_ARRAY:
        return r_mir_type_formats_net_address(context, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_STANDARD:
        return r_mir_standard_type_name_equal(context, type, "std.net::ip_address") ||
               r_mir_standard_type_name_equal(context, type, "std.net::socket_address");
    default:
        break;
    }
    if ((type->kind != R_SEMANTIC_TYPE_STRUCT) || (type->base == R_TYPE_ID_INVALID) ||
        ((size_t)type->base > context->semantic_aggregate_count)) {
        return false;
    }
    aggregate = &context->semantic_aggregates[type->base - 1U];
    if (!aggregate->is_tuple) {
        return false;
    }
    for (uint32_t index = 0U; index < aggregate->field_count; ++index) {
        if (r_mir_type_formats_net_address(
                context, context->semantic_fields[aggregate->first_field + index].type, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static RMirLinkLibraries r_mir_link_libraries(const RFrontendContext *context) {
    RMirLinkLibraries libraries = {0};
    size_t instruction_index;

    for (instruction_index = 0U; instruction_index < context->semantic_symbol_count;
         ++instruction_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[instruction_index];
        if (context->profile >= R_FRONTEND_PROFILE_HOSTED &&
            symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION && !symbol->is_protected &&
            !symbol->is_unsafe && !symbol->is_extern_c &&
            symbol->throws_type != R_TYPE_ID_INVALID &&
            r_source_text_equal(
                r_get_source_const(context, symbol->module_source), symbol->name_span, "main"))
            libraries.std_error = true;
        if (context->semantic_symbols[instruction_index].is_extern_c &&
            context->semantic_symbols[instruction_index].has_definition) {
            libraries.std_c = true;
        }
    }
    for (instruction_index = 0U; instruction_index < context->mir_instruction_count;
         ++instruction_index) {
        const RMirInstruction *instruction = &context->mir_instructions[instruction_index];

        const RSemanticType *address_type = r_semantic_type(
            context, r_semantic_representation_type(context, instruction->type));
        /* R-TYPE-0054 (L28): a function value is the number of an R function, not a C address. */
        if ((instruction->kind == R_MIR_INSTRUCTION_FUNCTION_ADDRESS &&
             (address_type == NULL || address_type->kind != R_SEMANTIC_TYPE_FUNCTION)) ||
            instruction->kind == R_MIR_INSTRUCTION_INDIRECT_CALL) {
            libraries.std_c = true;
        }
        if (instruction->kind == R_MIR_INSTRUCTION_CALL && instruction->symbol != 0U &&
            context->semantic_symbols[instruction->symbol - 1U].json_operation != 0U) {
            uint32_t operation = context->semantic_symbols[instruction->symbol - 1U].json_operation;
            libraries.std_json = true;
            if (operation == R_JSON_OPERATION_READ_NEXT)
                libraries.std_async = true;
            if (operation == R_JSON_OPERATION_NEW_READER) {
                const RSemanticType *transport = r_semantic_type(
                    context, context->semantic_symbols[instruction->symbol - 1U].json_type);
                const RInternEntry *name = &context->intern_entries[(size_t)transport->length - 1U];
                libraries.std_net = libraries.std_net ||
                                    (name->length == sizeof("std.net::tcp_stream") - 1U &&
                                     memcmp(name->bytes, "std.net::tcp_stream", name->length) == 0);
                libraries.std_fs =
                    libraries.std_fs || (name->length == sizeof("std.fs::file") - 1U &&
                                         memcmp(name->bytes, "std.fs::file", name->length) == 0);
                libraries.std_io =
                    libraries.std_io || (name->length == sizeof("std.io::input") - 1U &&
                                         memcmp(name->bytes, "std.io::input", name->length) == 0);
            }
            if (operation == R_JSON_OPERATION_MARSHAL ||
                operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS)
                libraries.std_format = true;
        }
        if (instruction->kind == R_MIR_INSTRUCTION_CALL && instruction->symbol != 0U &&
            context->semantic_symbols[instruction->symbol - 1U].format_recipe != 0U) {
            uint32_t recipe = context->semantic_symbols[instruction->symbol - 1U].format_recipe;
            if (context->format_recipes[recipe - 1U].kind <= R_FORMAT_IMMEDIATE) {
                libraries.std_format = true;
            } else {
                libraries.std_string = true;
            }
        }
        if (instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) {
            continue;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER) ||
            (instruction->standard_operation == R_STANDARD_CALL_CORE_FORMAT_APPEND)) {
            /* R-TYPE-0046 (L32): standard formatting builds on std.format and std.string and
               writes std.net addresses through std.net. */
            libraries.std_format = true;
            libraries.std_string = true;
            libraries.std_convert = true;
            libraries.std_net =
                libraries.std_net || r_mir_type_formats_net_address(context, instruction->runtime_type, 0U);
            continue;
        }
        if ((instruction->standard_operation == R_STANDARD_CALL_ALLOC_TRY_NEW) ||
            (instruction->standard_operation == R_STANDARD_CALL_ALLOC_INTO_VALUE) ||
            (instruction->standard_operation == R_STANDARD_CALL_ALLOC_BYTES)) {
            libraries.std_alloc = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_ARC_CLONE) ||
                   ((instruction->standard_operation >= R_STANDARD_CALL_ARC_CLONE_WEAK) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_ARC_TRY_UNWRAP))) {
            libraries.std_arc = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_RC_CLONE) ||
                   ((instruction->standard_operation >= R_STANDARD_CALL_RC_CLONE_WEAK) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_RC_TRY_UNWRAP))) {
            libraries.std_rc = true;
        } else if (((instruction->standard_operation >= R_STANDARD_CALL_ARRAY_CAPACITY) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_ARRAY_WITH_CAPACITY)) ||
                   (instruction->standard_operation == R_STANDARD_CALL_ARRAY_CREATE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_ARRAY_FILLED)) {
            libraries.std_array = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_LIST_CREATE) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_LIST_NEXT)) {
            libraries.std_list = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_DICT_CREATE) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_DICT_NEXT)) {
            libraries.std_dict = true;
        } else if (((instruction->standard_operation >= R_STANDARD_CALL_BYTES_WITH_CAPACITY) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_BYTES_ENDS_WITH)) ||
                   (instruction->standard_operation == R_STANDARD_CALL_BYTES_EQUAL) ||
                   (instruction->standard_operation == R_STANDARD_CALL_BYTES_COMPARE)) {
            libraries.std_bytes = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_HASH_CRC32) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_HASH_SHA512)) {
            libraries.std_hash = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_UTF8_IS_VALID) ||
                   (instruction->standard_operation == R_STANDARD_CALL_UTF8_VALIDATE)) {
            libraries.std_utf8 = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_BITS_READ) ||
                   (instruction->standard_operation == R_STANDARD_CALL_BITS_ALIGN_BYTE)) {
            libraries.std_bits = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_CONVERT_PARSE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED)) {
            libraries.std_convert = true;
        } else if (r_async_sync_operation(instruction->standard_operation)) {
            /* M24-1: a table operation names its library by module, in synchronous programs
               too. */
            const RAsyncSyncDescriptor *descriptor =
                r_async_sync_descriptor(instruction->standard_operation);
            if ((descriptor != NULL) && (strcmp(descriptor->module, "test") == 0)) {
                libraries.std_test = true;
            } else if ((descriptor != NULL) && (strcmp(descriptor->module, "sync") == 0)) {
                libraries.std_sync = true;
                libraries.std_async = true;
            } else {
                libraries.std_async = true;
            }
        } else if ((instruction->standard_operation == R_STANDARD_CALL_ASYNC_CANCEL) ||
                   (instruction->standard_operation == R_STANDARD_CALL_ASYNC_DETACH) ||
                   (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING)) {
            libraries.std_async = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
                   (instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
                   (instruction->standard_operation == R_STANDARD_CALL_THREAD_JOIN) ||
                   (instruction->standard_operation == R_STANDARD_CALL_THREAD_DETACH)) {
            libraries.std_thread = true;
        } else if (((instruction->standard_operation >= R_STANDARD_CALL_SYNC_ONCE_NEW) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_SYNC_RECEIVER)) ||
                   ((instruction->standard_operation >= R_STANDARD_CALL_SYNC_MUTEX_NEW) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_SYNC_WAIT)) ||
                   ((instruction->standard_operation >= R_STANDARD_CALL_SYNC_BARRIER_NEW) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_SYNC_NOTIFY_ALL))) {
            libraries.std_sync = true;
        } else if (((instruction->standard_operation >= R_STANDARD_CALL_FORMAT_CREATE) &&
                    (instruction->standard_operation <=
                     R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE)) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FORMAT_APPEND_INTEGER)) {
            libraries.std_format = true;
        } else if (instruction->standard_operation == R_STANDARD_CALL_C_CHECKED) {
            libraries.std_convert = true;
            libraries.std_c = true;
        } else if (instruction->standard_operation == R_STANDARD_CALL_C_LINK_AVAILABLE) {
            libraries.std_c = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_SECRET_WITH_LENGTH) &&
                   (instruction->standard_operation <=
                    R_STANDARD_CALL_SECRET_CONSTANT_TIME_EQUAL)) {
            libraries.std_secret = true;
        } else if (instruction->standard_operation == R_STANDARD_CALL_RANDOM_FILL) {
            libraries.std_random = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_C_TARGET) ||
                   ((instruction->standard_operation >= R_STANDARD_CALL_C_STRING_FROM_STR) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_C_COPY_UTF8)) ||
                   (instruction->standard_operation == R_STANDARD_CALL_C_ATTACH_THREAD) ||
                   (instruction->standard_operation == R_STANDARD_CALL_C_DETACH_THREAD) ||
                   (instruction->standard_operation == R_STANDARD_CALL_C_HANDLE_POINTER) ||
                   (instruction->standard_operation == R_STANDARD_CALL_C_ADOPT_HANDLE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_C_RELEASE_HANDLE)) {
            libraries.std_c = true;
        } else if (instruction->standard_operation == R_STANDARD_CALL_ERROR_ERASURE) {
            const RStandardErrorErasureDescriptor *descriptor =
                r_standard_error_erasure(instruction->integer_value);

            if (descriptor != NULL) {
                libraries.std_c =
                    libraries.std_c || strcmp(descriptor->library_target, "r_std_c") == 0;
                libraries.std_env =
                    libraries.std_env || strcmp(descriptor->library_target, "r_std_env") == 0;
                libraries.std_net =
                    libraries.std_net || strcmp(descriptor->library_target, "r_std_net") == 0;
                libraries.std_process = libraries.std_process ||
                                        strcmp(descriptor->library_target, "r_std_process") == 0;
                libraries.std_error =
                    libraries.std_error || strcmp(descriptor->library_target, "r_std_error") == 0;
            }
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_ENV_ARGUMENTS) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_ENV_REMOVE)) {
            libraries.std_env = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_NET_PARSE_IP) ||
                   (instruction->standard_operation == R_STANDARD_CALL_NET_FORMAT_IP) ||
                   (instruction->standard_operation == R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS) ||
                   (r_standard_net_operation(instruction->standard_operation) != NULL)) {
            libraries.std_net = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_PROCESS_COMMAND_CREATE) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_PROCESS_ABORT)) {
            libraries.std_process = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_PROCESS_SPAWN) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_PROCESS_TERMINATE)) {
            libraries.std_process = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_SIGNAL_LISTEN) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_SIGNAL_NEXT)) {
            /* R-SLIB-SIGNAL-0001: std.signal reports std.process::process_error. */
            libraries.std_signal = true;
            libraries.std_process = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_ERROR_NAME) ||
                   (instruction->standard_operation == R_STANDARD_CALL_ERROR_DIAGNOSTIC)) {
            libraries.std_error = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_THREAD_CURRENT) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_THREAD_PANIC_TEXT)) {
            libraries.std_thread = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_MATH_OPERATION) ||
                   (instruction->standard_operation == R_STANDARD_CALL_MATH_ABS_F64) ||
                   (instruction->standard_operation == R_STANDARD_CALL_MATH_SIN_F64)) {
            libraries.std_math = true;
        } else if ((instruction->standard_operation >= R_STANDARD_CALL_STRING_CREATE) &&
                   (instruction->standard_operation <= R_STANDARD_CALL_STRING_TRUNCATE)) {
            libraries.std_string = true;
        } else if (((instruction->standard_operation >=
                     R_STANDARD_CALL_TIME_DURATION_FROM_SECONDS) &&
                    (instruction->standard_operation <= R_STANDARD_CALL_TIME_AS_ERROR)) ||
                   (instruction->standard_operation == R_STANDARD_CALL_TIME_SLEEP_FOR) ||
                   (instruction->standard_operation == R_STANDARD_CALL_TIME_SLEEP_UNTIL)) {
            libraries.std_time = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8_BYTES) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_PATH_CLONE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_PATH_TO_UTF8) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_PATH_JOIN) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_READ_FILE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_OPEN_DIRECTORY) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_OPEN_FILE) ||
                   (instruction->standard_operation ==
                    R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) ||
                   (instruction->standard_operation ==
                    R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH) ||
                   (instruction->standard_operation == R_STANDARD_CALL_FS_AS_ERROR)) {
            libraries.std_fs = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_FS_FILE_METADATA) ||
                   (r_standard_fs_async_descriptor(instruction->standard_operation) != NULL)) {
            libraries.std_fs = true;
        } else if ((instruction->standard_operation == R_STANDARD_CALL_IO_STDIN) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_STDOUT) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_STDERR) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_READ) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_FLUSH) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_CLOSE_INPUT) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_CLOSE_OUTPUT) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE_ALL) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE_SHARED) ||
                   (instruction->standard_operation == R_STANDARD_CALL_IO_AS_ERROR)) {
            libraries.std_io = true;
        }
    }
    return libraries;
}

static bool r_mir_dump_link_libraries(const RFrontendContext *context,
                                      RFrontendWriteFn writer,
                                      void *user_data) {
    const RMirLinkLibraries libraries = r_mir_link_libraries(context);

    if (libraries.std_alloc &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.alloc\" target=\"r_std_alloc\")\n"))) {
        return false;
    }
    if (libraries.std_arc &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.arc\" target=\"r_std_arc\")\n"))) {
        return false;
    }
    if (libraries.std_rc &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.rc\" target=\"r_std_rc\")\n"))) {
        return false;
    }
    if (libraries.std_array &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.array\" target=\"r_std_array\")\n"))) {
        return false;
    }
    if (libraries.std_list &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.list\" target=\"r_std_list\")\n"))) {
        return false;
    }
    if (libraries.std_dict &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.dict\" target=\"r_std_dict\")\n"))) {
        return false;
    }
    if (libraries.std_bytes &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.bytes\" target=\"r_std_bytes\")\n"))) {
        return false;
    }
    if (libraries.std_string &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.string\" target=\"r_std_string\")\n"))) {
        return false;
    }
    if (libraries.std_format &&
        (!r_write_text(writer, user_data, "  ") ||
         !r_write_text(
             writer, user_data, "(library module=\"std.format\" target=\"r_std_format\")\n"))) {
        return false;
    }
    if (libraries.std_net &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.net\" target=\"r_std_net\")\n")))
        return false;
    if (libraries.std_json &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.json\" target=\"r_std_json\")\n")))
        return false;
    if (libraries.std_hash &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.hash\" target=\"r_std_hash\")\n"))) {
        return false;
    }
    if (libraries.std_math &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.math\" target=\"r_std_math\")\n"))) {
        return false;
    }
    if (libraries.std_utf8 &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.utf8\" target=\"r_std_utf8\")\n"))) {
        return false;
    }
    if (libraries.std_bits &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.bits\" target=\"r_std_bits\")\n"))) {
        return false;
    }
    if (libraries.std_convert &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.convert\" target=\"r_std_convert\")\n"))) {
        return false;
    }
    if (libraries.std_env &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.env\" target=\"r_std_env\")\n"))) {
        return false;
    }
    if (libraries.std_async &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.async\" target=\"r_std_async\")\n"))) {
        return false;
    }
    if (libraries.std_thread &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.thread\" target=\"r_std_thread\")\n"))) {
        return false;
    }
    if (libraries.std_process &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.process\" target=\"r_std_process\")\n"))) {
        return false;
    }
    if (libraries.std_signal &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.signal\" target=\"r_std_signal\")\n"))) {
        return false;
    }
    if (libraries.std_time &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.time\" target=\"r_std_time\")\n"))) {
        return false;
    }
    if (libraries.std_sync &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.sync\" target=\"r_std_sync\")\n"))) {
        return false;
    }
    if (libraries.std_io &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.io\" target=\"r_std_io\")\n"))) {
        return false;
    }
    if (libraries.std_fs &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.fs\" target=\"r_std_fs\")\n"))) {
        return false;
    }
    if (libraries.std_c &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(writer, user_data, "(library module=\"std.c\" target=\"r_std_c\")\n"))) {
        return false;
    }
    if (libraries.std_secret &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.secret\" target=\"r_std_secret\")\n"))) {
        return false;
    }
    if (libraries.std_random &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.random\" target=\"r_std_random\")\n"))) {
        return false;
    }
    if (libraries.std_test &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.test\" target=\"r_std_test\")\n"))) {
        return false;
    }
    if (libraries.std_error &&
        (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
         !r_write_text(
             writer, user_data, "(library module=\"std.error\" target=\"r_std_error\")\n"))) {
        return false;
    }
    return true;
}

static int r_mir_compare_intern_entries(const RInternEntry *left, const RInternEntry *right) {
    const size_t common = left->length < right->length ? left->length : right->length;
    const int comparison = memcmp(left->bytes, right->bytes, common);

    if (comparison != 0) {
        return comparison;
    }
    if (left->length < right->length) {
        return -1;
    }
    return left->length > right->length ? 1 : 0;
}

/* Logical @link providers of every extern "C" import, in ascending name order. */
static bool
r_mir_dump_link_imports(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data) {
    const RInternEntry *previous = NULL;
    bool implicit = false;
    bool more = true;

    while (more) {
        const RInternEntry *best = NULL;
        RLinkKind best_kind = R_LINK_KIND_UNSPECIFIED;
        size_t index;

        for (index = 0U; index < context->semantic_symbol_count; ++index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[index];
            const RInternEntry *entry;

            if (!symbol->is_import || ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) &&
                                       (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT))) {
                continue;
            }
            if (symbol->link_name_intern_id == UINT32_C(0)) {
                implicit = true;
                continue;
            }
            if ((size_t)symbol->link_name_intern_id > context->intern_count) {
                return false;
            }
            entry = &context->intern_entries[(size_t)symbol->link_name_intern_id - 1U];
            if ((previous != NULL) && (r_mir_compare_intern_entries(entry, previous) <= 0)) {
                continue;
            }
            if ((best == NULL) || (r_mir_compare_intern_entries(entry, best) < 0)) {
                best = entry;
                best_kind = symbol->link_kind;
            }
        }
        if (best == NULL) {
            more = false;
        } else {
            if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
                !r_write_text(writer, user_data, "(link name=") ||
                !r_write_escaped(writer, user_data, (const uint8_t *)best->bytes, best->length) ||
                !r_write_text(writer, user_data, " kind=\"") ||
                !r_write_text(writer, user_data, r_link_manifest_kind_name(best_kind)) ||
                !r_write_text(writer, user_data, "\")\n")) {
                return false;
            }
            previous = best;
        }
    }
    if (implicit && (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
                     !r_write_text(writer, user_data, "(link implicit-c-runtime)\n"))) {
        return false;
    }
    return true;
}

RFrontendStatus r_frontend_dump_link_plan(const RFrontendContext *context,
                                          const RFrontendArtifactOptions *options,
                                          RFrontendWriteFn writer,
                                          void *user_data) {
    RFrontendStatus status;

    if (writer == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    status = r_mir_validate_artifact_options(context, options);
    if (status != R_FRONTEND_OK) {
        return status;
    }
    if (!r_write_text(writer, user_data, "(link-plan version=1\n") ||
        !r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
        !r_write_text(writer, user_data, "(entry ") ||
        !r_write_escaped(
            writer, user_data, (const uint8_t *)options->entry, strlen(options->entry)) ||
        !r_write_text(writer, user_data, ")\n") ||
        !r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
        !r_write_text(writer, user_data, "(profile ") ||
        !r_write_escaped(
            writer, user_data, (const uint8_t *)options->profile, strlen(options->profile)) ||
        !r_write_text(writer, user_data, ")\n") ||
        !r_mir_write_manifest_fingerprint(writer,
                                          user_data,
                                          "target-manifest",
                                          options->target_manifest,
                                          options->target_manifest_length) ||
        !r_mir_write_manifest_fingerprint(writer,
                                          user_data,
                                          "link-manifest",
                                          options->link_manifest,
                                          options->link_manifest_length) ||
        !r_mir_write_abi_record_fingerprint(context, writer, user_data) ||
        !r_mir_dump_units(context, writer, user_data) ||
        !r_mir_dump_link_libraries(context, writer, user_data) ||
        !r_mir_dump_link_imports(context, writer, user_data) ||
        !r_write_text(writer, user_data, ")\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}

RFrontendStatus r_frontend_dump_bundle(const RFrontendContext *context,
                                       const RFrontendArtifactOptions *options,
                                       RFrontendWriteFn writer,
                                       void *user_data) {
    RFrontendStatus status;

    if (writer == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    status = r_mir_validate_artifact_options(context, options);
    if (status != R_FRONTEND_OK) {
        return status;
    }
    if (!r_write_text(writer,
                      user_data,
                      "(bundle version=1 core_revision=\"" R_FRONTEND_CORE_REVISION
                      "\"\n  (section interface\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    status = r_frontend_dump_interface(context, options, writer, user_data);
    if (status != R_FRONTEND_OK) {
        return status;
    }
    if (!r_write_text(writer, user_data, "  )\n  (section mir\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    status = r_frontend_dump_mir(context, writer, user_data);
    if (status != R_FRONTEND_OK) {
        return status;
    }
    if (!r_write_text(writer, user_data, "  )\n  (section link-plan\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    status = r_frontend_dump_link_plan(context, options, writer, user_data);
    if (status != R_FRONTEND_OK) {
        return status;
    }
    if (!r_write_text(writer, user_data, "  )\n)\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}
