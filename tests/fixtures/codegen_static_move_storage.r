module test.codegen.static_move_storage;

bytes shared_values = {};
thread_local bytes thread_values = {};

struct Storage {
    bytes values;
};

Storage shared_storage = {};
thread_local Storage thread_storage = {};
bytes[2] shared_buffers = {};
thread_local bytes[2] thread_buffers = {};

void touch_storage() throws std.alloc::alloc_error {
    static bytes static_values = {};
    thread_local bytes local_thread_values = {};

    unsafe {
        std.bytes::append_u8(&shared_values, 1);
        std.bytes::append_u8(&static_values, 2);
        std.bytes::append_u8(&shared_storage.values, 5);
        std.bytes::append_u8(&shared_buffers[0], 7);
        std.bytes::append_u8(&shared_buffers[1], 8);
    }
    std.bytes::append_u8(&thread_values, 3);
    std.bytes::append_u8(&local_thread_values, 4);
    std.bytes::append_u8(&thread_storage.values, 6);
    std.bytes::append_u8(&thread_buffers[0], 9);
    std.bytes::append_u8(&thread_buffers[1], 10);
}

i32 main() {
    try {
        touch_storage();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 1;
    }
    return 0;
}
