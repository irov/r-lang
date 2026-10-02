module test.regression.time_opaque_duration_field;
i32 main() {
    std.time::duration duration = std.time::duration_from_seconds(1i64);
    i64 seconds = duration.seconds;
    return seconds as i32;
}
