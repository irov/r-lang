module test.codegen.checked_conversion;

i32 main() {
    try {
        i32 value = std.convert::checked_i32(42u32);
        if (value != 42) {
            return 1;
        }
    } catch (std.convert::range_error error) {
        error as void;
        return 2;
    }

    try {
        i8 value = std.convert::checked_i8(128u16);
        value as void;
        return 3;
    } catch (std.convert::range_error error) {
        error as void;
    }

    try {
        c_int value = std.c::checked_c_int(-7i32);
        i32 restored_value = std.convert::checked_i32(value);
        if (restored_value != -7) {
            return 4;
        }
    } catch (std.convert::range_error error) {
        error as void;
        return 6;
    }

    try {
        c_bool value = std.c::checked_c_bool(2u8);
        value as void;
        return 7;
    } catch (std.convert::range_error error) {
        error as void;
    }

    try {
        f32 floating_value = std.convert::checked_f32(0u8);
        c_long_double value = std.c::checked_c_long_double(floating_value);
        f64 restored_value = std.convert::checked_f64(value);
        restored_value as void;
    } catch (std.convert::range_error error) {
        error as void;
        return 10;
    }

    try {
        i32 value = std.convert::checked_i32(42.0f32);
        if (value != 42) {
            return 11;
        }
    } catch (std.convert::range_error error) {
        error as void;
        return 12;
    }

    try {
        i32 value = std.convert::checked_i32(-7.5f64);
        value as void;
        return 13;
    } catch (std.convert::range_error error) {
        error as void;
    }

    try {
        i32 value = std.convert::checked_i32(2147483648.0);
        value as void;
        return 14;
    } catch (std.convert::range_error error) {
        error as void;
    }

    try {
        c_int value = std.c::checked_c_int(-7.0f32);
        i32 restored_value = std.convert::checked_i32(value);
        if (restored_value != -7) {
            return 15;
        }
    } catch (std.convert::range_error error) {
        error as void;
        return 17;
    }

    return 0;
}
