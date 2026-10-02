module profile.hosted_thread_ok;

i32 main() {
    thread_scope {
    }
    std.thread::yield_now();
    return 0;
}
