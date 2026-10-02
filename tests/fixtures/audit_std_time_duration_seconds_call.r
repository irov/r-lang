module audit.std_time_duration_seconds_call;

i32 main() {
    std.time::duration value = std.time::duration_from_seconds(7);
    if (std.time::duration_seconds(value) != 7) {
        return 1;
    }
    if (std.time::duration_nanoseconds(value) != 0u32) {
        return 2;
    }

    try {
        std.time::duration fractional = std.time::duration_from_parts(2, 3u32);
        if ((std.time::duration_seconds(fractional) != 2) ||
            (std.time::duration_nanoseconds(fractional) != 3u32)) {
            return 3;
        }

        std.time::duration sum = std.time::duration_add(value, fractional);
        if ((std.time::duration_seconds(sum) != 9) ||
            (std.time::duration_nanoseconds(sum) != 3u32)) {
            return 4;
        }

        std.time::duration difference = std.time::duration_sub(sum, fractional);
        if (std.time::duration_compare(difference, value) != 0) {
            return 5;
        }

        std.time::duration product = std.time::duration_multiply(fractional, 3);
        if ((std.time::duration_seconds(product) != 6) ||
            (std.time::duration_nanoseconds(product) != 9u32)) {
            return 6;
        }
    } catch (std.time::duration_error failure) {
        failure as void;
        return 7;
    }

    try {
        std.time::duration invalid = std.time::duration_from_parts(0, 1000000000u32);
        invalid as void;
        return 8;
    } catch (std.time::duration_error failure) {
        failure as void;
    }

    try {
        std.time::duration zero = std.time::duration_from_seconds(0);
        std.time::instant before = std.time::monotonic_now();
        std.time::instant after = std.time::instant_add(before, zero);
        std.time::duration elapsed = std.time::instant_duration(after, before);
        if (std.time::duration_compare(elapsed, zero) != 0) {
            return 9;
        }
        std.time::system_time now = std.time::system_now();
        std.time::system_time shifted = std.time::system_add(now, zero);
        std.time::utc_datetime utc = std.time::to_utc(shifted);
        std.time::system_time roundtrip = std.time::from_utc(utc);
        roundtrip as void;
    } catch (std.time::time_error failure) {
        std.error::error generic = std.time::as_error(failure);
        generic as void;
        return 10;
    }
    return 0;
}
