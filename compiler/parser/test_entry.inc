/* R-FUNC-0025 (M24): test mode. The entry module of the program gets the entry point
 * `async i32 main()`, which runs the test functions of the module in declaration order through
 * the runner of std.test (Library R-SLIB-TEST-0002). The entry is ordinary R text generated
 * from the tokens of the declarations and appended after the authored text of the module, one
 * region per test function, so that a diagnostic inside the code of a test is reported at its
 * attribute, like a derived declaration (R-AGG-0012). The declarations themselves are checked
 * by the semantic pass; a declaration that the generator cannot read gets no code. */

typedef struct RTestFunction {
    size_t attribute;
    size_t name;
    size_t expect_first;
    size_t expect_last;
    size_t throws_first;
    size_t throws_end;
    bool is_async;
    bool allocations;
    bool has_expect;
    bool has_throws;
} RTestFunction;

typedef struct RTestWriter {
    RFrontendContext *context;
    RTextBuilder *output;
    const RSource *source;
    const char *prefix;
    bool ok;
} RTestWriter;

/* Whether `source_id` is the entry module of test mode: the module named by the entry option,
   or else the first source that the library map did not supply. */
static bool r_test_entry_source(const RFrontendContext *context, RSourceId source_id) {
    const RSource *source = r_get_source_const(context, source_id);
    if (!context->test_mode || (source == NULL) || source->is_library) {
        return false;
    }
    if (context->test_module != NULL) {
        return (source->module_name != NULL) &&
               (strcmp(source->module_name, context->test_module) == 0);
    }
    for (size_t index = 0U; index < context->source_count; ++index) {
        if (!context->sources[index].is_library) {
            return (RSourceId)(index + 1U) == source_id;
        }
    }
    return false;
}

static void r_test_write(RTestWriter *writer, const char *text) {
    writer->ok = writer->ok &&
                 r_text_builder_append(writer->context, writer->output, text, strlen(text));
}

/* The authored text from the first to the last token of a range. */
static void r_test_write_tokens(RTestWriter *writer, size_t first, size_t last) {
    const RToken *start = r_scan_token(writer->source, first);
    const RToken *end = r_scan_token(writer->source, last);
    if ((start == NULL) || (end == NULL) || (end->span.end < start->span.start)) {
        writer->ok = false;
        return;
    }
    writer->ok = writer->ok &&
                 r_text_builder_append(writer->context,
                                       writer->output,
                                       (const char *)writer->source->bytes + start->span.start,
                                       (size_t)(end->span.end - start->span.start));
}

static void r_test_write_local(RTestWriter *writer, const char *suffix) {
    r_test_write(writer, writer->prefix);
    r_test_write(writer, suffix);
}

/* The index of the last significant token before `end`, starting at `first`. */
static size_t r_test_last_before(const RSource *source, size_t first, size_t end) {
    size_t last = first;
    for (size_t index = first; index < end; index = r_derive_next(source, index)) {
        last = index;
    }
    return last;
}

/* Reads the declaration that follows the attribute `@test` at `at`; false when it is not a
   function the generator can call. */
