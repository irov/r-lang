module test.semantic.io_as_error_argument_type;

void inspect(std.error::error value) {
    std.error::error converted = std.io::as_error(value);
    converted as void;
}
