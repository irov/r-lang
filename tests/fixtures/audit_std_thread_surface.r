module audit.std_thread_surface;

constexpr str panic_category(const std.thread::panic_report* report) {
    return std.thread::panic_category(report);
}

str panic_text(const std.thread::panic_report* report) {
    return std.thread::panic_text(report);
}

i32 sync_probe() {
    std.thread::thread current = std.thread::current();
    std.thread::thread duplicate = std.thread::clone_thread(&current);
    std.thread::unpark(&current);
    std.thread::park();
    std.thread::yield_now();
    std.thread::sleep_nanoseconds(0u64);
    drop duplicate;
    drop current;
    return 0;
}

async i32 main() {
    if (sync_probe() != 0) {
        return 1;
    }
    std.thread::thread current = std.thread::current();
    std.thread::thread duplicate = std.thread::clone_thread(&current);
    std.thread::unpark(&current);
    std.thread::park();
    std.thread::yield_now();
    std.thread::sleep_nanoseconds(0u64);
    drop duplicate;
    drop current;
    return 0;
}