static bool r_test_read(const RSource *source, size_t at, RTestFunction *test) {
    size_t cursor;
    (void)memset(test, 0, sizeof(*test));
    test->attribute = r_derive_next(source, at);
    cursor = r_derive_next(source, test->attribute);
    if (r_scan_kind(source, cursor) == R_TOKEN_LPAREN) {
        const size_t close = r_derive_close(source, cursor);
        const size_t inner = r_derive_next(source, cursor);
        if (r_derive_token_is(source, inner, "allocations") &&
            (r_derive_next(source, inner) == close)) {
            test->allocations = true;
        } else if (r_derive_token_is(source, inner, "expect") &&
                   (r_scan_kind(source, r_derive_next(source, inner)) == R_TOKEN_EQUAL) &&
                   (r_derive_next(source, r_derive_next(source, inner)) < close)) {
            test->has_expect = true;
            test->expect_first = r_derive_next(source, r_derive_next(source, inner));
            test->expect_last = r_test_last_before(source, test->expect_first, close);
        } else {
            return false;
        }
        cursor = r_derive_next(source, close);
    }
    while (r_scan_kind(source, cursor) == R_TOKEN_AT) {
        cursor = r_derive_next(source, r_derive_next(source, cursor));
        if (r_scan_kind(source, cursor) == R_TOKEN_LPAREN) {
            cursor = r_derive_next(source, r_derive_close(source, cursor));
        }
    }
    while ((r_scan_kind(source, cursor) != R_TOKEN_LPAREN) &&
           (r_scan_kind(source, cursor) != R_TOKEN_EOF) &&
           (r_scan_kind(source, cursor) != R_TOKEN_LBRACE) &&
           (r_scan_kind(source, cursor) != R_TOKEN_SEMICOLON)) {
        if (r_scan_kind(source, cursor) == R_TOKEN_KW_ASYNC) {
            test->is_async = true;
        }
        test->name = cursor;
        cursor = r_derive_next(source, cursor);
    }
    if ((r_scan_kind(source, cursor) != R_TOKEN_LPAREN) ||
        (r_scan_kind(source, test->name) != R_TOKEN_IDENTIFIER) ||
        (r_scan_kind(source, r_derive_next(source, cursor)) != R_TOKEN_RPAREN)) {
        return false;
    }
    cursor = r_derive_next(source, r_derive_next(source, cursor));
    if (r_scan_kind(source, cursor) == R_TOKEN_KW_THROWS) {
        test->has_throws = true;
        test->throws_first = r_derive_next(source, cursor);
        test->throws_end = test->throws_first;
        while ((r_scan_kind(source, test->throws_end) != R_TOKEN_LBRACE) &&
               (r_scan_kind(source, test->throws_end) != R_TOKEN_SEMICOLON) &&
               (r_scan_kind(source, test->throws_end) != R_TOKEN_EOF)) {
            test->throws_end = r_derive_next(source, test->throws_end);
        }
        return (r_scan_kind(source, test->throws_end) == R_TOKEN_LBRACE) &&
               (test->throws_end != test->throws_first);
    }
    return r_scan_kind(source, cursor) == R_TOKEN_LBRACE;
}

/* `catch (T PREFIXerror) { PREFIXrunner.unexpected(move PREFIXerror); }` for each type of the
   declared list, in order. */
static void r_test_write_declared_catches(RTestWriter *writer, const RTestFunction *test) {
    const RSource *source = writer->source;
    size_t first = test->throws_first;
    uint32_t angles = 0U;
    for (size_t index = test->throws_first; index <= test->throws_end;
         index = r_derive_next(source, index)) {
        const RTokenKind kind = r_scan_kind(source, index);
        if (kind == R_TOKEN_LESS) {
            angles += 1U;
        } else if (kind == R_TOKEN_GREATER) {
            angles = angles != 0U ? angles - 1U : 0U;
        } else if (kind == R_TOKEN_GREATER_GREATER) {
            angles = angles > 1U ? angles - 2U : 0U;
        }
        if (((kind == R_TOKEN_COMMA) && (angles == 0U)) || (index == test->throws_end)) {
            r_test_write(writer, " catch (");
            r_test_write_tokens(writer, first, r_test_last_before(source, first, index));
            r_test_write(writer, " ");
            r_test_write_local(writer, "error) { ");
            r_test_write_local(writer, "runner.unexpected(move ");
            r_test_write_local(writer, "error); }");
            first = r_derive_next(source, index);
        }
    }
}

/* One run of the test: the call inside the catches of std.test::failure and of the standard
   errors, inside the catches of its declared errors. With `expect`, the expected error is
   caught first; `allocation` catches std.alloc::alloc_error as the expected outcome of a run
   with an injected failure. */
static void r_test_write_run(RTestWriter *writer, const RTestFunction *test, bool allocation) {
    if (test->has_throws) {
        r_test_write(writer, "        try {\n");
    }
    r_test_write(writer, "            try {\n");
    if (test->has_expect || allocation) {
        r_test_write(writer, "                try { ");
    } else {
        r_test_write(writer, "                ");
    }
    if (test->is_async) {
        r_test_write(writer, "await ");
    }
    r_test_write_tokens(writer, test->name, test->name);
    r_test_write(writer, "();");
    if (test->has_expect) {
        r_test_write(writer, " ");
        r_test_write_local(writer, "runner.missing(\"");
        r_test_write_tokens(writer, test->expect_first, test->expect_last);
        r_test_write(writer, "\"); }\n                catch (");
        r_test_write_tokens(writer, test->expect_first, test->expect_last);
        r_test_write(writer, " ");
        r_test_write_local(writer, "expected) { ");
        r_test_write_local(writer, "runner.expected(move ");
        r_test_write_local(writer, "expected); }");
    } else if (allocation) {
        r_test_write(writer, " }\n                catch (std.alloc::alloc_error ");
        r_test_write_local(writer, "allocation) { ");
        r_test_write_local(writer, "allocation as void; }");
    }
    r_test_write(writer, "\n            }\n            catch (std.test::failure ");
    r_test_write_local(writer, "failure) { ");
    r_test_write_local(writer, "runner.fail(&");
    r_test_write_local(writer, "failure); }\n            catch (std.error::fault ");
    r_test_write_local(writer, "fault) { ");
    r_test_write_local(writer, "runner.fault(");
    r_test_write_local(writer, "fault); }\n");
    if (test->has_throws) {
        r_test_write(writer, "        }");
        r_test_write_declared_catches(writer, test);
        r_test_write(writer, "\n");
    }
}

