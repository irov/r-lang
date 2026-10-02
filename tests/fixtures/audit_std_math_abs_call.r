module audit.std_math_abs_call;

i32 main() {
    f64 value = std.math::abs_f64(-2.0);
    i32 selected = value == 2.0 ? 0 : 1;
    return selected;
}
