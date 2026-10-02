module test.semantic.async_io_write_all;

async i32 write_buffer(std.io::output output, array<u8> buffer) {
    try {
        task<std.io::write_all_result> operation =
            std.io::write_all(&output, move buffer, o::none);
        std.io::write_all_result completed = await move operation;
        return 0;
    } catch (std.async::start_error error) {
        array<u8> recovered = move buffer;
        error as void;
        return 1;
    }
}
