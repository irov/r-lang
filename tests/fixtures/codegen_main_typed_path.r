module test.codegen.main_typed_path;

i32 main() {
    std.fs::path value = std.fs::path_from_utf8("a\0b"); drop value; return 0;
}
