/* Component liveness uses full field/index paths. An unknown projection remains a
 * use of the whole prefix; the analysis never guesses that dynamic indices differ. */
static RHirNode *r_body_hir_node(const RBodyContext *, RHirNodeId);
static bool r_body_hir_child(const RBodyContext *, const RHirNode *, uint32_t, RHirNodeId *);
static bool r_body_constant_index(const RBodyContext *, RHirNodeId, uint64_t *);

static const RHirNode *r_body_future_path_segment(const RBodyContext *body,
                                                  RHirNodeId path,
                                                  uint32_t ordinal,
                                                  bool *complete) {
    RHirNodeId current = path;
    uint32_t length = 0U;
    *complete = false;
    for (; length <= body->frontend->options.limits.max_nesting; ++length) {
        const RHirNode *node = r_body_hir_node(body, current);
        if (node == NULL)
            return NULL;
        if (node->kind == R_HIR_PLACE)
            break;
        if ((node->kind != R_HIR_FIELD_PLACE && node->kind != R_HIR_INDEX_PLACE) ||
            !r_body_hir_child(body, node, 0U, &current))
            return NULL;
    }
    if (length > body->frontend->options.limits.max_nesting)
        return NULL;
    if (ordinal >= length) {
        *complete = true;
        return NULL;
    }
    current = path;
    while (length > ordinal + 1U) {
        if (!r_body_hir_child(body, r_body_hir_node(body, current), 0U, &current))
            return NULL;
        --length;
    }
    return r_body_hir_node(body, current);
}

static bool r_body_ast_postfix_uses_projection(RBodyContext *body,
                                               RAstRef ref,
                                               const RSemanticSymbol *symbol,
                                               RHirNodeId path) {
    RAstNodeView view, primary_view;
    RAstRef primary;
    if (!r_ast_ref_view(body->frontend, ref, &view) || view.kind != R_SYNTAX_POSTFIX_EXPRESSION ||
        view.child_count == 0U || !r_ast_ref_child(body->frontend, ref, 0U, &primary) ||
        !r_ast_ref_view(body->frontend, primary, &primary_view) ||
        primary_view.kind != R_SYNTAX_PRIMARY_EXPRESSION)
        return false;
    bool root_matches = false;
    for (uint32_t index = 0U; index < primary_view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (r_ast_ref_child(body->frontend, primary, index, &child) &&
            r_ast_ref_token(body->frontend, child, &token) && token.kind == R_TOKEN_IDENTIFIER &&
            token.intern_id == symbol->name_intern_id)
            root_matches = true;
    }
    if (!root_matches)
        return false;
    uint32_t ordinal = 0U;
    RAstRef index_expression = {0};
    bool has_index_expression = false;
    for (uint32_t index = 1U; index < view.child_count; ++index) {
        bool complete;
        const RHirNode *expected = r_body_future_path_segment(body, path, ordinal, &complete);
        RAstRef suffix;
        RAstNodeView suffix_view;
        if (expected == NULL || complete || !r_ast_ref_child(body->frontend, ref, index, &suffix) ||
            !r_ast_ref_view(body->frontend, suffix, &suffix_view))
            return true;
        /* The parser stores the lower bound before its subscript suffix. */
        if (suffix_view.kind == R_SYNTAX_EXPRESSION) {
            index_expression = suffix;
            has_index_expression = true;
            continue;
        }
        ++ordinal;
        if (suffix_view.kind == R_SYNTAX_MEMBER_SUFFIX && expected->kind == R_HIR_FIELD_PLACE) {
            RAstTokenView name;
            if (expected->aggregate_member == 0U ||
                expected->aggregate_member > body->frontend->semantic_field_count ||
                !r_semantic_first_identifier(body->frontend, suffix, &name))
                return true;
            if (body->frontend->semantic_fields[expected->aggregate_member - 1U].name_intern_id !=
                name.intern_id)
                return false;
        } else if (suffix_view.kind == R_SYNTAX_INDEX_SUFFIX &&
                   expected->kind == R_HIR_INDEX_PLACE) {
            RHirNodeId expected_index;
            RToken literal;
            RTokenKind explicit_kind;
            uint32_t count = 0U;
            uint64_t actual, wanted;
            if (!has_index_expression ||
                !r_semantic_single_integer_token(
                    body->frontend, index_expression, 0U, &literal, &count) ||
                count != 1U ||
                r_semantic_parse_integer_token(body->frontend, &literal, &actual, &explicit_kind) !=
                    R_INTEGER_TOKEN_VALID ||
                !r_body_hir_child(body, expected, 1U, &expected_index) ||
                !r_body_constant_index(body, expected_index, &wanted))
                return true;
            if (actual != wanted)
                return false;
            has_index_expression = false;
        } else {
            return true;
        }
    }
    return true;
}
