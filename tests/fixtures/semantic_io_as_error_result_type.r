module test.semantic.io_as_error_result_type;

void inspect(std.io::io_error value) {
    std.io::io_error converted = std.io::as_error(value);
    converted as void;
}
