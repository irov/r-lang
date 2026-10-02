module test.semantic.consteval_runtime_callee;

/* R-FUNC-0023: reading thread-local storage keeps the function a run-time function. */
thread_local usize width = 4;

usize current_width() {
    return width;
}

i32 main() {
    u8[current_width()] bytes = {};
    bytes as void;
    return 0;
}
