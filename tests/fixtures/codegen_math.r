module codegen.math;

protected i32 adjust(i32 value) {
    value += 3;
    value -= 1;
    value *= 2;
    value /= 2;
    value %= 17;
    return value;
}

protected i32 unused_helper(i32 value) {
    return value;
}

i32 combine(i32 left, i32 right) {
    i32 sum = left + right;
    i32 adjusted = adjust(sum);
    return adjusted;
}

u32 unsigned_value(u32 value) {
    value += 2;
    value -= 1;
    value *= 3;
    value /= 3;
    value %= 19;
    value *= -1;
    return value;
}

bool logic(bool left, bool right) {
    bool both = left && right;
    bool result = both || !left;
    return result;
}
