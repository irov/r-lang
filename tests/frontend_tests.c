#include "r_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RTestBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
} RTestBuffer;

typedef struct RTestAllocator {
    size_t call_count;
    size_t fail_at;
    size_t live_count;
} RTestAllocator;

static int failures = 0;

#define R_TEST_CHECK(condition)                                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_test_write(void *user_data, const char *bytes, size_t length) {
    RTestBuffer *buffer = user_data;
    size_t required = buffer->length + length + 1U;
    if (required < buffer->length) {
        return false;
    }
    if (required > buffer->capacity) {
        size_t capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
        char *replacement;
        while (capacity < required) {
            if (capacity > (SIZE_MAX / 2U)) {
                return false;
            }
            capacity *= 2U;
        }
        replacement = realloc(buffer->bytes, capacity);
        if (replacement == NULL) {
            return false;
        }
        buffer->bytes = replacement;
        buffer->capacity = capacity;
    }
    if (length != 0U) {
        (void)memcpy(buffer->bytes + buffer->length, bytes, length);
    }
    buffer->length += length;
    buffer->bytes[buffer->length] = '\0';
    return true;
}

static RSourceId r_test_add_source(RFrontendContext *context,
                                   const char *name,
                                   const uint8_t *bytes,
                                   size_t length) {
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RFrontendStatus status = r_frontend_add_source(context, name, bytes, length, &source_id);
    R_TEST_CHECK(status == R_FRONTEND_OK);
    return source_id;
}

static bool r_test_has_diagnostic(const RFrontendContext *context, const char *code) {
    size_t index;
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0)) {
            return true;
        }
    }
    return false;
}

static bool
r_test_has_diagnostic_rule(const RFrontendContext *context, const char *code, const char *rule_id) {
    size_t index;
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0) &&
            (strcmp(diagnostic->rule_id, rule_id) == 0)) {
            return true;
        }
    }
    return false;
}

