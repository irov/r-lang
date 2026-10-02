module audit.std_math_sin_call;

i32 main() {
    try {
        f64 value = std.math::sin_f64(0.0);
        if (value == 0.0) {
            return 0;
        }
        return 1;
    } catch (std.math::math_error failure) {
        failure as void;
        return 2;
    }
}
