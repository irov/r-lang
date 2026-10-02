module test.codegen.sync_async_start_user;

error child_error {
    i32 code;
};

protected async void child_without_arguments() {
}

protected async usize child_with_owned_buffer(array<u8> buffer) throws child_error {
    usize size = len(buffer);
    return size;
}

void start_without_arguments() {
    try {
        task<void> operation = child_without_arguments();
        std.async::detach(move operation);
    } catch (std.async::start_error error) {
        error as void;
    }
}

usize start_with_owned_buffer(array<u8> buffer) {
    try {
        task<usize throws child_error> operation = child_with_owned_buffer(move buffer);
        std.async::cancel(move operation);
        return 0;
    } catch (std.async::start_error error) {
        usize retained_size = len(buffer);
        error as void;
        drop buffer;
        return retained_size;
    }
}

i32 main() {
    return 0;
}
