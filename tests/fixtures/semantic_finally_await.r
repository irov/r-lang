module test.semantic.finally_await;

protected async void ready() {
}

protected async void invalid_await() throws std.async::start_error {
    task<void> operation = ready();
    try {
        ;
    } finally {
        await move operation;
    }
}
