module test.semantic.never_statement;

/* R-TYPE-0029 and R-FUNC-0003: a never expression statement ends a non-void body. */
protected i32 stop(str message) {
    panic(message);
}

i32 main() {
    i32 value = 0;
    if (value == 1) {
        panic("unreachable");
    }
    i32 result = stop("done");
    return result;
}
