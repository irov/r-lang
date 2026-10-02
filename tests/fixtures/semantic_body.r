module semantic.body;

protected i32 add(i32 left, i32 right) {
    i32 sum = left + right;
    return sum;
}

protected bool both(bool left, bool right) {
    return left && right;
}

protected i32 compute(i32 input) {
    i32 value = input;
    value += 2;
    if (value > 10) {
        value = value - 1;
    } else {
        value = -value;
    }
    while (value < 0) {
        value += 1;
    }
    i32 result = add(value, 1);
    return result;
}
