module test.semantic.fs_path_is_absolute_argument_type;

void inspect(const bool* source) {
    bool observed = std.fs::path_is_absolute(source);
    observed as void;
}
