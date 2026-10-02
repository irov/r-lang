module invalid.condition;

protected i32 broken(i32 value) {
    if (value) {
        return 1;
    }
    return 0;
}
