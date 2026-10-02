module audit.std_time_sleep;

async i32 main() {
    try {
        await std.time::sleep_for(std.time::duration_from_seconds(0));
    } catch (std.async::start_error failure) {
        failure as void;
        return 1;
    } catch (std.time::time_error failure) {
        failure as void;
        return 2;
    }

    try {
        std.time::instant now = std.time::monotonic_now();
        task<void throws std.time::time_error> operation = std.time::sleep_until(now);
        await move operation;
    } catch (std.async::start_error failure) {
        failure as void;
        return 3;
    } catch (std.time::time_error failure) {
        failure as void;
        return 4;
    }

    try {
        await std.time::sleep_for(
            std.time::duration_from_seconds(9223372036854775807));
        return 5;
    } catch (std.async::start_error failure) {
        failure as void;
        return 6;
    } catch (std.time::time_error failure) {
        failure as void;
    }
    return 0;
}
