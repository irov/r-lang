/* A local name shall not denote a sequence of unconditional replacement values.
   Guards are relative to its declaration: a nested block is not a guard, and a
   local declared inside a loop does not acquire permission from that loop. */
typedef struct RLocalReassignmentCheck {
    RBodyContext body;
    uint32_t *declaration_guards;
    uint8_t *visited;
} RLocalReassignmentCheck;

static bool r_local_reassignment_walk(RLocalReassignmentCheck *check,
                                      RHirNodeId id,
                                      uint32_t guards,
                                      uint32_t depth) {
    RFrontendContext *context = check->body.frontend;
    if (id == 0U || id > context->hir_node_count || depth > r_frontend_tree_depth_limit(context)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (check->visited[id - 1U])
        return true;
    check->visited[id - 1U] = 1U;
    const RHirNode node = context->hir_nodes[id - 1U];
    if (node.kind == R_HIR_LOCAL && node.symbol != 0U)
        check->declaration_guards[node.symbol - 1U] = guards;
    if (node.kind == R_HIR_ASSIGN && node.replaces_initialized && !node.out_commit &&
        node.out_place == 0U && node.child_count == 2U) {
        const RHirNode *place = &context->hir_nodes[context->hir_children[node.first_child] - 1U];
        if (place->kind == R_HIR_PLACE && place->symbol != 0U) {
            const RSemanticSymbol *binding = &context->semantic_symbols[place->symbol - 1U];
            if ((binding->kind == R_SEMANTIC_SYMBOL_LOCAL ||
                 binding->kind == R_SEMANTIC_SYMBOL_PARAMETER) &&
                !binding->is_hidden_local &&
                guards <= check->declaration_guards[place->symbol - 1U] &&
                !r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-USE-002",
                    "R-INIT-0014",
                    "unconditional replacement of a local value is forbidden; declare a new "
                    "variable even when the previous value has been used",
                    node.span))
                return false;
        }
    }
    bool constant_condition = false;
    bool truth = false;
    if ((node.kind == R_HIR_IF || node.kind == R_HIR_CONDITIONAL ||
         (node.kind == R_HIR_BINARY &&
          (node.operation == R_TOKEN_AMP_AMP || node.operation == R_TOKEN_PIPE_PIPE))) &&
        node.child_count != 0U)
        constant_condition =
            r_body_constant_bool(&check->body, context->hir_children[node.first_child], &truth);
    for (uint32_t i = 0U; i < node.child_count; ++i) {
        const bool conditional =
            i != 0U &&
            ((!constant_condition &&
              (node.kind == R_HIR_IF || node.kind == R_HIR_CONDITIONAL ||
               (node.kind == R_HIR_BINARY &&
                (node.operation == R_TOKEN_AMP_AMP || node.operation == R_TOKEN_PIPE_PIPE)))) ||
             node.kind == R_HIR_SWITCH || node.kind == R_HIR_WHILE || node.kind == R_HIR_FOR ||
             (node.kind == R_HIR_TRY && node.operation == R_TOKEN_KW_TRY &&
              i <= node.integer_value));
        if (!r_local_reassignment_walk(check,
                                       context->hir_children[node.first_child + i],
                                       guards + (conditional ? 1U : 0U),
                                       depth + 1U))
            return false;
    }
    return true;
}

static bool r_semantic_validate_local_reassignment(RFrontendContext *context) {
    RLocalReassignmentCheck check = {0};
    check.body.frontend = context;
    check.declaration_guards = r_context_allocate(
        context, (context->semantic_symbol_count + 1U) * sizeof(*check.declaration_guards));
    check.visited = r_context_allocate(context, context->hir_node_count + 1U);
    bool ok = check.declaration_guards != NULL && check.visited != NULL;
    if (ok) {
        memset(check.declaration_guards,
               0,
               (context->semantic_symbol_count + 1U) * sizeof(*check.declaration_guards));
        memset(check.visited, 0, context->hir_node_count + 1U);
        for (size_t i = 0U; ok && i < context->semantic_symbol_count; ++i) {
            const RSemanticSymbol *function = &context->semantic_symbols[i];
            if (function->kind == R_SEMANTIC_SYMBOL_FUNCTION && function->hir_node != 0U) {
                check.body.function_symbol = (RSymbolId)i + 1U;
                ok = r_local_reassignment_walk(&check, function->hir_node, 0U, 0U);
            }
        }
    }
    r_context_free(context, check.declaration_guards);
    r_context_free(context, check.visited);
    return ok;
}
