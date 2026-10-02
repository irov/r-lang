module test.semantic.fs_as_error_argument_type;

void inspect(std.error::error value) {
    std.error::error converted = std.fs::as_error(value);
    converted as void;
}
