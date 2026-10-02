module codegen.recursive;

protected i32 recurse(i32 value);

protected i32 recurse(i32 value) {
    if (value == 0) {
        return 0;
    }
    i32 next = value - 1;
    i32 result = recurse(next);
    return result + 1;
}

i32 main() {
    i32 value = recurse(5);
    if (value == 5) {
        return 0;
    }
    return 1;
}
