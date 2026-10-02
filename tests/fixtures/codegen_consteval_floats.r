module test.codegen.consteval_floats;

/* L22.2 (R-FUNC-0023, R-TYPE-0006): binary32 and binary64 values, their conversions and the
   exact std.math operations during translation, bit for bit as at run time. */

struct Point { f64 x; f64 y; };

f64 scale(f64 x) { return x * 2.5 + 1.0; }

f32[4] ramp(f32 step) {
    f32[4] values = {};
    f32 current = 0.0f32;
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        values[index] = current;
        current += step;
    }
    return values;
}

f64 clamp_unit(f64 value) { return std.math::min_f64(std.math::max_f64(value, 0.0), 1.0); }

f64 horner(f64 x) {
    f64[4] coefficients = {1.0, -3.0, 0.5, 2.0};
    f64 result = 0.0;
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        result = result * x + coefficients[index];
    }
    return result;
}

Point midpoint(Point a, Point b) {
    Point middle = Point {.x = (a.x + b.x) / 2.0, .y = (a.y + b.y) / 2.0};
    return middle;
}

f64 sum_tenths() {
    f64 total = 0.0;
    for (i32 index = 0; index < 10; index += 1) {
        total += 0.1;
    }
    return total;
}

f32 narrow_sum() {
    f32 total = 0.0f32;
    for (i32 index = 0; index < 10; index += 1) {
        total += 0.1f32;
    }
    return total;
}

bool orders() {
    f64 nan = 0.0 / 0.0;
    bool unordered = (nan < 1.0) == false && (nan > 1.0) == false && (nan == nan) == false;
    bool zeros = (0.0 == -0.0) && std.math::sign_bit_f64(-0.0) == true;
    return unordered && zeros;
}

c_double through_c(c_float value) { return (value as c_double) * 2.0 as c_double; }

const f64 SCALED = scale(2.0);
const f32[4] RAMP = ramp(0.25f32);
const f64 FLOORED = std.math::floor_f64(-2.5);
const f64 CEILED = std.math::ceil_f64(-2.5);
const f64 ROUNDED = std.math::round_f64(2.5);
const f64 TRUNCATED_F = std.math::trunc_f64(-2.7);
const f64 MAGNITUDE = std.math::abs_f64(-4.25);
const f64 SIGNED = std.math::copy_sign_f64(3.0, -0.0);
const f64 NEXT = std.math::next_after_f64(1.0, 2.0);
const f64 INFINITE = 1.0 / 0.0;
const bool NAN_UNEQUAL = (0.0 / 0.0) != (0.0 / 0.0);
const f64 CLAMPED = clamp_unit(1.7);
const f64 MIN_NAN = std.math::min_f64(0.0 / 0.0, 2.0);
const f64 POLY = horner(1.5);
const Point MIDDLE = midpoint(Point {.x = 1.0, .y = 2.0}, Point {.x = 4.0, .y = -6.0});
const f64 TENTHS = sum_tenths();
const f32 NARROW_TENTHS = narrow_sum();
const bool ORDERS = orders();
const i32 TRUNCATED = (-7.9 as i32);
const u8 FROM_FLOAT = (200.9 as u8);
const f32 NARROW = (0.1 as f32);
const f64 WIDE = (0.1f32 as f64);
const f64 FROM_INT = (9007199254740993i64 as f64);
const c_double C_DOUBLE = through_c(1.5 as c_float);
const u8[(scale(2.0) as usize)] buffer = {};
const f64 ROOT = std.math::sqrt_f64(2.0);
const f32 ROOT32 = std.math::sqrt_f32(10.0f32);
const f64 REMAINDER = std.math::remainder_f64(7.5, 2.0);

f64 root_or(f64 value, f64 fallback) {
    try {
        return std.math::sqrt_f64(value);
    } catch (std.math::math_error failure) {
        return fallback;
    }
}

const f64 NEGATIVE_ROOT = root_or(-4.0, -1.0);
const f64 ZERO_ROOT = root_or(-0.0, 7.0);

f64 runtime_tenths(i32 count) {
    f64 total = 0.0;
    for (i32 index = 0; index < count; index += 1) {
        total += 0.1;
    }
    return total;
}

f32 runtime_narrow(i32 count) {
    f32 total = 0.0f32;
    for (i32 index = 0; index < count; index += 1) {
        total += 0.1f32;
    }
    return total;
}

i32 main() {
    i32 failures = 0;
    i32 ten = len(buffer) as i32 + 4;
    failures += SCALED == 6.0 ? 0 : 1;
    failures += RAMP[3] == 0.75f32 ? 0 : 1;
    failures += FLOORED == -3.0 && CEILED == -2.0 && ROUNDED == 3.0 ? 0 : 1;
    failures += TRUNCATED_F == -2.0 && MAGNITUDE == 4.25 ? 0 : 1;
    failures += SIGNED == -3.0 && NEXT == 1.0000000000000002 ? 0 : 1;
    failures += std.math::is_infinite_f64(INFINITE) == true && NAN_UNEQUAL == true ? 0 : 1;
    failures += CLAMPED == 1.0 && MIN_NAN == 2.0 ? 0 : 1;
    failures += POLY == horner(1.5 + (ten - 10) as f64) ? 0 : 1;
    failures += MIDDLE.x == 2.5 && MIDDLE.y == -2.0 ? 0 : 1;
    failures += TENTHS == runtime_tenths(ten) && TENTHS != 1.0 ? 0 : 1;
    failures += NARROW_TENTHS == runtime_narrow(ten) ? 0 : 1;
    failures += ORDERS == true ? 0 : 1;
    failures += TRUNCATED == -7 && FROM_FLOAT == 200u8 ? 0 : 1;
    failures += NARROW == 0.1f32 && WIDE == (runtime_narrow(1) as f64) ? 0 : 1;
    failures += FROM_INT == 9007199254740992.0 ? 0 : 1;
    failures += C_DOUBLE == (3.0 as c_double) ? 0 : 1;
    failures += len(buffer) == 6usize ? 0 : 1;
    f64 two = (ten - 8) as f64;
    failures += ROOT == std.math::sqrt_f64(two) && ROOT32 == std.math::sqrt_f32((ten as f32)) ? 0 : 1;
    failures += REMAINDER == -0.5 ? 0 : 1;
    failures += NEGATIVE_ROOT == -1.0 && ZERO_ROOT == 0.0 && std.math::sign_bit_f64(ZERO_ROOT) == true ? 0 : 1;
    return failures;
}
