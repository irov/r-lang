module test.semantic.async_fs_create_directory_beneath_negative;

async i32 invalid_create_directory(
    arc std.fs::directory root,
    std.fs::path relative) throws std.async::start_error {
    const std.fs::directory* root_view = &*root;
    task<void throws std.fs::fs_error> started =
        std.fs::create_directory_beneath(root_view, &relative);
    std.async::cancel(move started);
    return 0;
}
