module test.codegen.char_cast_failure;

/* R-EXPR-0016: a surrogate code point is not a Unicode scalar value. */
i32 main() {
    u32 surrogate = 55296;
    char invalid = surrogate as char;
    return invalid as i32;
}
