module test.async_mir;

async i32 produce(i32 value) {
    return value;
}

protected task<i32> start() throws std.async::start_error {
    task<i32> started = produce(7);
    return move started;
}

async i32 consume(task<i32> work) {
    i32 value = await move work;
    return value;
}

async void drain(task<void> work) {
    await move work;
}
