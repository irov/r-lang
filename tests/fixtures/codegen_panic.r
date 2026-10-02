module test.codegen.explicit_panic;

/* R-ERR-0004: panic(message) accepts str, never returns and begins an explicit panic. */
protected i32 checked(i32 value) {
    if (value < 0) {
        panic("negative input");
    }
    return value;
}

/* R-FUNC-0003: a path of type never needs no return. */
protected i32 fail_with(str message) {
    panic(message);
}

i32 main() {
    i32 first = checked(1);
    if (first != 1) {
        return 1;
    }
    i32 unreachable = fail_with("explicit stop");
    return unreachable;
}
