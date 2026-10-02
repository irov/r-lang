module audit.float_compound_assignment;

i32 main() {
    f64 value = 1.5;
    value += 2.25;
    value -= 0.75;
    value *= 2.0;
    value /= 4.0;
    f32 amount = 0.5f32;
    value += amount;
    value += 2;
    i32 value_status = value == 4.0f64 ? 0 : 1;
    if (value_status != 0) { return 1; }

    f32 small = 8.0f32;
    small /= 2.0f32;
    small += 1.5f32;
    small *= 2.0f32;
    small -= 3.0f32;
    i32 one = 1;
    small += one;
    i32 selected = small == 9.0f32 ? 0 : 2;
    return selected;
}
