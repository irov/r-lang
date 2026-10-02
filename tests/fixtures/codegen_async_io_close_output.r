module test.codegen.async_io_close_output;

async i32 main() {
    std.io::output stream = std.io::stdout();
    try {
        task<void throws std.io::io_error> operation =
            std.io::close_output(move stream, o::none);
        await move operation;
        return 0;
    } catch (std.async::start_error error) {
        std.io::output retained = move stream;
        error as void;
        drop retained;
        return 11;
    } catch (std.io::io_error error) {
        error as void;
        return 10;
    }
}
