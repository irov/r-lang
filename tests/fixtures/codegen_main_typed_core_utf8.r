module test.codegen.main_typed_core_utf8;

i32 main() {
    const u8[1] bytes = {255}; str text = core::validate_utf8(bytes); return len(text) as i32;
}
