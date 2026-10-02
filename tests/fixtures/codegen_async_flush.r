module test.codegen_async_flush;

async i32 main() {
    std.io::output stream = std.io::stderr();
    try {
        task<void throws std.io::io_error> operation = std.io::flush(&stream, o::none);
        await move operation;
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    } catch (std.io::io_error error) {
        error as void;
        return 3;
    }
}
