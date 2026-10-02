module test.codegen.async_generic_static;

@generic<T: send & unborrowed>
async i32 count(T value) {
    static i32 calls = 0;
    unsafe {
        calls += 1;
        return calls;
    }
}

@generic<T>
usize size(T value) {
    static const usize bytes = sizeof(T);
    return bytes;
}

@generic<T>
usize alignment(T value) { return alignof(T); }

struct Padded { u8 flag; i32 value; };

async i32 main() {
    try {
        try {
            task<i32> first_task = count(7);
            i32 first = await move first_task;
            task<i32> second_task = count(11);
            i32 second = await move second_task;
            task<i32> third_task = count(true);
            i32 third = await move third_task;
            if (first != 1 || second != 2 || third != 1) { throw TestAssertionFailed {.code = 1}; }
        } catch (std.async::start_error error) { error as void; throw TestAssertionFailed {.code = 99}; }
        usize integer_size = size(1);
        usize bool_size = size(true);
        Padded padded = Padded { .flag = 1, .value = 2 };
        usize padded_size = size(padded);
        usize padded_align = alignment(padded);
        if (integer_size != 4 || bool_size != 1 || padded_size != 8 || padded_align != 4) { throw TestAssertionFailed {.code = 2}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
