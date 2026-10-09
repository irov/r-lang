module test.codegen.sync_fs_start;

/* Synchronous functions start the tasks of std.fs on a path they borrow from their caller, and
   return or cancel them (R-SLIB-FS). B6-5: the LLVM lowering took the path of such a start for a
   place of the function, while the MIR passes a borrowed path as the first operand. */

protected task<array<u8> throws std.fs::fs_error> start_read(const std.fs::path* path)
    throws std.async::start_error {
    return std.fs::read_file(path, 1048576, o::none);
}

protected task<std.fs::directory throws std.fs::fs_error> start_open(const std.fs::path* path)
    throws std.async::start_error {
    return std.fs::open_directory(path, o::none);
}

protected i32 start_and_cancel(const std.fs::path* path) {
    try {
        task<array<u8> throws std.fs::fs_error> operation = std.fs::read_file(path, 16, o::none);
        std.async::cancel(move operation);
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 1;
    }
}

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 3) {
        return 10;
    }
    str file_text = args[1];
    str directory_text = args[2];
    try {
        std.fs::path file = std.fs::path_from_utf8(file_text);
        std.fs::path directory = std.fs::path_from_utf8(directory_text);
        array<u8> bytes = await start_read(&file);
        usize byte_count = len(bytes);
        std.fs::directory opened = await start_open(&directory);
        drop opened;
        i32 cancelled = start_and_cancel(&file);
        if (byte_count == 0) {
            return 11 + cancelled;
        }
        return cancelled;
    } catch (std.fs::path_error error) {
        error as void;
        return 12;
    } catch (std.async::start_error error) {
        error as void;
        return 13;
    } catch (std.fs::fs_error error) {
        error as void;
        return 14;
    }
}
