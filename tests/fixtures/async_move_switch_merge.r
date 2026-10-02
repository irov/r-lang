module semantic.async_move_switch_merge;

protected async void consume(array<u8> payload) {
    return;
}

protected async void success_reaches(array<u8> bytes) {
    try {
        task<void> operation = consume(move bytes);
        await move operation;
    } catch (std.async::start_error error) {
        error as void;
        return;
    }
}

protected async void failure_reaches(array<u8> bytes) {
    try {
        task<void> operation = consume(move bytes);
        await move operation;
        return;
    } catch (std.async::start_error error) {
        error as void;
    }
    array<u8> recovered = move bytes;
}

i32 main() {
    return 0;
}
