module test.semantic.fs_path_is_absolute_result_type;

void inspect(const std.fs::path* source) {
    i32 observed = std.fs::path_is_absolute(source);
    observed as void;
}
