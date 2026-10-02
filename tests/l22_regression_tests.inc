/* L22 (docs/language-completeness-roadmap.ru.md): translation-time evaluation of checked errors,
 * error families and floating values. Each case is accepted, or is rejected with the diagnostic
 * code and, where given, the rule it names; no case may leave a construct outside the
 * implemented slice (R-DIAG-SLICE-001). */
#define R_L22_PORT                                                                                 \
    "error bad_port { u32 digit; }; error port_kind { empty, spaces }; "                           \
    "u16 parse_port(str text) throws bad_port, port_kind { "                                       \
    "throw (len(text) == 0usize) port_kind::empty; u32 value = 0u32; "                             \
    "for (usize index = 0usize; index < len(text); index += 1usize) { u8 byte = text[index]; "     \
    "throw (byte < 48u8 || byte > 57u8) bad_port {.digit = index as u32}; "                        \
    "value = value * 10u32 + ((byte - 48u8) as u32); } return value as u16; } "
#define R_L22_FAMILY                                                                               \
    "error config_error { u32 line; }; error syntax_error : config_error { u32 column; }; "        \
    "u32 check(u32 code) throws config_error { throw (code == 1u32) syntax_error {.line = 3u32, "  \
    ".column = 7u32}; throw (code == 2u32) config_error {.line = 9u32}; return code; } "

