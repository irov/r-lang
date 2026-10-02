module profile.hosted_std_thread;

i32 main() {
    std.thread::yield_now();
    return 0;
}
