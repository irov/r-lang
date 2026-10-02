module profile.hosted_thread_sleep;

i32 main() {
    std.time::duration pause = std.time::duration_from_seconds(1i64);
    std.time::sleep_for(pause);
    return 0;
}
