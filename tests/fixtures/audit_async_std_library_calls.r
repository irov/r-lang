module audit.async_std_library_calls;

async i32 main() {
    try {
        try {
            array<u8> value = std.alloc::bytes(3usize, 7u8);
            const u8[] value_view = std.array::as_slice(&value);
            if ((len(value_view) != 3usize) || (value_view[0] != 7u8) ||
                (value_view[2] != 7u8)) {
                drop value;
                throw TestAssertionFailed {.code = 1};
            }
            drop value;
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 2};
        }

        constexpr str left = "same";
        constexpr str right = "same";
        if (std.bytes::equal(left, right) == false) {
            throw TestAssertionFailed {.code = 3};
        }
        if (std.bytes::compare(left, right) != 0) {
            throw TestAssertionFailed {.code = 7};
        }

        std.c::target_info target = std.c::target();
        if (target.pointer_bits == 0u32) {
            throw TestAssertionFailed {.code = 4};
        }

        try {
            f64 sine = std.math::sin_f64(0.0);
            f32 root = std.math::sqrt_f32(9.0f32);
            if ((sine != 0.0) || (root != 3.0f32)) {
                throw TestAssertionFailed {.code = 5};
            }
        } catch (std.math::math_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 6};
        }

        try {
            std.string::string text = std.string::with_capacity(8usize);
            std.string::reserve(&text, 4usize);
            std.string::append_str(&text, "ab");
            std.string::push_scalar(&text, 'C');
            if ((std.string::len(&text) != 3usize) ||
                (std.string::capacity(&text) < std.string::len(&text))) {
                drop text;
                throw TestAssertionFailed {.code = 8};
            }
            std.string::clear(&text);
            if (std.string::len(&text) != 0usize) {
                drop text;
                throw TestAssertionFailed {.code = 11};
            }
            drop text;
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 16};
        }
        try {
            std.string::string text = std.string::from_utf8("abcd");
            std.string::append_utf8(&text, "ef");
            std.string::truncate(&text, 4usize);
            const u8[] bytes = std.string::as_bytes(&text);
            if ((len(bytes) != 4usize) || (bytes[0] != 97u8) || (bytes[3] != 100u8)) {
                drop text;
                throw TestAssertionFailed {.code = 17};
            }
            std.string::truncate(&text, 5usize);
            drop text;
            throw TestAssertionFailed {.code = 18};
        } catch (std.string::string_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 19};
        } catch (std.string::boundary_error failure) {
            failure as void;
        }
        std.time::duration duration = std.time::duration_from_seconds(-3);
        if (std.time::duration_seconds(duration) != -3) {
            throw TestAssertionFailed {.code = 9};
        }
        if (std.time::duration_nanoseconds(duration) != 0u32) {
            throw TestAssertionFailed {.code = 12};
        }
        try {
            std.time::duration fractional = std.time::duration_from_parts(2, 3u32);
            std.time::duration sum = std.time::duration_add(fractional, fractional);
            std.time::duration difference = std.time::duration_sub(sum, fractional);
            std.time::duration product = std.time::duration_multiply(fractional, 3);
            if ((std.time::duration_compare(difference, fractional) != 0) ||
                (std.time::duration_seconds(product) != 6) ||
                (std.time::duration_nanoseconds(product) != 9u32)) {
                throw TestAssertionFailed {.code = 13};
            }
        } catch (std.time::duration_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 14};
        }
        try {
            std.time::duration invalid = std.time::duration_from_parts(0, 1000000000u32);
            invalid as void;
            throw TestAssertionFailed {.code = 15};
        } catch (std.time::duration_error failure) {
            failure as void;
        }
        try {
            std.time::duration zero = std.time::duration_from_seconds(0);
            std.time::instant before = std.time::monotonic_now();
            std.time::instant after = std.time::instant_add(before, zero);
            std.time::duration elapsed = std.time::instant_duration(after, before);
            if (std.time::duration_compare(elapsed, zero) != 0) {
                throw TestAssertionFailed {.code = 20};
            }
            std.time::system_time now = std.time::system_now();
            std.time::system_time shifted = std.time::system_add(now, zero);
            std.time::utc_datetime utc = std.time::to_utc(shifted);
            std.time::system_time roundtrip = std.time::from_utc(utc);
            roundtrip as void;
        } catch (std.time::time_error failure) {
            std.error::error generic = std.time::as_error(failure);
            generic as void;
            throw TestAssertionFailed {.code = 21};
        }
        if (std.math::abs_f64(-2.0) != 2.0) {
            throw TestAssertionFailed {.code = 10};
        }
        if (std.math::max_f32(2.0f32, 4.0f32) != 4.0f32) {
            throw TestAssertionFailed {.code = 22};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
