module test.codegen.main_typed_string;

i32 main() {
    const u8[1] source = {255}; std.string::string value = std.string::from_utf8(source); drop value; return 0;
}
