/* L12 (docs/language-completeness-roadmap.ru.md): the region of what a parameter holds or
 * designates, apart from the parameter's own storage (defect L11-14). Each case is accepted, or
 * keeps its normative diagnostic. */
static void r_semantic_test_l12_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L12, R-BORROW-0018: what a parameter holds or designates has a region of its own; a view
           read from it survives exclusive access to the parameter storage. */
        {"parameter-container-element-rename",
         "struct Named { str name; i32 id; }; usize rename_first(array<Named>* items) { str second "
         "= \"\"; { const array<Named>* shared = items; switch (std.array::get(shared, 1usize)) { "
         "case variant o::some(entry): second = (*entry)->name; break; case variant o::none: "
         "break; } } switch (std.array::get_mut(items, 0usize)) { case variant o::some(entry): "
         "(*entry)->name = second; break; case variant o::none: break; } return len(second); }",
         NULL},
        {"parameter-holder-field-swap",
         "struct Pair { str left; str right; }; usize swap(Pair* p) { str a = p->left; p->left = "
         "p->right; p->right = a; return len(a); }",
         NULL},
        {"parameter-container-fill-from-element",
         "usize fill(array<str>* names) { str first = \"\"; { const array<str>* shared = names; "
         "switch (std.array::get(shared, 0usize)) { case variant o::some(e): first = **e; break; "
         "case variant o::none: break; } } for (str* n in names) { *n = first; } return "
         "len(first); }",
         NULL},
        {"parameter-list-reinsert-front",
         "struct Named { str name; i32 id; }; usize keep_first(list<Named>* items) throws "
         "std.list::push_error<Named> { str first = \"\"; { const list<Named>* shared = items; "
         "switch (std.list::front(shared)) { case variant o::some(front): first = (*front)->name; "
         "break; case variant o::none: break; } } std.list::push_back(items, Named {.name = first, "
         ".id = 2}) as void; return len(first); }",
         NULL},
        {"parameter-content-survives-clear",
         "struct Named { str name; i32 id; }; usize reset(array<Named>* items) throws "
         "std.array::push_error<Named> { str first = \"\"; { const array<Named>* shared = items; "
         "switch (std.array::get(shared, 0usize)) { case variant o::some(entry): first = "
         "(*entry)->name; break; case variant o::none: break; } } std.array::clear(items); "
         "std.array::push(items, Named {.name = first, .id = 1}); return len(first); }",
         NULL},
        {"slice-parameter-rotate",
         "struct Named { str name; i32 id; }; usize rotate(str[] w) { str first = w[0]; w[0] = "
         "w[1]; w[1] = first; return len(first); } usize rotate_named(Named[] w) { str first = "
         "w[0].name; w[0].name = w[1].name; w[1].name = first; return len(first); }",
         NULL},
        {"by-value-holder-parameter-rewrite",
         "struct Named { str name; i32 id; }; usize rename(Named n) { str old = n.name; n.name = "
         "\"x\"; return len(old) + len(n.name); }",
         NULL},
        {"parameter-content-result",
         "struct Named { str name; i32 id; }; str first_name(const array<Named>* items) { switch "
         "(std.array::get(items, 0usize)) { case variant o::some(entry): return (*entry)->name; "
         "case variant o::none: break; } return \"\"; } usize probe(const array<Named>* items) { "
         "str name = first_name(items); return len(name); }",
         NULL},
        /* Exclusive access to the storage a parameter designates still conflicts with a borrow of
           that storage. */
        {"parameter-storage-reborrow-conflict",
         "struct Named { str name; i32 id; }; usize f(array<Named>* items) { const array<Named>* "
         "shared = items; std.array::clear(items); return len(*shared); }",
         "R-DIAG-BORROW-001"},
        {"parameter-element-borrow-conflict",
         "struct Named { str name; i32 id; }; const Named* first_entry(const array<Named>* items) "
         "{ switch (std.array::get(items, 0usize)) { case variant o::some(entry): return *entry; "
         "case variant o::none: break; } panic(\"empty\"); } usize probe(array<Named>* items) { "
         "const Named* e = first_entry(items); std.array::clear(items); return len(e->name); }",
         "R-DIAG-BORROW-001"},
        {"by-value-holder-parameter-borrow-conflict",
         "struct Named { str name; i32 id; }; usize f(Named n) { const Named* p = &n; n.name = "
         "\"x\"; return len(p->name); }",
         "R-DIAG-BORROW-001"},
        {"parameter-container-foreign-store",
         "struct Named { str name; i32 id; }; void keep(array<Named>* items, str other) throws "
         "std.array::push_error<Named> { std.array::push(items, Named {.name = other, .id = 1}); }",
         NULL},
        {"parameter-container-local-store",
         "struct Named { str name; i32 id; }; void keep(array<Named>* items) throws "
         "std.alloc::alloc_error, std.array::push_error<Named> { std.string::string temp = "
         "std.string::from_str(\"t\"); std.array::push(items, Named {.name = "
         "std.string::as_str(&temp), .id = 1}); }",
         "R-DIAG-BORROW-002"},
        /* R-BORROW-0024: a view read from async main args stays unusable after a suspension. */
        {"async-main-argument-content-after-suspension",
         "async i32 main(const str[] args) { std.time::duration pause = "
         "std.time::duration_from_seconds(0i64); await pause.sleep_for(); usize total = 0usize; "
         "for (str arg in &args) { total += len(arg); } return total as i32; }",
         "R-DIAG-ASYNC-001"},
        {"async-main-argument-view-kept-across-suspension",
         "async i32 main(const str[] args) { str first = \"\"; if (len(args) > 1usize) { first = "
         "args[1]; } std.time::duration pause = std.time::duration_from_seconds(0i64); await "
         "pause.sleep_for(); return len(first) as i32; }",
         "R-DIAG-ASYNC-001"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l12_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_diagnostic_code_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l12 regression %s status %d\n", cases[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}
