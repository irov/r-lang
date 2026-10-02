module test.codegen.async_fs_read_file;

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 2) {
        return 10;
    }
    str input = args[1];
    try {
        std.fs::path path = std.fs::path_from_utf8(input);
        task<array<u8> throws std.fs::fs_error> operation =
            std.fs::read_file(&path, 1048576, o::none);
        array<u8> bytes = await move operation;
        usize byte_count = len(bytes);
        if (byte_count == 0) {
            return 14;
        }
        return 0;
    } catch (std.fs::path_error error) {
        error as void;
        return 11;
    } catch (std.async::start_error error) {
        error as void;
        return 12;
    } catch (std.fs::fs_error error) {
        error as void;
        return 13;
    }
}