/* The block of one test: its start line, its run, the reruns with each allocation attempt
   failing, and its outcome line. */
static void r_test_write_block(RTestWriter *writer, const RTestFunction *test) {
    r_test_write(writer, "    {\n        await std.console::print(");
    r_test_write_local(writer, "runner.start(\"");
    r_test_write_tokens(writer, test->name, test->name);
    r_test_write(writer, "\"));\n");
    if (test->allocations) {
        r_test_write(writer, "        std.test::fail_allocation_at(0u64);\n");
    }
    r_test_write_run(writer, test, false);
    if (test->allocations) {
        r_test_write(writer, "        u64 ");
        r_test_write_local(writer, "attempts = std.test::allocation_attempts();\n");
        r_test_write(writer, "        for (u64 ");
        r_test_write_local(writer, "attempt = 1u64; ");
        r_test_write_local(writer, "attempt <= ");
        r_test_write_local(writer, "attempts && ");
        r_test_write_local(writer, "runner.passing() == true; ");
        r_test_write_local(writer, "attempt += 1u64) {\n");
        r_test_write(writer, "        ");
        r_test_write_local(writer, "runner.inject(");
        r_test_write_local(writer, "attempt, ");
        r_test_write_local(writer, "attempts);\n");
        r_test_write(writer, "        std.test::fail_allocation_at(");
        r_test_write_local(writer, "attempt);\n");
        r_test_write_run(writer, test, true);
        r_test_write(writer, "        std.test::fail_allocation_at(0u64);\n        }\n        ");
        r_test_write_local(writer, "runner.inject(0u64, ");
        r_test_write_local(writer, "attempts);\n");
    }
    r_test_write(writer, "        await std.console::print(");
    r_test_write_local(writer, "runner.finish());\n    }\n");
}

/* A prefix for the generated locals that no identifier of the source starts with. */
static bool r_test_prefix(const RSource *source, char *prefix, size_t capacity) {
    for (uint32_t attempt = 0U; attempt < 1000U; ++attempt) {
        bool taken = false;
        size_t length;
        if (attempt == 0U) {
            (void)snprintf(prefix, capacity, "r_test_");
        } else {
            (void)snprintf(prefix, capacity, "r_test%u_", (unsigned int)attempt);
        }
        length = strlen(prefix);
        for (size_t index = 0U; !taken && (index < source->token_count); ++index) {
            const RToken *token = &source->tokens[index];
            taken = (token->kind == R_TOKEN_IDENTIFIER) &&
                    ((size_t)(token->span.end - token->span.start) >= length) &&
                    (memcmp(source->bytes + token->span.start, prefix, length) == 0);
        }
        if (!taken) {
            return true;
        }
    }
    return false;
}

static bool r_test_add_region(RFrontendContext *context,
                              RSource *source,
                              size_t base,
                              size_t start,
                              size_t end,
                              RSourceSpan origin) {
    if (!r_grow_array(context,
                      (void **)&source->derive_regions,
                      &source->derive_region_capacity,
                      sizeof(*source->derive_regions),
                      source->derive_region_count + 1U)) {
        return false;
    }
    source->derive_regions[source->derive_region_count].start = (uint32_t)(base + start);
    source->derive_regions[source->derive_region_count].end = (uint32_t)(base + end);
    source->derive_regions[source->derive_region_count].origin = origin;
    source->derive_regions[source->derive_region_count].suppressed = false;
    source->derive_regions[source->derive_region_count].incomplete = false;
    source->derive_region_count += 1U;
    return true;
}

