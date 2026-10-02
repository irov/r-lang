module test.codegen.generic_static;

@generic<T>
@safety("test.generic.count", "Calls to this counter are serialized")
unsafe i32 count(T value) {
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

i32 main() {
    try {
        unsafe {
            i32 first = count(7);
            i32 second = count(11);
            i32 third = count(true);
            if (first != 1 || second != 2 || third != 1) { throw TestAssertionFailed {.code = 1}; }
        }
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
