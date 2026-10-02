module test.codegen.main_typed_parse;

i32 main() {
    std.convert::parse_error failure = {.code = std.convert::parse_error_code::invalid_digit, .index = 7}; throw failure;
}
