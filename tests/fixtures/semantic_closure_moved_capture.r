module test.semantic.closure_moved_capture;

/* R-FUNC-0016: a moved capture consumes the local of the enclosing body. */
i32 main() {
    own i32* boxed = new i32(8);
    fn i32 unbox() move(boxed) { i32 value = *boxed; return value; }
    i32 first = unbox();
    i32 second = *boxed;
    return first + second;
}
