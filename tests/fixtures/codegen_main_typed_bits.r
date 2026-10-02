module test.codegen.main_typed_bits;

i32 main() {
    std.bits::read_error failure = {.code = std.bits::read_error_code::unexpected_end, .byte_index = 7}; throw failure;
}
