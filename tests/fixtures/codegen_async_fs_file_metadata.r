module test.codegen.async_fs_file_metadata;

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 2) {
        return 60;
    }
    try {
        str input = args[1];
        std.fs::path path = std.fs::path_from_utf8(input);
        std.fs::open_file_options options = std.fs::open_file_options {
            .access = std.fs::access::read,
            .create = std.fs::create_mode::existing,
            .truncate = false,
            .append = false,
            .follow_final_symlink = true,
        };
        std.fs::file file = await std.fs::open_file(&path, options, o::none);
        std.fs::metadata metadata = await std.fs::file_metadata(&file, o::none);
        metadata as void;
        return 0;
    } catch (std.fs::path_error error) {
        error as void;
        return 61;
    } catch (std.async::start_error error) {
        error as void;
        return 62;
    } catch (std.fs::fs_error error) {
        error as void;
        return 63;
    }
}