static void r_test_valid_frontend(void) {
    static const char source_text[] =
        "module demo.main;\n"
        "error Error { bad, };\n"
        "struct Node { i32 value; };\n"
        "extern \"C\" { c_int puts(raw const c_char*? text); }\n"
        "protected i32 choose(const Node* node) {\n"
        "  if ((node->value & 1) == 0) { return 0; } else { return 1; }\n"
        "}\n"
        "protected i32 r(i32 value) { return value; }\n"
        "protected i32 result(i32 value) throws Error {\n"
        "  if (value < 0) { throw Error::bad; }\n"
        "  return value;\n"
        "}\n"
        "protected async i32 waiting(task<i32 throws Error> work) throws Error {\n"
        "  try {\n"
        "    i32 value = await move work;\n"
        "    switch (value) {\n"
        "      case 0: value += 1; fallthrough;\n"
        "      case 1: throw Error::bad;\n"
        "      default: return value;\n"
        "    }\n"
        "  } catch (Error error) {\n"
        "    error as void;\n"
        "    throw;\n"
        "  } finally { ; }\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer cst_dump = {0};
    RTestBuffer second_cst_dump = {0};
    RTestBuffer ast_dump = {0};
    RTestBuffer reconstructed = {0};
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id =
        r_test_add_source(context, "valid.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(cst != R_SYNTAX_NODE_ID_INVALID);
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(ast != R_AST_NODE_ID_INVALID);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_cst(context, source_id, r_test_write, &cst_dump) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_cst(context, source_id, r_test_write, &second_cst_dump) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_ast(context, source_id, r_test_write, &ast_dump) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source_id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "extern_block") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "await_operation") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "throws_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "try_statement") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "catch_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "finally_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "throw_statement") != NULL));
    R_TEST_CHECK((ast_dump.bytes != NULL) && (strstr(ast_dump.bytes, "fallthrough") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (second_cst_dump.bytes != NULL) &&
                 (cst_dump.length == second_cst_dump.length) &&
                 (memcmp(cst_dump.bytes, second_cst_dump.bytes, cst_dump.length) == 0));
    R_TEST_CHECK((reconstructed.bytes != NULL) && (reconstructed.length == strlen(source_text)) &&
                 (memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0));
    free(cst_dump.bytes);
    free(second_cst_dump.bytes);
    free(ast_dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_restricted_syntax(void) {
    static const char source_text[] = "module invalid;\n"
                                      "protected i32 produce() { return 1; }\n"
                                      "protected i32 consume(i32 value) { return value; }\n"
                                      "protected i32 broken(i32 value) {\n"
                                      "  if (value) { return 0; }\n"
                                      "  if (value == 1) return 1;\n"
                                      "  consume(produce());\n"
                                      "  return produce();\n"
                                      "  i32 tried = try produce();\n"
                                      "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer reconstructed = {0};
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id =
        r_test_add_source(context, "invalid.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(cst != R_SYNTAX_NODE_ID_INVALID);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source_id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK((reconstructed.length == strlen(source_text)) &&
                 (memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
    R_TEST_CHECK(ast == R_AST_NODE_ID_INVALID);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void
r_test_source_diagnostic(const char *name, const char *source_text, const char *expected_code) {
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_test_add_source(context, name, (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(cst != R_SYNTAX_NODE_ID_INVALID);
    R_TEST_CHECK(r_test_has_diagnostic(context, expected_code));
    r_frontend_destroy(context);
}

static void r_test_async_ffi_and_attribute_diagnostics(void) {
    static const char invalid_await[] = "module invalid.await_case;\n"
                                        "protected i32 wrong(task<i32> operation) {\n"
                                        "  i32 result = await move operation;\n"
                                        "  return result;\n"
                                        "}\n";
    static const char invalid_ffi[] = "module invalid.ffi;\n"
                                      "extern \"other\" { c_int foreign_value; }\n";
    /* An unknown name is no longer a syntax error: it may name an attribute type (R-AGG-0013),
       which the semantic pass resolves. A malformed argument still is. */
    static const char invalid_attribute[] = "module invalid.attribute;\n"
                                            "@repr(-) protected i32 unknown() { return 0; }\n";
    static const char invalid_standard_return[] =
        "module invalid.standard_return;\n"
        "protected array<u8> wrong() { return std.array::create::()(); }\n";
    static const char invalid_standard_name[] = "module invalid.standard_name;\n"
                                                "protected i32 wrong(i32 value) {\n"
                                                "  std.array::unknown(&value);\n"
                                                "  return value;\n"
                                                "}\n";
    static const char invalid_types[] =
        "module invalid.types;\n"
        "struct Item { i32 value; };\n"
        "struct Bad { array<u8>[4] nested; atomic f32 number; const arc Item shared; };\n"
        "protected i32 wrong(void value) { return 0; }\n";
    static const char invalid_nested_atomic[] = "module invalid.nested_atomic;\n"
                                                "protected atomic ai8 wrong_fixed;\n"
                                                "protected atomic aisize wrong_size;\n";
    static const char invalid_switch[] = "module invalid.switch_case;\n"
                                         "protected i32 wrong(i32 value) {\n"
                                         "  switch (value) { value += 1; case 1: break; }\n"
                                         "  return value;\n"
                                         "}\n";
    static const char invalid_legacy_result[] =
        "module invalid.legacy_result;\n"
        "error Error { i32 code; };\n"
        "protected r(i32, Error) old_result(i32 value) { return r::ok(value); }\n";
    static const char invalid_legacy_prefix_try[] =
        "module invalid.legacy_prefix_try;\n"
        "error Error { i32 code; };\n"
        "protected i32 operation() throws Error { return 1; }\n"
        "protected i32 caller() throws Error { i32 value = try operation(); return value; }\n";
    r_test_source_diagnostic("invalid-await.r", invalid_await, "R-DIAG-ASYNC-001");
    r_test_source_diagnostic("invalid-ffi.r", invalid_ffi, "R-DIAG-SYN-001");
    r_test_source_diagnostic("invalid-attribute.r", invalid_attribute, "R-DIAG-SYN-002");
    r_test_source_diagnostic(
        "invalid-standard-return.r", invalid_standard_return, "R-DIAG-SYN-001");
    r_test_source_diagnostic("invalid-standard-name.r", invalid_standard_name, "R-DIAG-SYN-001");
    r_test_source_diagnostic("invalid-types.r", invalid_types, "R-DIAG-SYN-001");
    r_test_source_diagnostic("invalid-nested-atomic.r", invalid_nested_atomic, "R-DIAG-SYN-001");
    r_test_source_diagnostic("invalid-switch.r", invalid_switch, "R-DIAG-SYN-001");
    {
        RFrontendContext *context = r_frontend_create(NULL);
        R_TEST_CHECK(context != NULL);
        if (context != NULL) {
            (void)r_test_add_source(context,
                                    "invalid-legacy-result.r",
                                    (const uint8_t *)invalid_legacy_result,
                                    strlen(invalid_legacy_result));
            R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
            r_frontend_destroy(context);
        }
    }
    r_test_source_diagnostic(
        "invalid-legacy-prefix-try.r", invalid_legacy_prefix_try, "R-DIAG-SYN-001");
}

static void r_test_raw_function_type_syntax(void) {
    static const char source_text[] =
        "module syntax.raw_function;\n"
        "extern \"C\" { c_int increment(c_int value); }\n"
        "struct Callbacks {\n"
        "    raw /* callback type */ fn(c_int) -> c_int first;\n"
        "    raw fn /* nullable */ ? (c_int) -> c_int second;\n"
        "    raw fn(raw fn?(c_int) -> c_int) -> raw fn(c_int) -> c_int nested;\n"
        "    (raw fn(c_int) -> c_int)[2] entries;\n"
        "    raw (raw fn(c_int) -> c_int)* pointer;\n"
        "};\n"
        "raw fn(c_int) -> c_int identity(raw fn(c_int) -> c_int callback) {\n"
        "    return callback;\n"
        "}\n"
        "raw fn() -> raw fn(c_int) -> c_int factory(\n"
        "    raw fn() -> raw fn(c_int) -> c_int value) { return value; }\n"
        "c_int invoke(raw fn(c_int) -> c_int callback, Callbacks* table, c_int value) {\n"
        "    unsafe {\n"
        "        c_int first = callback(value);\n"
        "        c_int second = (callback)(first);\n"
        "        c_int third = table->first(second);\n"
        "        raw fn(c_int) -> c_int selected = identity(callback);\n"
        "        c_int fourth = selected(third);\n"
        "        return fourth;\n"
        "    }\n"
        "}\n";
    static const char *const invalid_fields[] = {
        "raw fn(void) -> void callback;",
        "raw fn?(c_int,) -> c_int callback;",
        "raw fn(c_int) c_int callback;",
        "raw fn(c_int) -> ;",
        "raw fn(c_int) -> c_int? callback;",
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer reconstructed = {0};
    RTestBuffer ast_dump = {0};
    RSourceId source;
    size_t calls = 0U;
    const char *suffix;

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(
        context, "raw-function.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &ast_dump) == R_FRONTEND_OK);
    suffix = ast_dump.bytes;
    while ((suffix != NULL) && ((suffix = strstr(suffix, "call_suffix")) != NULL)) {
        calls += 1U;
        suffix += strlen("call_suffix");
    }
    R_TEST_CHECK(calls == 5U);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(source_text) &&
                 memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0);
    free(ast_dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);

    for (size_t index = 0U; index < sizeof(invalid_fields) / sizeof(invalid_fields[0]); ++index) {
        char text[256];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.raw_invalid; struct Callbacks { %s }; "
                              "i32 after() { return 0; }",
                              invalid_fields[index]);
        context = r_frontend_create(NULL);
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        reconstructed = (RTestBuffer){0};
        source = r_test_add_source(
            context, "raw-function-invalid.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_type_statement_and_expression_inventory(void) {
    static const char source_text[] =
        "module grammar.inventory;\n"
        "error Error : u32 { bad = 1, payload(i32), fields { i32 code; }, };\n"
        "@repr(C) struct Item {\n"
        "  const i32 signed_value;\n"
        "  u8[4][2] bytes;\n"
        "  const u8[] view;\n"
        "  o<i32> optional;\n"
        "  array<u8> dynamic;\n"
        "  list<i32> linked;\n"
        "  dict<u32, i32> table;\n"
        "  task<i32 throws Error> pending;\n"
        "  arc Item shared;\n"
        "  rc Item local;\n"
        "  weak arc Item weak_shared;\n"
        "  own Item*? owned;\n"
        "  own array<u8>* owned_buffer;\n"
        "  raw const Item*? raw_item;\n"
        "  raw dict<u32, i32>*? raw_table;\n"
        "  atomic i32 count;\n"
        "  atomic raw const Item*? atomic_item;\n"
        "  ai8 fast_i8;\n"
        "  ai16 fast_i16;\n"
        "  ai32 fast_i32;\n"
        "  ai64 fast_i64;\n"
        "  aisize fast_isize;\n"
        "  au8 fast_u8;\n"
        "  au16 fast_u16;\n"
        "  au32 fast_u32;\n"
        "  au64 fast_u64;\n"
        "  ausize fast_size;\n"
        "  raw fn?(i32, const Item*) -> i32 callback;\n"
        "  constexpr str message;\n"
        "  std.sync::mutex<i32> mutex;\n"
        "};\n"
        "@header(\"stdio.h\") extern \"\\x43\" {\n"
        "  opaque struct File;\n"
        "  thread_local c_int c_errno;\n"
        "  c_int c_constant = 1;\n"
        "  unsafe c_int c_printf(raw const c_char* format, ...);\n"
        "}\n"
        "constexpr str joined = \"left\" \"right\";\n"
        "protected i32 exercise(Item* item, i32 value) throws Error {\n"
        "  static i32 counter = 0;\n"
        "  thread_local i32 local_counter = 0;\n"
        "  array<u8> data = std.array::with_capacity::<u8>(4);\n"
        "  list<i32> values = std.list::create::<i32>();\n"
        "  dict<u32, i32> table = std.dict::create::<u32, i32>();\n"
        "  own Item* allocated = new Item {};\n"
        "  i32 selected = (value == 0) ? 1 : 2;\n"
        "  char letter = '\\u{52}';\n"
        "  for (i32 index = 0; index < 4; index += 1) {\n"
        "    value += index;\n"
        "  }\n"
        "  while (value > 0) { value -= 1; }\n"
        "  thread_scope { value += 1; }\n"
        "  unsafe { raw Item*? pointer = null; pointer as void; }\n"
        "  switch (selected) {\n"
        "    case 0: value += 1; fallthrough;\n"
        "    case variant Error::bad: throw Error::bad;\n"
        "    default: value += 2; continue;\n"
        "  }\n"
        "  item->signed_value += value;\n"
        "  drop data;\n"
        "  allocated as void;\n"
        "  values as void;\n"
        "  table as void;\n"
        "  counter as void;\n"
        "  local_counter as void;\n"
        "  letter as void;\n"
        "  return value;\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_test_add_source(
        context, "inventory.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(ast != R_AST_NODE_ID_INVALID);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    r_frontend_destroy(context);
}

static void r_test_null_type_syntax(void) {
    static const char source_text[] =
        "module null_value.syntax;\n"
        "@generic<T: copy> struct Box { T value; };\n"

        "i32 choose(const /* marker */ null_t absent) { return 1; }\n"
        "i32 choose(i32 value) { return value; }\n"
        "async i32 later(null_t absent) { return 1; }\n"
        "i32 main() { i32 value = choose((null)); return value - 1; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer dump = {0};
    RTestBuffer reconstructed = {0};
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    RSourceId source = r_test_add_source(
        context, "null_type.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "null_t") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(source_text) &&
                 memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_checked_error_syntax(void) {
    static const char source_text[] =
        "module checked.syntax;\n"
        "error FirstError { i32 code; };\n"
        "error SecondError { i32 code; };\n"
        "protected i32 checked(i32 value) throws FirstError, SecondError {\n"
        "  try {\n"
        "    if (value == 0) { throw { .code = value }; }\n"
        "    throw SecondError { .code = value };\n"
        "  } catch (FirstError first) {\n"
        "    first as void;\n"
        "    throw;\n"
        "  } catch (SecondError second) {\n"
        "    switch (value) { default: throw; }\n"
        "  } finally {\n"
        "    value += 1;\n"
        "  }\n"
        "}\n"
        "protected async i32 checked_async(\n"
        "    task<i32 throws FirstError, SecondError> operation)\n"
        "    throws FirstError, SecondError {\n"
        "  try {\n"
        "    i32 value = await move operation;\n"
        "    return value;\n"
        "  } finally { ; }\n"
        "}\n"
        "protected void observe_thread(\n"
        "    std.thread::join_handle<i32 throws FirstError, SecondError> handle);\n"
        "protected void observe_scoped_thread(\n"
        "    std.thread::scoped_join_handle<i32 throws SecondError> handle);\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer cst_dump = {0};
    RTestBuffer ast_dump = {0};

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_test_add_source(
        context, "checked-syntax.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(ast != R_AST_NODE_ID_INVALID);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_cst(context, source_id, r_test_write, &cst_dump) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_ast(context, source_id, r_test_write, &ast_dump) == R_FRONTEND_OK);
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "throws_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "try_statement") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "catch_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "finally_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "throw_statement") != NULL));
    R_TEST_CHECK((ast_dump.bytes != NULL) && (strstr(ast_dump.bytes, "(token throws") != NULL));
    R_TEST_CHECK((ast_dump.bytes != NULL) && (strstr(ast_dump.bytes, "(token catch") != NULL));
    R_TEST_CHECK((ast_dump.bytes != NULL) && (strstr(ast_dump.bytes, "(token finally") != NULL));
    R_TEST_CHECK((ast_dump.bytes != NULL) && (strstr(ast_dump.bytes, "(token throw") != NULL));
    free(cst_dump.bytes);
    free(ast_dump.bytes);
    r_frontend_destroy(context);
}

static void r_test_error_declarations(void) {
    static const char source[] =
        "module errors.syntax;\n"
        "error Empty {};\n"
        "@repr(C) protected error Record { protected Unknown value; };\n"
        "error Before { Later value; }; error Later { i32 code; };\n"
        "error Parametric { array<u8> data; };\n"
        "error Borrow { const u8[] data; };\n"
        "error Choice { One, Two = 7, Value(i32), Fields { i32 code; }, };\n"
        "error Number : u8 { A = 1, B = 255, };\n"
        "error /* name */ Comments /* body */ { /* member */ A /* comma */, };\n"
        "struct error { i32 value; }; error identity(error error) { return error; }\n"
        "error named = {.value=1}; void consume(std.error::error error);\n";
    static const char *const invalid[] = {
        "error E { i32 code; Variant, };",
        "error E { Variant, i32 code; };",
        "error E { Variant(i32), i32 code; };",
        "error E { Variant { i32 code; }, i32 code; };",
        "error E : u8 {};",
        "error E : u8 { i32 field; };",
        "error struct E { i32 code; };",
        "error enum E { A, };",
        "error E { i32 code };",
        "error E { A(i32 };",
        "error E { A { i32 code }; };",
        "error E { A = , };",
        "error E { A { , }; };",
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer dump = {0};
    RTestBuffer reconstructed = {0};
    size_t index;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    RSourceId id = r_test_add_source(context, "errors.r", (const uint8_t *)source, strlen(source));
    R_TEST_CHECK(r_frontend_lower_ast(context, id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, id, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "error_struct_declaration") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "error_enum_declaration") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(source) &&
                 memcmp(reconstructed.bytes, source, strlen(source)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        char broken[512];
        (void)snprintf(broken, sizeof(broken), "module broken; %s error After {};", invalid[index]);
        context = r_frontend_create(NULL);
        R_TEST_CHECK(context != NULL);
        if (context == NULL) {
            return;
        }
        id = r_test_add_source(context, "broken.r", (const uint8_t *)broken, strlen(broken));
        R_TEST_CHECK(r_frontend_lower_ast(context, id, &ast) == R_FRONTEND_NOT_LOWERABLE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        reconstructed = (RTestBuffer){0};
        R_TEST_CHECK(r_frontend_reconstruct_source(context, id, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == strlen(broken) &&
                     memcmp(reconstructed.bytes, broken, strlen(broken)) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_conditional_expression_syntax(void) {
    static const char source_text[] =
        "module ternary.syntax;\n"
        "error Error { i32 code; };\n"
        "void run(bool flag, i32 a, i32 b) {\n"
        "    i32 one = (a == b) ? 1 : 2;\n"
        "    i32 two = a > 0 && b < 10 ? a + 1 : b * 2;\n"
        "    i32 three = a == 0 ? 1 : b == 0 ? 2 : 3;\n"
        "    i32 four = (a == 0 ? true : false) == true ? 4 : 5;\n"
        "    a = flag == true ? b : 0;\n"
        "    i32 commented = (a /* condition */ != b) ? /* yes */ 1 : /* no */ 0;\n"
        "    throw Error { .code = flag == true ? call() : call() };\n"
        "}\n";
    static const char *const invalid[] = {
        "i32 v = flag ? 1 : 2;",
        "i32 v = 12 ? 1 : 2;",
        "i32 v = true ? : 2;",
        "i32 v = true ? 1 :;",
        "i32 v = true ? 1 2;",
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer dump = {0};
    RTestBuffer reconstructed = {0};
    size_t index;

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    RSourceId source_id = r_test_add_source(
        context, "conditional.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source_id, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "conditional_expression") != NULL));
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source_id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK((reconstructed.length == strlen(source_text)) &&
                 (memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0));
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        char source[512];
        (void)snprintf(source, sizeof(source), "module negative; void f() { %s }", invalid[index]);
        context = r_frontend_create(NULL);
        R_TEST_CHECK(context != NULL);
        if (context == NULL) {
            return;
        }
        source_id =
            r_test_add_source(context, "negative.r", (const uint8_t *)source, strlen(source));
        R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        r_frontend_destroy(context);
    }
}

static void r_test_conditional_throw_syntax(void) {
    static const char source_text[] =
        "module checked.conditional;\n"
        "error Error { i32 code; };\n"
        "void test(i32 n, Error error, Error* pointer) throws Error {\n"
        "  throw (n != 0) { .code = n, };\n"
        "  throw (((n) != 0) && (true || n < 2)) Error { .code = n };\n"
        "  throw (n > 0) error;\n"
        "  throw (n > 0) error else (error);\n"
        "  throw (((n) != 0) && (true || n < 2)) Error { .code = n } else {.code = 4};\n"
        "  throw (true) { .code = 1 } else { .code = 2 };\n"
        "  throw (true) move error else move error;\n"
        "  throw (true) (error);\n"
        "  throw (true) *pointer;\n"
        "  throw (true) move error;\n"
        "  throw (false) Error::bad;\n"
        "  throw (error);\n"
        "  throw ((error));\n"
        "  throw (pointer)[0];\n"
        "  throw (error).code;\n"
        "  throw (error) as Error;\n"
        "  throw (n) + 1 as Error;\n"
        "  throw (n) * 2 as Error;\n"
        "  throw (n)++;\n"
        "  switch (n) { case 0: throw (n == 1) error; n as void; break;\n"
        "    default: throw error; }\n"
        "  switch (n) { case 0: throw (n == 1) error else Error {.code = 2};\n"
        "    default: throw (true) {.code = 3} else {.code = 4}; }\n"
        "}\n";
    static const char *const invalid[] = {
        "throw (true) call( ;",
        "throw (123) error;",
        "throw (flag) error;",
        "throw n == 0 error;",
        "throw (true) error else;",
        "throw (true) else error;",
        "throw error else error;",
        "throw (true) error; else error;",
        "throw (true) error else throw error;",
        "switch (n) { default: throw (true) error else error; break; }",
        "throw (true { .code = 1 };",
        /* L20.1: a clause ending in a conditional throw ends with an implicit break. */
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer dump = {0};
    RTestBuffer reconstructed = {0};
    size_t index;

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    RSourceId source_id = r_test_add_source(
        context, "conditional.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source_id, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "throw_condition") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "throw_else") != NULL));
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source_id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK((reconstructed.length == strlen(source_text)) &&
                 (memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0));
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        char source[512];
        (void)snprintf(source, sizeof(source), "module negative; void f() { %s }", invalid[index]);
        context = r_frontend_create(NULL);
        R_TEST_CHECK(context != NULL);
        if (context == NULL) {
            return;
        }
        source_id =
            r_test_add_source(context, "negative.r", (const uint8_t *)source, strlen(source));
        R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        r_frontend_destroy(context);
    }
}

static void r_test_checked_error_recovery(void) {
    static const char source_text[] = "module checked.recovery;\n"
                                      "error Error { i32 code; };\n"
                                      "protected i32 malformed() throws {\n"
                                      "  try { throw build_error(); }\n"
                                      "  throw Error { .code = build_code() };\n"
                                      "  return 0;\n"
                                      "}\n"
                                      "protected void malformed_handle(\n"
                                      "    std.thread::join_handle<i32 throws> handle);\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer cst_dump = {0};
    RTestBuffer reconstructed = {0};

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_test_add_source(
        context, "checked-recovery.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(cst != R_SYNTAX_NODE_ID_INVALID);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
    R_TEST_CHECK(r_frontend_dump_cst(context, source_id, r_test_write, &cst_dump) == R_FRONTEND_OK);
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "throws_clause") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "throw_statement") != NULL));
    R_TEST_CHECK((cst_dump.bytes != NULL) &&
                 (strstr(cst_dump.bytes, "aggregate_initializer") != NULL));
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source_id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK((reconstructed.length == strlen(source_text)) &&
                 (memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
    R_TEST_CHECK(ast == R_AST_NODE_ID_INVALID);
    free(cst_dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_checked_error_clause_order_recovery(void) {
    static const char source_text[] =
        "module checked.clause_order;\n"
        "error Error { i32 code; };\n"
        "protected void reordered() {\n"
        "  try { ; } finally { ; } catch (Error error) { error as void; }\n"
        "}\n"
        "protected void duplicated() {\n"
        "  try { ; } finally { ; } finally { ; }\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RTestBuffer cst_dump = {0};
    RTestBuffer reconstructed = {0};
    const char *first_finally;

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_test_add_source(
        context, "checked-clause-order.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(cst != R_SYNTAX_NODE_ID_INVALID);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
    R_TEST_CHECK(r_frontend_dump_cst(context, source_id, r_test_write, &cst_dump) == R_FRONTEND_OK);
    R_TEST_CHECK((cst_dump.bytes != NULL) && (strstr(cst_dump.bytes, "catch_clause") != NULL));
    first_finally = cst_dump.bytes == NULL ? NULL : strstr(cst_dump.bytes, "finally_clause");
    R_TEST_CHECK((first_finally != NULL) && (strstr(first_finally + 1, "finally_clause") != NULL));
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source_id, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK((reconstructed.length == strlen(source_text)) &&
                 (memcmp(reconstructed.bytes, source_text, reconstructed.length) == 0));
    R_TEST_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
    R_TEST_CHECK(ast == R_AST_NODE_ID_INVALID);
    free(cst_dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_lexer_boundaries(void) {
    static const char source_text[] =
        "0 0xff 0b10 0o7 1_000 1.0 1e2 0x1.fp2 1..2 <<= >>= ... // end\r\n"
        "/* block */ ai8 ai16 ai32 ai64 aisize au8 au16 au32 au64 ausize "
        "catch finally throw throws try r impl Self this trait auto in";
    static const char *const atomic_keyword_tokens[] = {
        "(token ai8 ",
        "(token ai16 ",
        "(token ai32 ",
        "(token ai64 ",
        "(token aisize ",
        "(token au8 ",
        "(token au16 ",
        "(token au32 ",
        "(token au64 ",
        "(token ausize ",
        "(token impl ",
        "(token Self ",
        "(token this ",
        "(token trait ",
        "(token auto ",
        "(token in ",
    };
    static const uint8_t embedded_bom[] = {'a', ' ', 0xEFU, 0xBBU, 0xBFU, ' ', 'b'};
    static const uint8_t malformed_comment[] = {'/', '/', ' ', 0xFFU, '\n'};
    static const uint8_t unicode_whitespace[] = {'a', 0xC2U, 0xA0U, 'b'};
    static const char invalid_character_hex[] = "'\\x80'";
    static const char invalid_character_split_utf8[] = "'\\xC3\\xA9'";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSourceId bom_id;
    RSourceId comment_id;
    RSourceId whitespace_id;
    RSourceId invalid_character_hex_id;
    RSourceId invalid_character_split_utf8_id;
    RTestBuffer dump = {0};
    size_t keyword_index;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id =
        r_test_add_source(context, "numbers.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_lex(context, source_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_tokens(context, source_id, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token ..") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token <<=") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "float_literal") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token catch ") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token finally ") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token throw ") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token throws ") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token try ") != NULL));
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "(token identifier ") != NULL) &&
                 (strstr(dump.bytes, "\"r\"") != NULL));
    for (keyword_index = 0U;
         keyword_index < (sizeof(atomic_keyword_tokens) / sizeof(atomic_keyword_tokens[0]));
         ++keyword_index) {
        R_TEST_CHECK((dump.bytes != NULL) &&
                     (strstr(dump.bytes, atomic_keyword_tokens[keyword_index]) != NULL));
    }
    free(dump.bytes);
    bom_id = r_test_add_source(context, "embedded-bom.r", embedded_bom, sizeof(embedded_bom));
    R_TEST_CHECK(r_frontend_lex(context, bom_id) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-LEX-001"));
    comment_id = r_test_add_source(
        context, "malformed-comment.r", malformed_comment, sizeof(malformed_comment));
    R_TEST_CHECK(r_frontend_lex(context, comment_id) == R_FRONTEND_INVALID_SOURCE);
    whitespace_id = r_test_add_source(
        context, "unicode-whitespace.r", unicode_whitespace, sizeof(unicode_whitespace));
    R_TEST_CHECK(r_frontend_lex(context, whitespace_id) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-LEX-004"));
    invalid_character_hex_id = r_test_add_source(context,
                                                 "invalid-character-hex.r",
                                                 (const uint8_t *)invalid_character_hex,
                                                 strlen(invalid_character_hex));
    R_TEST_CHECK(r_frontend_lex(context, invalid_character_hex_id) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic_rule(context, "R-DIAG-LEX-002", "R-LEX-0013"));
    invalid_character_split_utf8_id =
        r_test_add_source(context,
                          "invalid-character-split-utf8.r",
                          (const uint8_t *)invalid_character_split_utf8,
                          strlen(invalid_character_split_utf8));
    R_TEST_CHECK(r_frontend_lex(context, invalid_character_split_utf8_id) ==
                 R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic_rule(context, "R-DIAG-LEX-002", "R-LEX-0013"));
    r_frontend_destroy(context);
}

static void r_test_trailing_literal_escape_at_eof(void) {
    static const uint8_t unterminated_string[] = {(uint8_t)'"', (uint8_t)'v', (uint8_t)'\\'};
    static const uint8_t unterminated_character[] = {(uint8_t)'\'', (uint8_t)'v', (uint8_t)'\\'};
    static const uint8_t *const sources[] = {unterminated_string, unterminated_character};
    static const size_t lengths[] = {sizeof(unterminated_string), sizeof(unterminated_character)};
    static const char *const names[] = {"unterminated-string-escape.r",
                                        "unterminated-character-escape.r"};
    size_t case_index;

    for (case_index = 0U; case_index < (sizeof(sources) / sizeof(sources[0])); ++case_index) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id;
        size_t diagnostic_index;
        bool found_unterminated = false;

        R_TEST_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id =
            r_test_add_source(context, names[case_index], sources[case_index], lengths[case_index]);
        R_TEST_CHECK(r_frontend_lex(context, source_id) == R_FRONTEND_INVALID_SOURCE);
        for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
             ++diagnostic_index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);
            if ((diagnostic != NULL) && (strcmp(diagnostic->code, "R-DIAG-LEX-002") == 0) &&
                (strcmp(diagnostic->message, "unterminated literal") == 0)) {
                found_unterminated = true;
            }
        }
        R_TEST_CHECK(found_unterminated);
        r_frontend_destroy(context);
    }
}

static void r_test_lexer_unicode_and_lossless_tokens(void) {
    static const uint8_t valid_source[] = {
        0xEFU, 0xBBU, 0xBFU, 'm', 'o', 'd', 'u',  'l', 'e', ' ', 'c', 'a',  'f', 0xC3U, 0xA9U, ';',
        '\r',  '\n',  '/',   '/', ' ', 't', 'r',  'i', 'v', 'i', 'a', '\n', 'c', 'o',   'n',   's',
        't',   'e',   'x',   'p', 'r', ' ', 's',  't', 'r', ' ', 's', ' ',  '=', ' ',   '"',   '\\',
        'x',   'C',   '3',   '"', ' ', '"', '\\', 'x', 'A', '9', '"', ';',  '\n'};
    static const uint8_t decomposed_source[] = {
        'm', 'o', 'd', 'u', 'l', 'e', ' ', 'c', 'a', 'f', 'e', 0xCCU, 0x81U, ';', '\n'};
    static const uint8_t malformed_source[] = {'m', 'o', 'd', 'u', 'l', 'e', ' ', 0xC3U, ';'};
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId valid_id;
    RSourceId decomposed_id;
    RSourceId malformed_id;
    RTestBuffer tokens = {0};
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    valid_id = r_test_add_source(context, "unicode-valid.r", valid_source, sizeof(valid_source));
    R_TEST_CHECK(r_frontend_lex(context, valid_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_tokens(context, valid_id, r_test_write, &tokens) == R_FRONTEND_OK);
    R_TEST_CHECK((tokens.bytes != NULL) && (strstr(tokens.bytes, "(token bom 0 3") != NULL));
    R_TEST_CHECK((tokens.bytes != NULL) && (strstr(tokens.bytes, "line_comment") != NULL));
    free(tokens.bytes);
    decomposed_id = r_test_add_source(
        context, "unicode-not-nfc.r", decomposed_source, sizeof(decomposed_source));
    R_TEST_CHECK(r_frontend_lex(context, decomposed_id) == R_FRONTEND_INVALID_SOURCE);
    malformed_id = r_test_add_source(
        context, "unicode-malformed.r", malformed_source, sizeof(malformed_source));
    R_TEST_CHECK(r_frontend_lex(context, malformed_id) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-LEX-003"));
    r_frontend_destroy(context);
}

static void r_test_contextual_aggregate_classification(void) {
    static const char dependency[] = "module dependency;\n"
                                     "struct Node { i32 value; };\n"
                                     "protected struct Secret { i32 value; };\n";
    static const char consumer[] =
        "module consumer;\n"
        "import dependency::{Node};\n"
        "protected Node* select(Node* value) { Node* next = value; return next; }\n";
    static const char unresolved[] =
        "module unresolved;\n"
        "import dependency::{Secret};\n"
        "protected i32 broken(i32 value) { Secret* next = value; return value; }\n";
    static const char qualified_consumer[] =
        "module qualified.consumer;\n"
        "import dependency;\n"
        "protected dependency::Node* select(dependency::Node* value) {\n"
        "  dependency::Node* next = value;\n"
        "  return next;\n"
        "}\n";
    static const char missing_consumer[] = "module missing.consumer;\n"
                                           "import unavailable;\n"
                                           "protected i32 broken(i32 value) {\n"
                                           "  unavailable::Node* next = value;\n"
                                           "  return value;\n"
                                           "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId dependency_id;
    RSourceId consumer_id;
    RSourceId unresolved_id;
    RSourceId qualified_id;
    RSourceId missing_id;
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0};
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    dependency_id =
        r_test_add_source(context, "dependency.r", (const uint8_t *)dependency, strlen(dependency));
    consumer_id =
        r_test_add_source(context, "consumer.r", (const uint8_t *)consumer, strlen(consumer));
    unresolved_id =
        r_test_add_source(context, "unresolved.r", (const uint8_t *)unresolved, strlen(unresolved));
    qualified_id = r_test_add_source(
        context, "qualified.r", (const uint8_t *)qualified_consumer, strlen(qualified_consumer));
    missing_id = r_test_add_source(
        context, "missing.r", (const uint8_t *)missing_consumer, strlen(missing_consumer));
    R_TEST_CHECK(r_frontend_scan_interface(context, dependency_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_scan_interface(context, consumer_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_scan_interface(context, unresolved_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_scan_interface(context, qualified_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_scan_interface(context, missing_id) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_link_interfaces(context) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_parse_cst(context, consumer_id, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, consumer_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_parse_cst(context, unresolved_id, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_cst(context, unresolved_id, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "ambiguous_decl_or_expr") != NULL));
    R_TEST_CHECK(r_frontend_lower_ast(context, unresolved_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
    free(dump.bytes);
    (void)memset(&dump, 0, sizeof(dump));
    R_TEST_CHECK(r_frontend_parse_cst(context, qualified_id, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, qualified_id, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_parse_cst(context, missing_id, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_cst(context, missing_id, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "ambiguous_decl_or_expr") != NULL));
    R_TEST_CHECK(r_frontend_lower_ast(context, missing_id, &ast) == R_FRONTEND_NOT_LOWERABLE);
    free(dump.bytes);
    r_frontend_destroy(context);
}

static void *r_test_allocate(void *user_data, size_t size) {
    RTestAllocator *allocator = user_data;
    void *pointer;
    allocator->call_count += 1U;
    if (allocator->call_count == allocator->fail_at) {
        return NULL;
    }
    pointer = malloc(size);
    if (pointer != NULL) {
        allocator->live_count += 1U;
    }
    return pointer;
}

static void r_test_free(void *user_data, void *pointer) {
    RTestAllocator *allocator = user_data;
    if (pointer != NULL) {
        R_TEST_CHECK(allocator->live_count != 0U);
        if (allocator->live_count != 0U) {
            allocator->live_count -= 1U;
        }
        free(pointer);
    }
}

static void r_test_allocation_failure_sweep(void) {
    static const char source_text[] = "module allocation.test; struct Item { i32 value; }; "
                                      "protected i32 run(Item* item) { return item->value; }";
    size_t fail_at;
    for (fail_at = 1U; fail_at <= 96U; ++fail_at) {
        RTestAllocator allocator = {0};
        RFrontendOptions options = r_frontend_default_options();
        RFrontendContext *context;
        allocator.fail_at = fail_at;
        options.allocate = r_test_allocate;
        options.free = r_test_free;
        options.allocator_user_data = &allocator;
        context = r_frontend_create(&options);
        if (context != NULL) {
            RSourceId source_id = R_SOURCE_ID_INVALID;
            RFrontendStatus status = r_frontend_add_source(context,
                                                           "allocation.r",
                                                           (const uint8_t *)source_text,
                                                           strlen(source_text),
                                                           &source_id);
            if (status == R_FRONTEND_OK) {
                RAstNodeId root = R_AST_NODE_ID_INVALID;
                status = r_frontend_lower_ast(context, source_id, &root);
                if (!((status == R_FRONTEND_OK) || (status == R_FRONTEND_OUT_OF_MEMORY) ||
                      (status == R_FRONTEND_LIMIT_EXCEEDED) ||
                      (status == R_FRONTEND_INTERNAL_ERROR))) {
                    (void)fprintf(stderr,
                                  "allocation sweep fail_at=%zu returned status=%d\n",
                                  fail_at,
                                  (int)status);
                }
                R_TEST_CHECK((status == R_FRONTEND_OK) || (status == R_FRONTEND_OUT_OF_MEMORY) ||
                             (status == R_FRONTEND_LIMIT_EXCEEDED) ||
                             (status == R_FRONTEND_INTERNAL_ERROR));
            } else {
                R_TEST_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                             (status == R_FRONTEND_LIMIT_EXCEEDED));
            }
            r_frontend_destroy(context);
        }
        R_TEST_CHECK(allocator.live_count == 0U);
    }
}

static void r_test_frontend_limits(void) {
    static const char prefix[] = "module limits; protected i32 nested() ";
    static const char statement[] = "return 0;";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RTestBuffer source = {0};
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    size_t index;
    options.limits.max_nesting = 256U;
    R_TEST_CHECK(r_test_write(&source, prefix, strlen(prefix)));
    for (index = 0U; index < 300U; ++index) {
        R_TEST_CHECK(r_test_write(&source, "{", 1U));
    }
    R_TEST_CHECK(r_test_write(&source, statement, strlen(statement)));
    for (index = 0U; index < 300U; ++index) {
        R_TEST_CHECK(r_test_write(&source, "}", 1U));
    }
    context = r_frontend_create(&options);
    R_TEST_CHECK(context != NULL);
    if (context != NULL) {
        source_id =
            r_test_add_source(context, "limits.r", (const uint8_t *)source.bytes, source.length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_LIMIT_EXCEEDED);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-LIMIT-001"));
        r_frontend_destroy(context);
    }
    free(source.bytes);
    {
        uint8_t dummy = UINT8_C(0);
        RSourceId rejected = R_SOURCE_ID_INVALID;
        size_t oversized = (64U * 1024U * 1024U) + 1U;
        context = r_frontend_create(NULL);
        R_TEST_CHECK(context != NULL);
        if (context != NULL) {
            R_TEST_CHECK(
                r_frontend_add_source(context, "oversized.r", &dummy, oversized, &rejected) ==
                R_FRONTEND_LIMIT_EXCEEDED);
            R_TEST_CHECK(rejected == R_SOURCE_ID_INVALID);
            R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-LIMIT-001"));
            r_frontend_destroy(context);
        }
    }
}

static void r_test_method_syntax(void) {
    static const char text[] =
        "module syntax.methods;\n"
        "struct Point { i32 x; i32 y; };\n"
        "@generic<T> struct Holder { T value; };\n"
        "trait Shown { u64 show(const Self* this); };\n"
        "trait Reported { u64 report(const Self* this, i32 scale) throws Failure; };\n"
        "error Failure { code, };\n"
        "impl Shown for Point { u64 show(const Point* this) { return 1u64; } };\n"
        "impl Shown for i32 { u64 show(const i32* this) { return 2u64; } };\n"
        "@generic<T: copy> impl Shown for Holder<T> {\n"
        "    u64 show(const Self* this) { return 3u64; }\n"
        "};\n"
        "Point Point::origin() { return Point { .x = 0, .y = 0 }; }\n"
        "protected i32 Point::sum(const Point* this) { return this->x + this->y; }\n"
        "void Point::shift(Point* this, i32 dx) { this->x += dx; }\n"
        "i32 Point::scaled(Point this, i32 factor) { i32 total = this.x; return total * factor; }\n"
        "@generic<T: copy> T Holder<T>::get(const Holder<T>* this) { return this->value; }\n"
        "@generic<U: Shown & copy> u64 show_of(const U* value) { u64 code = value->show();"
        " return code; }\n"
        "i32 main() {\n"
        "    Point point = Point::origin();\n"
        "    point.shift(2);\n"
        "    i32 total = point.sum();\n"
        "    const Point* view = &point;\n"
        "    i32 indirect = view->sum();\n"
        "    u64 shown = point.show();\n"
        "    u64 generic = show_of(&point);\n"
        "    return total + indirect + (shown as i32) + (generic as i32);\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0}, reconstructed = {0};
    RSourceId source;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "methods.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "trait_declaration") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "trait_name") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "impl_declaration") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "method_call_suffix") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "associated_name") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

/* R-FUNC-0024 (L19): a call continued by further suffixes is syntax; whether the method permits
   it is checked with the method. The source round trip keeps every byte. */
static void r_test_method_chain_syntax(void) {
    static const char text[] =
        "module syntax.method_chain; struct Point { i32 x; }; @chain Point Point::a(Point this) "
        "{ return move this; } i32 Point::b(Point this) { return this.x; } i32 caller(Point p) { "
        "i32 v = p.a().a().b(); return v + (p.a()).b(); }";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0};
    RTestBuffer reconstructed = {0};
    RSourceId source;

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "chain.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "method_call_suffix") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

/* L21.1, R-AGG-0011: `error Name : Parent { fields };` names a parent error, while a primitive
   after ':' still declares a fieldless error enum; the source reconstructs exactly. */
static void r_test_error_parent_syntax(void) {
    static const char text[] =
        "module syntax.error_parent; error io_error { i32 code; }; error net_error : io_error { "
        "u16 port; }; error code_error : u8 { first, second }; error qualified_error : "
        "syntax.error_parent::io_error { i32 line; };";
    static const char broken[] = "module syntax.error_parent; error bad_error : { i32 code; };";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer reconstructed = {0};
    RSourceId source;

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "parent.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(reconstructed.bytes);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "broken-parent.r", (const uint8_t *)broken, strlen(broken));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
    r_frontend_destroy(context);
}

static void r_test_method_recovery(void) {
    static const char *const broken[] = {
        "struct Point { i32 x; }; i32 Point::wide(i32 first, Point* this) { return first; }",
        "trait Shown { u64 show(const Self* this) { return 1u64; };",
        "struct Point { i32 x; }; trait Shown { u64 show(const Self* this); };"
        " impl Shown for Point { u64 show(const Point* this); };",
        "struct Point { i32 x; }; trait Shown { u64 show(const Self* this); };"
        " impl Shown Point { u64 show(const Point* this) { return 1u64; } };",
        "trait Shown { u64 show(const Self* this); }",
        "struct Point { i32 x; }; protected trait Shown { u64 show(const Self* this); };"
        " protected impl Shown for Point { u64 show(const Point* this) { return 1u64; } };",
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.method_recovery; %s i32 after() { return 0; }",
                              broken[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        RSourceId source;
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        source =
            r_test_add_source(context, "broken-method.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_lambda_syntax(void) {
    static const char text[] =
        "module syntax.lambdas;\n"
        "@generic<F: fn(i32) -> i32> i32 apply(const F* f, i32 x) { i32 r = f(x); return r; }\n"
        "@generic<G: fn(i32, u8) -> u64 & copy> u64 both(const G* g) { u64 r = g(1, 2u8);"
        " return r; }\n"
        "i32 main() {\n"
        "    i32 offset = 10;\n"
        "    own i32* boxed = new i32(3);\n"
        "    fn i32 add(i32 x) { return x + offset; }\n"
        "    fn i32 unbox() move(boxed) { i32 value = *boxed; return value; }\n"
        "    auto seed = add(1);\n"
        "    i32 applied = apply(&add, seed);\n"
        "    i32 taken = unbox();\n"
        "    return applied + taken;\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0}, reconstructed = {0};
    RSourceId source;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "lambdas.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "lambda_declaration") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "move_capture_list") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "callable_constraint") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_range_for_syntax(void) {
    static const char text[] =
        "module syntax.iteration;\n"
        "trait Source { type Item; o<Self::Item> pull(Self* this); };\n"
        "struct Counter { i32 next_value; i32 limit; };\n"
        "impl Source for Counter { type Item = i32; o<i32> pull(Counter* this) { return o::none; } "
        "};\n"
        "impl core::Iterator for Counter { type Item = i32; o<i32> next(Counter* this) {"
        " return o::none; } };\n"
        "@generic<I: core::Iterator> i32 count(I* it) { i32 n = 0; for (auto item in it)"
        " { n += 1; } return n; }\n"
        "@generic<S: Source> o<S::Item> first(S* source) { o<S::Item> v = source->pull();"
        " return move v; }\n"
        "i32 main() {\n"
        "    i32 total = 0;\n"
        "    for (i32 i in 0..10) { total += i; }\n"
        "    i32[2] pair = {1, 2};\n"
        "    for (const i32* x in &pair) { total += *x; }\n"
        "    Counter c = Counter { .next_value = 0, .limit = 3 };\n"
        "    for (i32 v in move c) { total += v; }\n"
        "    if (total in 0..100 && total not in 5..6) { total += 1; }\n"
        "    bool b = total + 1 in 1..3;\n"
        "    return total;\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0}, reconstructed = {0};
    RSourceId source;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "iteration.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "for_in_statement") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "range_expression") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "membership_expression") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "associated_type") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_range_for_recovery(void) {
    static const char *const broken[] = {
        "i32 caller() { for (i32 i in) { } return 0; }",
        "i32 caller() { for (i32 i in 0..) { } return 0; }",
        "i32 caller() { for (i in 0..3) { } return 0; }",
        "i32 caller() { i32 a = 1; if (a in 0..3 in 1..2) { } return 0; }",
        "i32 caller() { i32 a = 1; bool b = a not 0..3; return 0; }",
        "trait Source { type Item = i32; };",
        "struct P { i32 x; }; trait Source { type Item; }; impl Source for P { type Item; };",
        "trait Source { type; };",
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.iteration_recovery; %s i32 after() { return 0; }",
                              broken[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        RSourceId source;
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        source =
            r_test_add_source(context, "broken-iteration.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_collection_syntax(void) {
    static const char text[] =
        "module syntax.collections;\n"
        "i32 sum(i32... values) { i32 total = 0; for (const i32* v in &values) { total += *v; }"
        " return total; }\n"
        "i32 main() {\n"
        "    try {\n"
        "        array<i32> small = [1, 2, 3];\n"
        "        array<i32> empty = [];\n"
        "        array<i32> squares = [x * x for (i32 x in 0..10) if (x % 2 == 0)];\n"
        "        array<i32> pairs = [x + *y for (i32 x in 0..3) for (auto y in &small)"
        " if (x < *y)];\n"
        "        dict<i32, i32> doubles = {x: x * 2 for (i32 x in 0..4)};\n"
        "        dict<i32, i32> fixed = {1: 10, 2: 20};\n"
        "        i32 a = sum(1, 2, 3);\n"
        "        i32 b = sum();\n"
        "        i32[2] pair = {4, 5};\n"
        "        const i32[] view = &pair;\n"
        "        i32 c = sum(...view);\n"
        "        return a + b + c;\n"
        "    } catch (std.array::push_error<i32> failure) {\n"
        "        failure as void;\n"
        "        return 1;\n"
        "    } catch (std.dict::insert_error<i32, i32> failure) {\n"
        "        failure as void;\n"
        "        return 2;\n"
        "    }\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0}, reconstructed = {0};
    RSourceId source;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "collections.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "collection_expression") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "dict_expression") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "dict_entry") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "comprehension_for") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "comprehension_if") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "spread_argument") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "aggregate_initializer") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_collection_recovery(void) {
    static const char *const broken[] = {
        "i32 caller() { array<i32> a = [1 2]; return 0; }",
        "i32 caller() { array<i32> a = [x for x in 0..3]; return 0; }",
        "i32 caller() { array<i32> a = [x if (x > 0) for (i32 x in 0..3)]; return 0; }",
        "i32 caller() { array<i32> a = [x for (i32 x in 0..3) for]; return 0; }",
        "i32 caller() { array<i32> a = [x, y for (i32 x in 0..3)]; return 0; }",
        "i32 caller() { dict<i32, i32> d = {1: 2, 3: 4 for (i32 x in 0..3)}; return 0; }",
        "i32 caller() { dict<i32, i32> d = {1: 2, 3}; return 0; }",
        "i32 caller() { dict<i32, i32> d = {1: }; return 0; }",
        "i32 sum(i32... values, i32 last) { return 0; }",
        "i32 sum(i32...) { return 0; }",
        ("i32 sum(i32... values) { return 0; } i32 caller() { i32[1] a = {1}; "
         "const i32[] v = &a; i32 r = sum(...v, 1); return r; }"),
        "i32 sum(i32... values) { return 0; } i32 caller() { i32 r = sum(...); return r; }",
        "i32 sum(i32... values) { return 0; } i32 caller() { i32 r = sum([1, 2); return r; }",
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.collection_recovery; %s i32 after() { return 0; }",
                              broken[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        RSourceId source;
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        source = r_test_add_source(
            context, "broken-collections.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_reflection_syntax(void) {
    static const char text[] =
        "module syntax.reflection;\n"
        "enum Color { red, green, blue, };\n"
        "enum Shape { dot, circle(f32), };\n"
        "struct Point { i32 x; i32 y; };\n"
        "usize take(usize value, constexpr str name) { name as void; return value; }\n"
        "i32 main() {\n"
        "    Color[3] all = core::enum_variants::<Color>();\n"
        "    Color first = core::enum_min::<Color>();\n"
        "    Color last = core::enum_max::<Color>();\n"
        "    constexpr str name = core::enum_name(first);\n"
        "    usize ordinal = core::enum_ordinal(last);\n"
        "    o<Color> at = core::enum_at::<Color>(ordinal);\n"
        "    o<Color> found = core::enum_from_name::<Color>(\"blue\");\n"
        "    Shape shape = Shape::dot;\n"
        "    constexpr str variant_label = core::variant_name(&shape);\n"
        "    constexpr str spelled = core::type_name::<const Point*>();\n"
        "    constexpr str target = core::target_name();\n"
        "    constexpr str profile = core::profile_name();\n"
        "    usize total = take(core::enum_count::<Color>(), core::field_name::<Point>(1));\n"
        "    total += take(core::variant_count::<Shape>(), core::type_name::<array<i32>>());\n"
        "    total += core::field_count::<Point>();\n"
        "    all[0] as void;\n"
        "    at as void;\n"
        "    found as void;\n"
        "    variant_label as void;\n"
        "    spelled as void;\n"
        "    target as void;\n"
        "    profile as void;\n"
        "    name as void;\n"
        "    return total as i32;\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0}, reconstructed = {0};
    RSourceId source;
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source = r_test_add_source(context, "reflection.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "standard_type_call") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "enum_variants") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "field_name") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_reflection_recovery(void) {
    static const char *const broken[] = {
        "i32 caller() { usize n = core::enum_count::()(); return 0; }",
        "i32 caller() { usize n = core::enum_count::<Color>(1); return 0; }",
        "i32 caller() { constexpr str n = core::field_name::<Point>(); return 0; }",
        "i32 caller() { o<Color> c = core::enum_at::<Color>(); return 0; }",
        "i32 caller() { o<Color> c = core::enum_from_name::()(\"red\"); return 0; }",
        ("usize take(usize v) { return v; } "
         "i32 caller() { usize n = take(core::enum_at::<Color>(0, 1)); return 0; }"),
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.reflection_recovery; %s i32 after() { return 0; }",
                              broken[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        RSourceId source;
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        source = r_test_add_source(
            context, "broken-reflection.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_lambda_recovery(void) {
    static const char *const broken[] = {
        "i32 caller() { fn i32 add(i32 x) throws { return x; } return 0; }",
        "i32 caller() { fn add(i32 x) { return x; } return 0; }",
        "i32 caller() { fn i32 add(i32 x) move() { return x; } return 0; }",
        "i32 caller() { fn i32 add(i32 x) move(a { return x; } return 0; }",
        "i32 caller() { fn i32 add(i32 x); return 0; }",
        "@generic<F: fn(i32) i32> i32 apply(const F* f) { return 0; }",
        "@generic<F: fn i32 -> i32> i32 apply(const F* f) { return 0; }",
        "i32 caller() { auto; return 0; }",
        "fn i32 top(i32 x) { return x; }",
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.lambda_recovery; %s i32 after() { return 0; }",
                              broken[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        RSourceId source;
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        source =
            r_test_add_source(context, "broken-lambda.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_generic_syntax(void) {
    static const char text[] =
        "module syntax.generic_cases;\n"
        "@ /* header */ generic /* parameters */ <T> struct Forward { Box<T> value; };\n"
        "@repr(C) @generic<T: copy & copy> @repr(C) protected struct Box { T value; };\n"
        "@generic<T> enum Choice { Some(T), Fields { T value; }, None, };\n"
        "@generic<E: error & send & unborrowed> error Failure { E value; };\n"
        "@generic<T> struct generic { T value; };\n"
        "generic<i32> ordinary(generic<i32> value) { return value; }\n"
        "generic<i32> global = generic<i32> { .value = 3 };\n"
        "generic<i32>* pointer(generic<i32>* value) { return value; }\n"
        "@generic<T> T identity(T value) { return move value; }\n"
        "@generic<T: send & unborrowed> async T relay(T value) { return move value; }\n"
        "@generic<T: copy> drop(Box<T>* self) {}\n"
        "@generic<T: copy> u64 Box<T>::hash(const Box<T>* value) { return 0; }\n"
        "@generic<T: copy> bool Box<T>::equal(const Box<T>* left, const Box<T>* right) { return "
        "true; }\n"
        "i32 after() {\n"
        "    i32 generic = 7;\n"
        "    Box<Box<i32>> nested = Box<Box<i32>> { .value = Box<i32> { .value = generic } };\n"
        "    Choice<i32> first = Choice<i32>::Some(1);\n"
        "    Choice<i32> second = Choice<i32>::Fields { .value = 2 };\n"
        "    /* Preserve comments and forward references exactly. */\n"
        "    return 0;\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSyntaxNodeId cst;
    RAstNodeId ast;
    RTestBuffer dump = {0}, reconstructed = {0};
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    RSourceId source = r_test_add_source(context, "generic.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) == R_FRONTEND_OK);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "generic_header") != NULL);
    R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "generic_parameter") != NULL);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(dump.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_generic_recovery(void) {
    static const char *const broken[] = {
        "@generic<T:> struct Broken { T value; };",
        "@generic<T T> struct Broken { T value; };",
        "@generic<T: copy | pod> struct Broken { T value; };",
        "@generic<T,> error Broken { Value(T), };",
        "@generic<> struct Broken {};",
        "@generic<T> @generic<U> struct Broken { T value; };",
        "@generic<T> @repr(C) @generic<U> struct Broken { T value; };",
        "@generic struct Broken {};",
        "@generic<T struct Broken { T value; };",
        "@generic<T: copy &> struct Broken { T value; };",
        "@generic<T: & copy> struct Broken { T value; };",
        "struct Broken { @generic<T> i32 value; };",
        "error Broken { Fields { @generic<T> i32 value; }, };",
        "void broken(@generic<T> i32 value) {}",
        "void broken() { @generic<T> i32 value = 1; value as void; }",
        "@generic<T> i32 value = 1;",
        "@generic<T> extern \"C\" { c_int value; }",
        "extern \"C\" { @generic<T> c_int broken(c_int value); }",
        "protected @generic<T> struct Broken { T value; };",
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.recovery; %s i32 after() { return 0; }",
                              broken[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0}, dump = {0};
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        RSourceId source =
            r_test_add_source(context, "broken-generic.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        R_TEST_CHECK(r_frontend_dump_cst(context, source, r_test_write, &dump) == R_FRONTEND_OK);
        R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, "after") != NULL);
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(dump.bytes);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_legacy_generic_diagnostic(void) {
    static const char *const declarations[] = {
        "generic<T> struct Broken { T value; };",
        "generic<T> enum Broken { Value(T), };",
        "generic<T> error Broken { T value; };",
        "generic<T> T broken(T value) { return move value; }",
        "generic(T: send & unborrowed) async T broken(T value) { return move value; }",
        "@generic<T> struct Box { T value; }; generic<T> drop(Box<T>* self) {}",
        "@repr(C) generic /* legacy */ (T) @repr(C) struct Broken { T value; };",
    };
    for (size_t index = 0U; index < sizeof(declarations) / sizeof(declarations[0]); ++index) {
        char text[512];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.legacy; %s i32 after() { return 0; }",
                              declarations[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        bool found = false;
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        RSourceId source =
            r_test_add_source(context, "legacy-generic.r", (const uint8_t *)text, (size_t)length);
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) == R_FRONTEND_INVALID_SOURCE);
        for (size_t diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
             ++diagnostic_index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);
            if (strcmp(diagnostic->code, "R-DIAG-SYN-001") == 0 &&
                strcmp(diagnostic->message, "use @generic<...> to declare generic parameters") ==
                    0) {
                found = true;
            }
        }
        R_TEST_CHECK(found);
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_static_conditions(void) {
    static const char *const cases[] = {
        "@ /* prefix */ if (core::profile is hosted-native-async) { i32 f() { return 1; } } @else "
        "@if (core::target is \"target\") { error E {}; } @else { struct S {}; }",
        "@generic<T> T f(T x) { @if ((T is copy && !(T is pod)) || (T is send && T is sync)) { "
        "return x; } @else { return move x; } }",
        "@generic<F> i32 f(const F* x) { @if (F is fn(i32) -> i32) { i32 y = x(1); return y; } "
        "return 0; }",
        "void f() { @if ((i32) is i32) { } @else { } }",
        "@generic<const usize N> usize f() { @if (N <= 64usize && !(N == 0usize) || ((N + "
        "1usize) > 4usize)) { return 1usize; } return 0usize; }",
        "void f() { @if (true) { } @else @if (false || 1 in 0..4 || 2 not in 5..9) { } }",
        "@if (sizeof(i32) == 4usize && core::profile is hosted) { i32 g() { return 1; } }",
        "@generic<T> void f() { @if (T is copy && core::enum_count::<E>() <= 8usize) { } }",
    };
    static const char *const invalid[] = {
        "void f() { @if () {} }",
        "void f() { @if (value) {} }",
        "void f() { @if (1 <= ) {} }",
        "void f() { @if (i32 copy) {} }",
        "void f() { @if (i32 is) {} }",
        "void f() { @if (i32 is copy &&) {} }",
        "void f() { @else {} }",
        "@if (core::profile is hosted) { import other; }",
        "struct S { @if (i32 is copy) { i32 value; } };",
        "void f() { i32 value = @if (i32 is copy) { 1; }; }",
    };
    for (size_t index = 0U;
         index < sizeof(cases) / sizeof(cases[0]) + sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        const bool valid = index < sizeof(cases) / sizeof(cases[0]);
        const char *body = valid ? cases[index] : invalid[index - sizeof(cases) / sizeof(cases[0])];
        char text[1024];
        RFrontendContext *context = r_frontend_create(NULL);
        RTestBuffer reconstructed = {0};
        RAstNodeId ast;
        (void)snprintf(
            text, sizeof(text), "module static_syntax; %s i32 after() { return 0; }", body);
        R_TEST_CHECK(context != NULL);
        if (context == NULL)
            return;
        RSourceId source =
            r_test_add_source(context, "static.r", (const uint8_t *)text, strlen(text));
        RFrontendStatus status = r_frontend_lower_ast(context, source, &ast);
        if (status != (valid ? R_FRONTEND_OK : R_FRONTEND_NOT_LOWERABLE)) {
            (void)fprintf(stderr, "static syntax case %zu: %s\n", index, body);
            R_TEST_CHECK(false);
        }
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == strlen(text) &&
                     memcmp(reconstructed.bytes, text, strlen(text)) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_const_generics(void) {
    static const char *const cases[] = {
        ("@generic<T, const /* capacity */ usize N> struct B { T[N] data; };"),
        ("@generic<const usize N> enum E { Value(u8[N]), Empty, };"),
        ("@generic<const usize N> error E { u8[N] data; };"),
        ("@generic<const usize N> async usize f(u8[N] data) { return N; }"),
        ("@generic<const usize N> struct B { u8[N] data; }; @generic<const usize N> drop(B<N>* "
         "self) {}"),
        ("@generic<const usize N> struct B { u8[N] data; }; void f() { B<(2usize + 2usize)> value "
         "= {}; }"),
        ("@generic<const i32 N, const bool F, const u8 M> struct B {}; void f() { B<-1, true, 3u8> "
         "value = {}; }"),
        /* R-TYPE-0050: associated constants and `Self::NAME`. */
        ("trait S { const usize N; const bool F = false; usize n(const Self* this) { return "
         "Self::N; } }; struct P {}; impl S for P { const usize N = 2usize; };"),
        /* R-TYPE-0051: dyn interface types behind borrows. */
        ("trait S { usize n(const Self* this); }; usize f(const dyn(S & send)* s, dyn(S)*? t) { "
         "dyn(S)* local = null; return 0usize; }"),
    };
    static const char *const invalid[] = {
        ("@generic<const usize> struct B {};"),
        ("@generic<const f64 N> struct B {};"),
        ("trait S { const f64 N; };"),
        ("trait S { const usize N = ; };"),
        ("trait S { const usize N; }; struct P {}; impl S for P { const usize N; };"),
        ("void f(dyn S* s) {}"),
        ("void f(dyn(S* s) {}"),
        ("@generic<const N> struct B {};"),
        ("@generic<const usize N: copy> struct B {};"),
        ("@generic<const usize N=4usize> struct B {};"),
        ("@generic<const usize N,> struct B {};"),
        ("@generic<const usize N> struct B {}; void f() { B<2usize + > value = {}; }"),
    };
    for (size_t index = 0U;
         index < sizeof(cases) / sizeof(cases[0]) + sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        const bool valid = index < sizeof(cases) / sizeof(cases[0]);
        const char *body = valid ? cases[index] : invalid[index - sizeof(cases) / sizeof(cases[0])];
        char text[1024];
        RFrontendContext *context = r_frontend_create(NULL);
        RTestBuffer reconstructed = {0};
        RAstNodeId ast;
        (void)snprintf(
            text, sizeof(text), "module const_syntax; %s i32 after() { return 0; }", body);
        R_TEST_CHECK(context != NULL);
        if (context == NULL)
            return;
        RSourceId source =
            r_test_add_source(context, "const.r", (const uint8_t *)text, strlen(text));
        RFrontendStatus status = r_frontend_lower_ast(context, source, &ast);
        if (status != (valid ? R_FRONTEND_OK : R_FRONTEND_NOT_LOWERABLE)) {
            (void)fprintf(stderr, "const generic syntax case %zu: %s\n", index, body);
            R_TEST_CHECK(false);
        }
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == strlen(text) &&
                     memcmp(reconstructed.bytes, text, strlen(text)) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_closure_modes(void) {
    static const char *const cases[] = {
        "@must_use @generic<T> struct Ticket { T value; }; @must_use i32 result() { return 1; }",
        "@discardable i32 result() { return 1; } @discardable async i32 later() { return 2; }",
        "error Failure {}; void f() { fn @noalloc once i32 work() throws Failure { return 1; } }",
        "error Failure {}; void f() { async fn i32 work() throws Failure { return 1; } }",
        "error Failure {}; @generic<F: async fn(i32) -> i32 throws(Failure) & send & unborrowed> "
        "void f(F work) {}",
        "@generic<T> T f(T value) { fn once T identity(T argument) { return move argument; } "
        "T result = identity(move value); return move result; }",
        "void f() { fn @ /* guarantee */ noalloc @nonblocking mut i32 work(i32 x) { return x; } }",
        "@generic<F: fn @noalloc @nonblocking(i32) -> i32> void f(F work) {}",
        "void f(raw fn @noalloc @nonblocking?(c_int) -> c_int work) {}",
        "@generic<F> void f() { @if (F is fn @noalloc() -> void) {} }",
        "void f() { fn /* mode */ mut i32 work(i32 x) { return x; } }",
        ("@generic<F: fn once(i32) -> i32 & send> i32 f(F work) { i32 x = (move work).call(1); "
         "return x; }"),
        "@generic<F> void f() { @if (F is fn shared() -> void) { } }",
        "@generic<T> struct Box { T value; }; void f() { fn once Box<i32> make() { return Box<i32> "
        "{.value=1}; } }",
        "@generic<T> struct mut { T value; }; void f() { fn mut<i32> make() { return mut<i32> "
        "{.value=1}; } }",
        "void f() { fn mut make() { return mut {}; } }",
    };
    static const char *const invalid[] = {
        "void f() { fn mut once i32 work() {} }",
        "@generic<F: fn mut -> i32> void f(F work) {}",
        "void f() { fn once i32 work( {} }",
    };
    for (size_t index = 0U;
         index < sizeof(cases) / sizeof(cases[0]) + sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        const bool valid = index < sizeof(cases) / sizeof(cases[0]);
        const char *body = valid ? cases[index] : invalid[index - sizeof(cases) / sizeof(cases[0])];
        char text[1024];
        RFrontendContext *context = r_frontend_create(NULL);
        RTestBuffer reconstructed = {0};
        RAstNodeId ast;
        (void)snprintf(
            text, sizeof(text), "module closure_syntax; %s i32 after() { return 0; }", body);
        R_TEST_CHECK(context != NULL);
        if (context == NULL)
            return;
        RSourceId source =
            r_test_add_source(context, "closure.r", (const uint8_t *)text, strlen(text));
        RFrontendStatus status = r_frontend_lower_ast(context, source, &ast);
        if (status != (valid ? R_FRONTEND_OK : R_FRONTEND_NOT_LOWERABLE)) {
            (void)fprintf(stderr, "closure syntax case %zu: %s\n", index, body);
            R_TEST_CHECK(false);
        }
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == strlen(text) &&
                     memcmp(reconstructed.bytes, text, strlen(text)) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

static void r_test_static_profile_after_parse(void) {
    static const char text[] = "module static_profile; @if (core::profile is freestanding) { i32 "
                               "selected() { return 0; } }"
                               "@else { i32 selected() { return inactive_name; } }";
    RFrontendContext *context = r_frontend_create(NULL);
    RTestBuffer before = {0}, after = {0}, reconstructed = {0};
    RAstNodeId ast;
    R_TEST_CHECK(context != NULL);
    if (context == NULL)
        return;
    RSourceId source =
        r_test_add_source(context, "static-profile.r", (const uint8_t *)text, strlen(text));
    R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &before) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_set_profile(context, "freestanding") == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &after) == R_FRONTEND_OK);
    R_TEST_CHECK(before.length == after.length &&
                 memcmp(before.bytes, after.bytes, before.length) == 0);
    R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reconstructed.length == strlen(text) &&
                 memcmp(reconstructed.bytes, text, strlen(text)) == 0);
    free(before.bytes);
    free(after.bytes);
    free(reconstructed.bytes);
    r_frontend_destroy(context);
}

static void r_test_await_calls(void) {
    static const char *const bodies[] = {
        "await action(); i32 value = await demo.name::result(1);",
        "await /* call */ std.io::flush(&stream, o::none);",
        "i32 result = await identity((1 + 2)); await move pending;",
        "await pending;",
        "await move action();",
        "return await action();",
        "consume(await action());",
        "await action(await another());",
        "await action( ;",
        "await ;",
        "if (await action()) {}",
        "static i32 value = await action();",
    };
    for (size_t index = 0U; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        char source_text[512];
        int length = snprintf(source_text,
                              sizeof(source_text),
                              "module syntax.await_calls; async void test() { %s } "
                              "i32 after() { return 0; }",
                              bodies[index]);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source_text));
        if (context == NULL) {
            continue;
        }
        RSourceId source = r_test_add_source(
            context, "await-calls.r", (const uint8_t *)source_text, (size_t)length);
        // Storage modifiers are parsed and rejected by semantic analysis.
        bool valid = index < 3U || (index >= 5U && index <= 7U) || index == 11U;
        R_TEST_CHECK(r_frontend_parse_cst(context, source, &cst) ==
                     (valid ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, source_text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

#include "format_syntax_tests.inc"

static void r_test_profile_selection(void) {
    static const char source_text[] = "module profile.unit;\n"
                                      "i32 main() {\n"
                                      "  own i32* value = new i32(7);\n"
                                      "  i32 result = *value;\n"
                                      "  drop value;\n"
                                      "  return result - 7;\n"
                                      "}\n";
    RFrontendContext *context = r_frontend_create(NULL);

    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_TEST_CHECK(r_frontend_set_profile(context, "imaginary") == R_FRONTEND_INVALID_ARGUMENT);
    R_TEST_CHECK(r_frontend_set_profile(NULL, "hosted") == R_FRONTEND_INVALID_ARGUMENT);
    R_TEST_CHECK(r_frontend_set_profile(context, "freestanding") == R_FRONTEND_OK);
    (void)r_test_add_source(
        context, "profile-unit.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_TEST_CHECK(r_test_has_diagnostic_rule(context, "R-DIAG-PROFILE-001", "R-CONF-G005"));
    R_TEST_CHECK(r_frontend_set_profile(context, "hosted") == R_FRONTEND_CONTEXT_SEALED);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_TEST_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_test_add_source(
        context, "profile-default.r", (const uint8_t *)source_text, strlen(source_text));
    R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_TEST_CHECK(!r_test_has_diagnostic(context, "R-DIAG-PROFILE-001"));
    r_frontend_destroy(context);
}

#include "compositional_syntax_tests.inc"

int main(void) {
    r_test_format_syntax();
    r_test_profile_selection();
    r_test_await_calls();
    r_test_static_conditions();
    r_test_const_generics();
    r_test_closure_modes();
    r_test_static_profile_after_parse();
    r_test_generic_syntax();
    r_test_compositional_syntax();
    r_test_method_syntax();
    r_test_method_chain_syntax();
    r_test_error_parent_syntax();
    r_test_method_recovery();
    r_test_lambda_syntax();
    r_test_lambda_recovery();
    r_test_range_for_syntax();
    r_test_range_for_recovery();
    r_test_collection_syntax();
    r_test_collection_recovery();
    r_test_reflection_syntax();
    r_test_reflection_recovery();
    r_test_generic_recovery();
    r_test_legacy_generic_diagnostic();
    r_test_valid_frontend();
    r_test_restricted_syntax();
    r_test_async_ffi_and_attribute_diagnostics();
    r_test_raw_function_type_syntax();
    r_test_type_statement_and_expression_inventory();
    r_test_null_type_syntax();
    r_test_checked_error_syntax();
    r_test_error_declarations();
    r_test_conditional_expression_syntax();
    r_test_conditional_throw_syntax();
    r_test_checked_error_recovery();
    r_test_checked_error_clause_order_recovery();
    r_test_lexer_unicode_and_lossless_tokens();
    r_test_lexer_boundaries();
    r_test_trailing_literal_escape_at_eof();
    r_test_contextual_aggregate_classification();
    r_test_allocation_failure_sweep();
    r_test_frontend_limits();
    if (failures != 0) {
        (void)fprintf(stderr, "%d frontend test checks failed\n", failures);
        return 1;
    }
    (void)printf("r_frontend_tests: ok\n");
    return 0;
}
