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

// tests/codegen_sync_async_start_user_wrapper.c refuses the frame allocation of the first start
// and stops the executor before the second; each start then fails and the buffer it named stays
// with the caller (Core R-FUNC-0010).
i32 main() {
    try {
        array<u8> first = std.array::filled(1usize, 17u8);
        if (start_with_owned_buffer(move first) != 1usize) { return 1; }
        array<u8> second = std.array::filled(1usize, 29u8);
        if (start_with_owned_buffer(move second) != 1usize) { return 2; }
    } catch (std.alloc::alloc_error failure) { return 3; }
    return 0;
}
