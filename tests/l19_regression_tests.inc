/* L19 (docs/language-completeness-roadmap.ru.md): calls continued after @chain methods.
 * Each case is accepted, or is rejected with the diagnostic code and, where given, the rule it
 * names; no case may leave a construct outside the implemented slice (R-DIAG-SLICE-001). */
#define R_L19_OPTIONS                                                                              \
    "struct Options { u16 port; usize capacity; }; Options Options::create() { return Options "    \
    "{.port = 8080u16, .capacity = 64usize}; } @chain Options Options::with_port(Options this, "   \
    "u16 value) { this.port = value; return move this; } @chain Options "                          \
    "Options::with_capacity(Options this, usize value) { this.capacity = value; return move "      \
    "this; } Options Options::plain(Options this, u16 value) { this.port = value; return move "    \
    "this; } "

static void r_semantic_test_l19_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L19.1, R-FUNC-0024: consuming links, borrow links and the suffixes after them. */
        {"chain-consuming-builder",
         R_L19_OPTIONS "u16 f() { Options options = "
                       "Options::create().with_port(1u16).with_capacity(2usize); return "
                       "options.port; }",
         NULL},
        {"chain-then-field",
         R_L19_OPTIONS "usize f() { return Options::create().with_port(1u16).capacity; }",
         NULL},
        {"chain-field-after-plain-method",
         R_L19_OPTIONS "u16 f() { return (Options::create().plain(1u16)).plain(2u16).port; }",
         "R-DIAG-SYN-001 [R-FUNC-0024]"},
        {"chain-parenthesized-receiver",
         R_L19_OPTIONS "u16 f() { Options options = (Options::create().plain(1u16)).plain(2u16); "
                       "return options.port; }",
         NULL},
        {"chain-borrow-arrow",
         "struct Counter { i32 total; }; @chain Counter* Counter::add(Counter* this, i32 value) { "
         "this->total += value; return this; } i32 Counter::value(const Counter* this) { return "
         "this->total; } i32 f() { Counter c = {.total = 0}; c.add(1)->add(2) as void; return "
         "c.add(3)->value(); }",
         NULL},
        {"chain-discardable-borrow",
         "struct Counter { i32 total; }; @discardable @chain Counter* Counter::add(Counter* this, "
         "i32 value) { this->total += value; return this; } i32 f() { Counter c = {.total = 0}; "
         "c.add(1)->add(2); return c.total; }",
         NULL},
        {"chain-then-index-and-element",
         "struct Grid { i32[3] cells; }; @chain i32[3] Grid::row(const Grid* this) { return "
         "this->cells; } @chain (i32, bool) Grid::first(const Grid* this) { return "
         "(this->cells[0], true); } i32 f() { Grid g = {.cells = {1, 2, 3}}; return g.row()[1] + "
         "g.first().0; }",
         NULL},
        {"chain-throwing-link",
         "error Invalid { i32 code; }; struct Options { u16 port; }; @chain Options "
         "Options::checked(Options this, u16 value) throws Invalid { throw (value == 0u16) Invalid "
         "{.code = 1}; this.port = value; return move this; } u16 f() throws Invalid { Options o1 "
         "= {.port = 1u16}; return o1.checked(2u16).checked(3u16).port; }",
         NULL},
        {"chain-generic-owner",
         "@generic<T: copy> struct Cell { T value; }; @generic<T: copy> @chain Cell<T> "
         "Cell<T>::put(Cell<T> this, T value) { this.value = value; return move this; } i32 f() { "
         "Cell<i32> cell = {.value = 0}; return cell.put(1).put(2).value; }",
         NULL},
        /* L19.2: the permission of a trait prototype through constraints, dyn and impls. */
        {"chain-trait-generic",
         "trait Scaled { @chain Self scaled(Self this, i32 factor); }; struct Meter { i32 amount; "
         "}; impl Scaled for Meter { Meter scaled(Meter this, i32 factor) { this.amount *= "
         "factor; return move this; } }; @generic<T: Scaled> T six(T value) { return (move "
         "value).scaled(2).scaled(3); } i32 f() { return six(Meter {.amount = 1}).amount; }",
         NULL},
        {"chain-dyn-and-impl",
         "struct Label { i32 size; }; i32 Label::width(Label this) { return this.size * 2; } trait "
         "Named { @chain Label label(const Self* this); }; struct Item { i32 size; }; impl Named "
         "for Item { Label label(const Item* this) { return Label {.size = this->size}; } }; i32 "
         "via(const dyn(Named)* value) { return value->label().width(); } i32 f() { Item item = "
         "{.size = 5}; const dyn(Named)* erased = &item; return item.label().width() + "
         "via(erased); }",
         NULL},
        /* L19.3: chains in asynchronous frames, on await results and before an await. */
        {"chain-async",
         R_L19_OPTIONS "protected async Options fetch() { return Options::create(); } protected "
                       "async u16 f() throws std.async::start_error { Options options = (await "
                       "fetch()).with_port(1u16).with_capacity(2usize); return options.port; }",
         NULL},
        /* Rejections: an unmarked link, and @chain where it does not belong. */
        {"chain-after-plain-method",
         R_L19_OPTIONS "u16 f() { return Options::create().plain(1u16).with_port(2u16).port; }",
         "R-DIAG-SYN-001 [R-FUNC-0024]"},
        {"chain-after-standard-method",
         "import std.string; usize f() throws std.alloc::alloc_error { std.string::string s = "
         "std.string::from_str(\"ab\"); usize n = s.as_str().len(); drop s; return n; }",
         "R-DIAG-SYN-001 [R-FUNC-0024]"},
        {"chain-on-free-function", "@chain i32 f() { return 1; }", "R-DIAG-SYN-002 [R-FUNC-0024]"},
        {"chain-on-associated-function",
         "struct Options { u16 port; }; @chain Options Options::fresh() { return Options {.port = "
         "1u16}; }",
         "R-DIAG-SYN-002 [R-FUNC-0024]"},
        {"chain-on-void-method",
         "struct Options { u16 port; }; @chain void Options::reset(Options* this) { this->port = "
         "0u16; }",
         "R-DIAG-SYN-002 [R-FUNC-0024]"},
        {"chain-on-async-method",
         "struct Options { u16 port; }; @chain async Options Options::later(Options this) { "
         "return move this; }",
         "R-DIAG-SYN-002 [R-FUNC-0024]"},
        {"chain-duplicate",
         "struct Options { u16 port; }; @chain @chain Options Options::same(Options this) { "
         "return move this; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]"},
        {"chain-impl-of-plain-prototype",
         "trait Scaled { Self scaled(Self this, i32 factor); }; struct Meter { i32 amount; }; impl "
         "Scaled for Meter { @chain Meter scaled(Meter this, i32 factor) { this.amount *= factor; "
         "return move this; } };",
         "R-DIAG-TRAIT-001 [R-FUNC-0024]"},
        {"chain-redeclaration-mismatch",
         "struct Options { u16 port; }; Options Options::same(Options this); @chain Options "
         "Options::same(Options this) { return move this; }",
         "R-DIAG-NAME-002 [R-NAME-0004]"},
        {"chain-borrows-temporary",
         "struct Text { i32 v; }; Text Text::make() { return Text {.v = 3}; } @chain Text "
         "Text::keep(Text this) { return move this; } i32 Text::get(const Text* this) { return "
         "this->v; } i32 f() { return Text::make().keep().get(); }",
         "R-DIAG-BORROW-002 [R-BORROW-0020]"},
        /* L19-1: an argument-free attribute no longer takes a following tuple type. */
        {"tuple-result-after-attribute",
         "struct Grid { i32 a; }; @discardable (i32, bool) Grid::first(const Grid* this) { "
         "return (this->a, true); } @must_use (i32, bool) second(i32 a) { return (a, true); } "
         "i32 f() { Grid g = {.a = 1}; (i32, bool) p = g.first(); return p.0 + second(2).0; }",
         NULL},
        {"attribute-arguments-still-rejected",
         "@nonblocking(1) void f() {}",
         "R-DIAG-SYN-002 [R-GRAM-0007]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l19_regressions; %s", cases[i].source);
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
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l19 regression %s status %d\n", cases[i].name, (int)status);
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

#undef R_L19_OPTIONS
