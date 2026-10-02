module test.codegen.async_cancel;

protected async i32 produce_value() {
    return 37;
}

protected async void produce_void() {
    return;
}

async i32 main() {
    try {
        task<i32> value_task = produce_value();
        std.async::cancel(move value_task);
    } catch (std.async::start_error error) {
        error as void;
        return 1;
    }

    try {
        task<void> void_task = produce_void();
        std.async::cancel(move void_task);
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }

    return 0;
}