static void r_semantic_test_l22_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L22.1, R-FUNC-0023, R-EXPR-0032: a function with checked errors of constant value
           types is evaluated; a required constant does not catch them. */
        {"throwing-module-constant", R_L22_PORT "const u16 PORT = parse_port(\"8080\");", NULL},
        {"throw-escapes-module-constant",
         R_L22_PORT "const u16 PORT = parse_port(\"80a\");",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"fieldless-throw-escapes",
         R_L22_PORT "const u16 PORT = parse_port(\"\");",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"throw-escapes-array-bound",
         R_L22_PORT "struct Frame { u8[parse_port(\"1x\")] bytes; };",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"throw-escapes-module-object",
         R_L22_PORT "const u16[2] PORTS = {parse_port(\"80\"), parse_port(\"4x3\")};",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"throw-escapes-static-local",
         R_L22_PORT "u16 f() { static const u16 cached = parse_port(\"8y\"); return cached; }",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"throwing-static-local",
         R_L22_PORT "u16 f() { static const u16 cached = "
                    "parse_port(\"81\"); return cached; }",
         NULL},
        {"throwing-case-label",
         R_L22_PORT "i32 f(u16 port) { switch (port) { case parse_port(\"80\"): return 1; "
                    "default: return 0; } }",
         NULL},
        {"throw-escapes-case-label",
         R_L22_PORT "i32 f(u16 port) { switch (port) { case parse_port(\"8o\"): return 1; "
                    "default: return 0; } }",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"throwing-static-condition",
         R_L22_PORT "i32 f() { @if (parse_port(\"7\") == 7u16) { return 1; } @else { return 0; } }",
         NULL},
        {"throw-escapes-static-condition",
         R_L22_PORT "i32 f() { @if (parse_port(\"q\") == 7u16) { return 1; } @else { return 0; } }",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"body-call-still-checked",
         R_L22_PORT "u16 f() { return parse_port(\"80\"); }",
         "R-DIAG-EFFECT-001 [R-ERR-0001]"},
        {"local-const-still-checked",
         R_L22_PORT "u16 f() { const u16 port = parse_port(\"80\"); return port; }",
         "R-DIAG-EFFECT-001 [R-ERR-0001]"},
        {"catch-at-translation",
         R_L22_PORT "u32 port_or(str text, u32 fallback) { try { return parse_port(text) as u32; } "
                    "catch (bad_port failure) { return fallback + failure.digit; } catch "
                    "(port_kind kind) { return 1u32; } } const u32 PORT = port_or(\"8x\", 90u32);",
         NULL},
        {"finally-at-translation",
         R_L22_PORT "u32 count(str text) { u32 total = 0u32; try { try { total += "
                    "parse_port(text) as u32; } finally { total += 100u32; } } catch (bad_port "
                    "failure) { total += 1u32; } catch (port_kind kind) { total += 2u32; } return "
                    "total; } const u32 A = count(\"12\"); const u32 B = count(\"1z\");",
         NULL},
        {"rethrow-at-translation",
         R_L22_PORT "error wrapped { u32 at; }; u16 wrap(str text) throws wrapped { try { return "
                    "parse_port(text); } catch (bad_port failure) { throw wrapped {.at = "
                    "failure.digit}; } catch (port_kind kind) { return 0u16; } } const u16 PORT "
                    "= wrap(\"x\");",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"error-outside-constant-types",
         "error native_error { raw i32* handle; }; u16 f(u16 x) throws native_error { return "
         "x; } const u16 X = f(1u16);",
         "R-DIAG-CONST-002 [R-FUNC-0023]"},
        /* L22.1, R-AGG-0011: an error family is a value of translation-time evaluation. */
        {"family-catch-at-translation",
         R_L22_FAMILY "u32 line_of(u32 code) { try { return check(code); } catch (config_error "
                      "failure) { return failure.line; } } const u32 LINE = line_of(1u32);",
         NULL},
        {"family-nearest-at-translation",
         R_L22_FAMILY "u32 pick(u32 code) { try { return check(code); } catch (config_error "
                      "failure) { return 1u32; } catch (syntax_error failure) { return "
                      "failure.column; } } const u32 PICKED = pick(1u32);",
         NULL},
        {"family-rethrow-escapes",
         R_L22_FAMILY "u32 forward(u32 code) throws config_error { try { return check(code); } "
                      "catch (config_error failure) { throw failure; } } const u32 X = "
                      "forward(1u32);",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"family-module-object",
         R_L22_FAMILY "config_error make(u32 line) { config_error made = syntax_error {.line = "
                      "line, .column = 1u32}; return made; } const config_error SAMPLE = "
                      "make(4u32);",
         NULL},
        {"tagged-module-object",
         "enum shape { circle(u32), square { u32 side; }, dot }; const shape S = "
         "shape::circle(2u32); const shape Q = shape::square {.side = 3u32}; const shape D = "
         "shape::dot;",
         NULL},
        /* L22.3: tagged values and standard owners as working values. */
        {"tagged-switch-constant",
         "enum shape { circle(u32), dot }; u32 size(shape value) { switch (value) { case variant "
         "shape::circle(radius): return *radius; case variant shape::dot: return 0u32; } } const "
         "u32 SIZE = size(shape::circle(4u32));",
         NULL},
        {"option-module-object",
         "o<u32> first(u32 x) { o<u32> found = o::some(x); return found; } const o<u32> FIRST = "
         "first(3u32); const o<u32> NONE = o::none;",
         NULL},
        {"owner-draft-constant",
         "u32 count(u32 limit) { try { array<u32> found = std.array::create::<u32>(); for (u32 i "
         "= 0u32; i < limit; i += 1u32) { found.push(i); } return len(found) as u32; } catch "
         "(std.array::push_error<u32> failure) { return 0u32; } } const u32 COUNT = count(5u32);",
         NULL},
        {"owner-capacity-runtime",
         "usize room() { array<u32> found = std.array::create::<u32>(); return found.capacity(); "
         "} const usize ROOM = room();",
         "R-DIAG-CONST-002 [R-FUNC-0023]"},
        {"string-boundary-at-translation",
         "usize cut() throws std.alloc::alloc_error, std.string::boundary_error { "
         "std.string::string text = std.string::from_str(\"a\"); std.string::truncate(&text, "
         "4usize); return std.string::len(&text); } const usize CUT = cut();",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        /* L22.4: owners frozen into the program image. */
        {"frozen-array-const",
         "array<u32> make() throws std.alloc::alloc_error, std.array::push_error<u32> { "
         "array<u32> made = std.array::create::<u32>(); made.push(7u32); return move made; } "
         "const array<u32> MADE = make(); u32 first() { return MADE[0usize]; }",
         NULL},
        {"frozen-owner-not-const",
         "array<u32> make() throws std.alloc::alloc_error, std.array::push_error<u32> { "
         "array<u32> made = std.array::create::<u32>(); made.push(7u32); return move made; } "
         "array<u32> MADE = make();",
         "R-DIAG-INIT-001 [R-OBJ-0010]"},
        {"frozen-owner-thread-local",
         "array<u32> make() throws std.alloc::alloc_error, std.array::push_error<u32> { "
         "array<u32> made = std.array::create::<u32>(); made.push(7u32); return move made; } "
         "thread_local const array<u32> MADE = make();",
         "R-DIAG-INIT-001 [R-OBJ-0010]"},
        {"frozen-dict-float-key",
         "dict<f64, u8> make() throws std.alloc::alloc_error, std.dict::insert_error<f64, u8> { "
         "dict<f64, u8> made = std.dict::create::<f64, u8>(); o<u8> old = made.insert(1.5, "
         "2u8); old as void; return move made; } const dict<f64, u8> MADE = make();",
         "R-DIAG-INIT-001 [R-OBJ-0010]"},
        /* L22.2, R-TYPE-0006: binary32 and binary64 values and exact std.math operations. */
        {"float-module-constant",
         "f64 scale(f64 x) { return x * 2.5 + 1.0; } const f64 SCALED = scale(2.0); const f32 "
         "NARROW = (scale(0.5) as f32);",
         NULL},
        {"float-array-bound",
         "f64 scale(f64 x) { return x * 2.5 + 1.0; } struct Frame { u8[(scale(2.0) as usize)] "
         "bytes; };",
         NULL},
        {"float-exact-math",
         "f64 clamp(f64 x) { return std.math::min_f64(std.math::max_f64(x, 0.0), 1.0); } const "
         "f64 A = clamp(1.7); const f64 B = std.math::floor_f64(-2.5);",
         NULL},
        {"float-conversion-panics",
         "i32 whole(f64 x) { return x as i32; } const i32 BIG = whole(1e300);",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"float-nan-conversion-panics",
         "i32 whole(f64 x) { return x as i32; } f64 nan() { return 0.0 / 0.0; } const i32 N = "
         "whole(nan());",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"float-sqrt-domain-throws",
         "const f64 ROOT = std.math::sqrt_f64(-1.0);",
         "R-DIAG-CONST-003 [R-EXPR-0032]"},
        {"float-remainder-constant",
         "const f64 REMAINDER = std.math::remainder_f64(7.5, 2.0); const f32 ROOT = "
         "std.math::sqrt_f32(9.0f32);",
         NULL},
        {"float-transcendental-runtime",
         "f64 wave(f64 x) { return std.math::sin_f64(x); } const f64 W = wave(1.0);",
         "R-DIAG-CONST-002 [R-FUNC-0023]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l22_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l22 regression %s status %d\n", cases[i].name, (int)status);
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

#undef R_L22_FAMILY
#undef R_L22_PORT
