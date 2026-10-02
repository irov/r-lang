module test.codegen.async_detach;

protected
async i32 child() {
    return 7;
}

async i32 main() {
    try {
        task<i32> operation = child();
        std.async::detach(move operation);
    } catch (std.async::start_error error) {
        error as void;
        return 1;
    }
    return 0;
}
