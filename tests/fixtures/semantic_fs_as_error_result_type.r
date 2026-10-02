module test.semantic.fs_as_error_result_type;

void inspect(std.fs::fs_error value) {
    std.fs::fs_error converted = std.fs::as_error(value);
    converted as void;
}
