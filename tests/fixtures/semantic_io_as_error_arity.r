module test.semantic.io_as_error_arity;

void inspect() {
    std.error::error converted = std.io::as_error();
    converted as void;
}
