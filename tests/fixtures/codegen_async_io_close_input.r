module test.codegen.async_io_close_input;

async i32 main() {
    std.io::input stream = std.io::stdin();
    try {
        task<void throws std.io::io_error> operation =
            std.io::close_input(move stream, o::none);
        await move operation;
        return 0;
    } catch (std.async::start_error error) {
        std.io::input retained = move stream;
        error as void;
        drop retained;
        return 11;
    } catch (std.io::io_error error) {
        error as void;
        return 10;
    }
}
