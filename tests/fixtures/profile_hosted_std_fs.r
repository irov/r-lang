module profile.hosted_std_fs;

protected bool absolute(const std.fs::path* value) {
    return std.fs::path_is_absolute(value);
}

i32 main() {
    return 0;
}
