module test.semantic.fs_as_error_arity;

void inspect() {
    std.error::error converted = std.fs::as_error();
    converted as void;
}