RFrontendStatus r_test_expand(RFrontendContext *context, RSourceId source_id) {
    RSource *source = r_get_source(context, source_id);
    RTextBuilder output = {0};
    RTestWriter writer;
    RSourceSpan module_origin;
    char prefix[32];
    size_t depth = 0U;
    size_t base;
    size_t prologue_end;
    bool success = true;

    if ((source == NULL) || source->test_expanded || !r_test_entry_source(context, source_id)) {
        return R_FRONTEND_OK;
    }
    source->test_expanded = true;
    if (!source->lexed || source->has_lex_error || (source->tokens == NULL) ||
        !r_test_prefix(source, prefix, sizeof(prefix))) {
        return R_FRONTEND_OK;
    }
    module_origin = source->tokens[r_skip_trivia(source, 0U)].span;
    /* R-FUNC-0025: the entry module of test mode declares no main of its own. */
    for (size_t index = r_skip_trivia(source, 0U); index < source->token_count;
         index = r_derive_next(source, index)) {
        const RTokenKind kind = r_scan_kind(source, index);
        if ((size_t)source->tokens[index].span.start >= source->authored_length) {
            break;
        }
        if (kind == R_TOKEN_LBRACE) {
            depth += 1U;
        } else if ((kind == R_TOKEN_RBRACE) && (depth != 0U)) {
            depth -= 1U;
        } else if ((depth == 0U) && r_derive_token_is(source, index, "main") &&
                   (r_scan_kind(source, r_derive_next(source, index)) == R_TOKEN_LPAREN)) {
            (void)r_add_diagnostic(context,
                                   "R-DIAG-FLOW-001",
                                   "R-FUNC-0025",
                                   "in test mode the entry module shall not declare main",
                                   R_DIAGNOSTIC_ERROR,
                                   source->tokens[index].span);
            return context->resource_status;
        }
    }
    base = source->length;
    writer.context = context;
    writer.output = &output;
    writer.source = source;
    writer.prefix = prefix;
    writer.ok = true;
    r_test_write(&writer, "\nasync i32 main() {\n    std.test::runner ");
    r_test_write_local(&writer, "runner = std.test::runner::create();\n");
    prologue_end = output.count;
    success =
        writer.ok && r_test_add_region(context, source, base, 0U, prologue_end, module_origin);
    depth = 0U;
    for (size_t index = r_skip_trivia(source, 0U); success && (index < source->token_count);
         index = r_derive_next(source, index)) {
        const RTokenKind kind = r_scan_kind(source, index);
        RTestFunction test;
        size_t start;
        if ((kind == R_TOKEN_EOF) ||
            ((size_t)source->tokens[index].span.start >= source->authored_length)) {
            break;
        }
        if (kind == R_TOKEN_LBRACE) {
            depth += 1U;
        } else if ((kind == R_TOKEN_RBRACE) && (depth != 0U)) {
            depth -= 1U;
        }
        if ((depth != 0U) || (kind != R_TOKEN_AT) ||
            !r_derive_token_is(source, r_derive_next(source, index), "test") ||
            !r_test_read(source, index, &test)) {
            continue;
        }
        start = output.count;
        r_test_write_block(&writer, &test);
        success = writer.ok && r_test_add_region(context,
                                                 source,
                                                 base,
                                                 start,
                                                 output.count,
                                                 source->tokens[test.attribute].span);
    }
    if (success) {
        const size_t start = output.count;
        r_test_write(&writer, "    await std.console::print(");
        r_test_write_local(&writer, "runner.summary());\n    return ");
        r_test_write_local(&writer, "runner.status();\n}\n");
        success = writer.ok &&
                  r_test_add_region(context, source, base, start, output.count, module_origin);
    }
    if (success && (output.count != 0U)) {
        success = r_lex_append(context, source_id, output.items, output.count);
    }
    r_context_free(context, output.items);
    if (!success) {
        return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                         : R_FRONTEND_INTERNAL_ERROR;
    }
    return R_FRONTEND_OK;
}

/* R-FUNC-0025: the entry of test mode names std.test and std.console, which the module then
   imports. */
static bool r_test_scan_imports(RFrontendContext *context, RSourceId source_id, RSource *source) {
    static const char *const modules[] = {"std.test", "std.console"};
    const RToken *token;
    if (!r_test_entry_source(context, source_id)) {
        return true;
    }
    token = r_scan_token(source, r_skip_trivia(source, 0U));
    if (token == NULL) {
        return true;
    }
    for (size_t module = 0U; module < sizeof(modules) / sizeof(modules[0]); ++module) {
        bool present = false;
        for (size_t import = 0U; !present && (import < context->import_count); ++import) {
            present = (context->imports[import].source == source_id) &&
                      !context->imports[import].imports_name &&
                      (strcmp(context->imports[import].module_name, modules[module]) == 0);
        }
        if (!present &&
            !r_add_import_entry(context, source_id, modules[module], token->span, source, NULL)) {
            return false;
        }
    }
    return true;
}
