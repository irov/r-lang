module test.semantic.fs_path_is_absolute_arity;

void inspect() {
    bool observed = std.fs::path_is_absolute();
    observed as void;
}
