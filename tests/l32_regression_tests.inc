/* L32 (docs/language-completeness-roadmap.ru.md): core::Format (R-TYPE-0046) formats user
   types in formatted literals (R-EXPR-0028), through explicit calls, bounded generic code,
   interfaces (R-TYPE-0051) and @derive(format) (R-AGG-0012). Each case is accepted, or is
   rejected with the diagnostic code, the rule it names and a part of its message. */
static void r_semantic_test_l32_regressions(void) {
    static const char prelude[] =
        "struct Point { i32 x; i32 y; }; impl core::Format for Point { void format(const "
        "Point* this, std.format::builder* out) throws std.alloc::alloc_error { "
        "std.format::append_str(out, \"p\"); } }; struct Plain { i32 v; }; struct Holder { "
        "Point point; Plain plain; };";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"slot",
         "std.string::string f(Point p) throws std.alloc::alloc_error { return f\"<{p:6}>\"; }",
         NULL,
         NULL},
        {"borrow-slot",
         "std.string::string f(const Point* p, const i32* n) throws std.alloc::alloc_error { "
         "return f\"{p} {n}\"; }",
         NULL,
         NULL},
        {"bounded-generic",
         "@generic<T: core::Format> std.string::string f(const T* v) throws "
         "std.alloc::alloc_error { return f\"{v}\"; } std.string::string g() throws "
         "std.alloc::alloc_error { Point p = Point {.x = 1, .y = 2}; i32 n = 3; "
         "std.string::string a = f(&p); std.string::string b = f(&n); return f\"{a}{b}\"; }",
         NULL,
         NULL},
        {"standard-values",
         "std.string::string f(o<Point> a, Point[2] b, (i32, str) c, std.net::socket_address "
         "d) throws std.alloc::alloc_error { return f\"{a} {b} {c} {d}\"; }",
         NULL,
         NULL},
        {"text-width",
         "std.string::string f(str s, bool b, char c) throws std.alloc::alloc_error { return "
         "f\"{s:8}{b:6}{c:3}\"; }",
         NULL,
         NULL},
        {"explicit-calls",
         "void f(Holder h, o<i32> m, std.format::builder* out) throws std.alloc::alloc_error { "
         "h.point.format(out); m.format(out); const Point* view = &h.point; "
         "view->format(out); }",
         NULL,
         NULL},
        {"generic-explicit",
         "@generic<T: core::Format> void f(const T* v, std.format::builder* out) throws "
         "std.alloc::alloc_error { v->format(out); }",
         NULL,
         NULL},
        {"interface",
         "void f(const dyn(core::Format)* v, std.format::builder* out) throws "
         "std.alloc::alloc_error { v->format(out); } void g(std.format::builder* out) throws "
         "std.alloc::alloc_error { Point p = Point {.x = 1, .y = 2}; i32 n = 3; f(&p, out); "
         "f(&n, out); }",
         NULL,
         NULL},
        {"derived",
         "@derive(format) struct Size { u32 w; u32 h; }; @derive(format) enum Token { "
         "number(i64), word { str text; }, end }; @derive(format) @generic<T> struct Tag { T "
         "item; }; std.string::string f(Size s, Token t, Tag<Point> g) throws "
         "std.alloc::alloc_error { return f\"{s}{t}{g}\"; }",
         NULL,
         NULL},
        {"this-slot",
         "i32 Point::sum(const Point* this) { return this->x + this->y; } "
         "std.string::string Point::describe(const Point* this) throws std.alloc::alloc_error "
         "{ return f\"{this->x},{this->y}\"; }",
         NULL,
         NULL},
        {"no-contract",
         "std.string::string f(Plain p) throws std.alloc::alloc_error { return f\"{p}\"; }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "type has no formatting contract"},
        {"unbounded-generic",
         "@generic<T> std.string::string f(const T* v) throws std.alloc::alloc_error { return "
         "f\"{v}\"; }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "type has no formatting contract"},
        {"radix-on-user-type",
         "std.string::string f(Point p) throws std.alloc::alloc_error { return f\"{p:x}\"; }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "incompatible with the value type"},
        {"zero-fill-on-text",
         "std.string::string f(str s) throws std.alloc::alloc_error { return f\"{s:08}\"; }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "incompatible with the value type"},
        {"precision-on-bool",
         "std.string::string f(bool b) throws std.alloc::alloc_error { return f\"{b:.2}\"; }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "incompatible with the value type"},
        {"impl-for-tuple",
         "impl core::Format for (i32, i32) { void format(const (i32, i32)* this, "
         "std.format::builder* out) throws std.alloc::alloc_error { } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0046]",
         "nominal type"},
        {"impl-signature",
         "struct Other { i32 v; }; impl core::Format for Other { void format(const Other* "
         "this, std.format::builder* out) { } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0042]",
         "exact signature"},
        {"call-arity",
         "void f(Point p) throws std.alloc::alloc_error { p.format(); }",
         "R-DIAG-TYPE-001 [R-TYPE-0046]",
         "takes one std.format::builder* argument"},
        {"call-without-contract",
         "void f(Plain p, std.format::builder* out) throws std.alloc::alloc_error { "
         "p.format(out); }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "satisfies core::Format"},
        {"member-without-contract",
         "void f(Holder h, std.format::builder* out) throws std.alloc::alloc_error { "
         "h.plain.format(out); }",
         "R-DIAG-FORMAT-001 [R-EXPR-0028]",
         "satisfies core::Format"},
        {"recursive-call",
         "struct Node { i32 v; }; impl core::Format for Node { void format(const Node* this, "
         "std.format::builder* out) throws std.alloc::alloc_error { this->format(out); } };",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         "recursive call chain"},
        {"recursive-render",
         "struct Node { i32 v; }; impl core::Format for Node { void format(const Node* this, "
         "std.format::builder* out) throws std.alloc::alloc_error { std.string::string text = "
         "f\"{this}\"; std.format::append_str(out, text.as_str()); } };",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         "recursive call chain"},
        {"noalloc",
         "@noalloc void f(const i32* n, std.format::builder* out) throws std.alloc::alloc_error "
         "{ n->format(out); }",
         "R-DIAG-RESOURCE-001 [R-FUNC-0019]",
         "formatting allocates its text"},
        {"nonblocking",
         "@nonblocking void f(o<i32> n, std.format::builder* out) throws "
         "std.alloc::alloc_error { n.format(out); }",
         "R-DIAG-RESOURCE-002 [R-FUNC-0019]",
         "no nonblocking proof"},
        {"derive-member",
         "@derive(format) struct Bad { i32 a; Plain inner; };",
         "R-DIAG-TRAIT-001 [R-AGG-0012]",
         "satisfy core::Format; `inner` does not"},
        {"derive-duplicate",
         "@derive(format) struct Twice { i32 x; }; impl core::Format for Twice { void "
         "format(const Twice* this, std.format::builder* out) throws std.alloc::alloc_error { "
         "} };",
         "R-DIAG-TRAIT-001 [R-TYPE-0042]",
         "duplicate implementation"},
        {"interface-member-without-contract",
         "void f(const dyn(core::Format)* v) { v as void; } void g() { Plain p = Plain {.v = "
         "1}; f(&p); }",
         "R-DIAG-TRAIT-001",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(
            source, sizeof(source), "module test.l32_regressions; %s %s", prelude, cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
            if ((status == R_FRONTEND_NOT_LOWERABLE) && (context->diagnostic_count != 0U)) {
                status = R_FRONTEND_INVALID_SOURCE;
            }
        }
        bool message_found = cases[i].message == NULL;
        for (size_t d = 0U; d < context->diagnostic_count; ++d) {
            if ((cases[i].message != NULL) &&
                (strstr(context->diagnostics[d].message, cases[i].message) != NULL)) {
                message_found = true;
            }
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            message_found && r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l32 regression %s status %d\n", cases[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s [%s]: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].rule_id,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}
