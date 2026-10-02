module test.codegen.async_fs_create_directory_beneath;

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 3) {
        return 30;
    }
    try {
        str root_text = args[1];
        std.fs::path root_path = std.fs::path_from_utf8(root_text);
        str relative_text = args[2];
        std.fs::path relative = std.fs::path_from_utf8(relative_text);
        task<std.fs::directory throws std.fs::fs_error> open_operation =
            std.fs::open_directory(&root_path, o::none);
        std.fs::directory directory = await move open_operation;
        arc std.fs::directory root = new arc std.fs::directory(move directory);
        const std.fs::directory* root_view = &*root;
        task<void throws std.fs::fs_error> create_operation =
            std.fs::create_directory_beneath(root_view, &relative, true, o::none);
        await move create_operation;
        return 0;
    } catch (std.fs::path_error error) {
        error as void;
        return 31;
    } catch (std.async::start_error error) {
        error as void;
        return 33;
    } catch (std.fs::fs_error error) {
        error as void;
        return 36;
    }
}
