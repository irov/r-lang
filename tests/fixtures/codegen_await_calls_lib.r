module test.await_library;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

async void action() {}

async i32 combine(i32 first, i32 second) {
    return first * 10 + second;
}

@generic<T: send & unborrowed>
async T identity(T value) {
    return move value;
}

@generic<T: send & unborrowed>
async T relay(T value) throws std.async::start_error {
    T result = await identity(move value);
    return move result;
}

// A synchronous task factory makes repeated call evaluation observable.
task<i32> factory() throws std.async::start_error {
    static i32 calls = 0;
    TestStorage1 storage_sequence = {.value = 0};
    unsafe {
        calls += 1;
        storage_sequence.value = calls;
    }
    task<i32> operation = identity(storage_sequence.value);
    return move operation;
}
