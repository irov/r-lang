module test.codegen.async_core_atomic_invalid_order;

async void update(core::memory_order failure_order) {
    ai32 value = 7;
    core::atomic_compare_exchange(
        &value,
        7i32,
        9i32,
        core::memory_order::relaxed,
        failure_order) as void;
}

async i32 main() {
    try {
        await update(core::memory_order::acquire);
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
    return 1;
}
