module audit.std_time_duration_call;

i32 main() {
    std.time::duration value = std.time::duration_from_seconds(1);
    value as void;
    return 0;
}
