module test.codegen.async_fs_write_file_atomic_no_replace_beneath;

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 4) {
        return 40;
    }
    try {
        str input_text = args[1];
        std.fs::path input_path = std.fs::path_from_utf8(input_text);
        str root_text = args[2];
        std.fs::path root_path = std.fs::path_from_utf8(root_text);
        str relative_text = args[3];
        std.fs::path relative = std.fs::path_from_utf8(relative_text);

        task<array<u8> throws std.fs::fs_error> read_operation =
            std.fs::read_file(&input_path, 1048576, o::none);
        array<u8> data = await move read_operation;
        task<std.fs::directory throws std.fs::fs_error> open_operation =
            std.fs::open_directory(&root_path, o::none);
        std.fs::directory directory = await move open_operation;
        arc std.fs::directory root = new arc std.fs::directory(move directory);

        try {
            task<std.fs::write_file_result> write_operation =
                std.fs::write_file_atomic_no_replace_beneath(
                    &*root,
                    &relative,
                    move data,
                    o::none);
            drop root;
            drop relative;
            std.fs::write_file_result write_completed = await move write_operation;
            switch (move write_completed) {
                case variant std.fs::write_file_result::committed(move returned):
                    drop returned;
                    return 0;
                case variant std.fs::write_file_result::failed(move failure):
                    bool exists =
                        failure.error.code == std.fs::error_code::already_exists;
                    drop failure;
                    if (exists == true) {
                        return 0;
                    }
                    return 50;
            }
        } catch (std.async::start_error error) {
            usize retained = len(data);
            error as void;
            if (retained == 0) {
                return 49;
            }
            return 48;
        }
    } catch (std.fs::path_error error) {
        error as void;
        return 41;
    } catch (std.async::start_error error) {
        error as void;
        return 42;
    } catch (std.fs::fs_error error) {
        error as void;
        return 43;
    }
}
