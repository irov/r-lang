module test.semantic.stack_recursive_mutual;

protected bool is_even(i32 value);
protected bool is_odd(i32 value);

protected bool is_even(i32 value) {
    if (value == 0) {
        return true;
    }
    i32 next = value - 1;
    bool result = is_odd(next);
    return result;
}

protected bool is_odd(i32 value) {
    if (value == 0) {
        return false;
    }
    i32 next = value - 1;
    bool result = is_even(next);
    return result;
}

i32 main() {
    bool even = is_even(4);
    if (even == true) {
        return 0;
    }
    return 1;
}
