module test.semantic.async_fs_create_directory_beneath;

async i32 create_directory(
    arc std.fs::directory root,
    std.fs::path relative) {
    const std.fs::directory* root_view = &*root;
    try {
        task<void throws std.fs::fs_error> operation = std.fs::create_directory_beneath(
            root_view,
            &relative,
            true,
            o::none);
        await move operation;
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    } catch (std.fs::fs_error error) {
        error as void;
        return 3;
    }
}
