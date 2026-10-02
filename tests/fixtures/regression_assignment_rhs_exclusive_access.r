module regression.assignment_rhs_exclusive_access;

protected i32 write(i32* target) {
    *target = 2;
    return 3;
}

i32 main() {
    i32 value = 1;
    value = write(&value);
    return value;
}
