module test.codegen.sync_async_start_std;

void start_fs_read_file(const std.fs::path* path) {
    try {
        task<array<u8> throws std.fs::fs_error> operation =
            std.fs::read_file(path, 1048576, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        error as void;
    }
}

void start_fs_open_directory(const std.fs::path* path) {
    try {
        task<std.fs::directory throws std.fs::fs_error> operation =
            std.fs::open_directory(path, o::none);
        std.async::detach(move operation);
    } catch (std.async::start_error error) {
        error as void;
    }
}

void start_fs_create_directory_beneath(const std.fs::directory* root,
                                       const std.fs::path* relative) {
    try {
        task<void throws std.fs::fs_error> operation =
            std.fs::create_directory_beneath(root, relative, true, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        error as void;
    }
}

void start_fs_write_file_atomic_no_replace_beneath(const std.fs::directory* root,
                                                   const std.fs::path* relative,
                                                   array<u8> data) {
    try {
        task<std.fs::write_file_result> operation =
            std.fs::write_file_atomic_no_replace_beneath(root, relative, move data, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        usize retained_size = len(data);
        retained_size as void;
        error as void;
        drop data;
    }
}

void start_io_read(const std.io::input* stream, array<u8> buffer) {
    try {
        task<std.io::read_result> operation = std.io::read(stream, move buffer, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        usize retained_size = len(buffer);
        retained_size as void;
        error as void;
        drop buffer;
    }
}

void start_io_write(const std.io::output* stream, array<u8> buffer) {
    try {
        task<std.io::write_result> operation = std.io::write(stream, move buffer, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        usize retained_size = len(buffer);
        retained_size as void;
        error as void;
        drop buffer;
    }
}

void start_io_write_all(const std.io::output* stream, array<u8> buffer) {
    try {
        task<std.io::write_all_result> operation =
            std.io::write_all(stream, move buffer, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        usize retained_size = len(buffer);
        retained_size as void;
        error as void;
        drop buffer;
    }
}

void start_io_write_shared(const std.io::output* stream, arc (array<u8>) buffer) {
    try {
        task<std.io::shared_write_result> operation =
            std.io::write_shared(stream, move buffer, 0, 0, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        error as void;
        drop buffer;
    }
}

void start_io_close_input(std.io::input stream) {
    try {
        task<void throws std.io::io_error> operation =
            std.io::close_input(move stream, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        error as void;
        drop stream;
    }
}

void start_io_close_output(std.io::output stream) {
    try {
        task<void throws std.io::io_error> operation =
            std.io::close_output(move stream, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        error as void;
        drop stream;
    }
}

void start_io_flush(const std.io::output* stream) {
    try {
        task<void throws std.io::io_error> operation = std.io::flush(stream, o::none);
        std.async::cancel(move operation);
    } catch (std.async::start_error error) {
        error as void;
    }
}

i32 main() {
    return 0;
}
