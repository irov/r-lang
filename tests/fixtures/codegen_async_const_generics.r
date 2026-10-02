module test.codegen.async_const_generics;
@generic<const usize N>
struct Buffer { u8[N] data; };
@generic<const usize N>
async Buffer<N> relay(Buffer<N> value) {
    const usize count = N;
    u8[count] scratch = {};
    usize capacity = len(scratch);
    if (capacity != N) { panic("wrong closed frame layout"); }
    usize position = 0usize;
    u8[2usize][N] rows = {};
    usize row_width = len(rows[position]);
    if (position != 0usize || row_width != N) { panic("wrong closed row length"); }
    return value;
}
@generic<const usize N>
usize width(Buffer<N> value) { return N; }
async i32 main() {
    try {
        Buffer<4usize> original = {.data={10u8, 20u8, 30u8, 40u8}};
        try {
            Buffer<2usize + 2usize> result = await relay(original);
            if (original.data[3] != 40u8 || result.data[3] != 40u8) { throw TestAssertionFailed {.code = 1}; }
            std.thread::join_handle<usize> worker = std.thread::spawn(width, result);
            std.thread::join_result<usize> completion = (move worker).join();
            switch (move completion) {
            case variant std.thread::join_result::returned(move value): i32 selected = value == 4usize ? 0 : 2; return selected;
            case variant std.thread::join_result::panicked(move report): throw TestAssertionFailed {.code = 3};
            }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 4}; }
          catch (std.thread::thread_error failure) { throw TestAssertionFailed {.code = 5}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
