module test.codegen.generic_constants;
struct Padded { u8 small; u64 large; };
@generic<T>
struct generic { T value; };
generic<Padded> contextual(generic<Padded> value) { return value; }
@generic<T: pod>
T pod_copy(T value) { return move value; }
@generic<T>
struct Storage { u8[sizeof(T) + alignof(T)] bytes; };
@generic<T>
usize capacity(T value) {
    Storage<T> storage = {};
    const usize width = sizeof(T);
    u8[width] local = {};
    if (len(local) != sizeof(T)) { return 0; }
    usize length = len(storage.bytes);
    return length;
}
@generic<T: pod>
usize optional_capacity(o<T> token) {
    Storage<T> storage = {};
    usize length = len(storage.bytes);
    return length;
}
i32 main() {
    try {
        Padded padded = Padded { .small = 1, .large = 2 };
        generic<Padded> generic_value = generic<Padded> { .value = padded };
        generic<Padded> contextual_value = contextual(generic_value);
        Padded copied = pod_copy(contextual_value.value);
        bool flag = pod_copy(true);
        if (copied.large != 2 || flag == false) { throw TestAssertionFailed {.code = 4}; }
        if (capacity(padded) != sizeof(Padded) + alignof(Padded)) { throw TestAssertionFailed {.code = 1}; }
        if (capacity(true) != 2) { throw TestAssertionFailed {.code = 2}; }
        i32[2 + 2] values = {1, 2, 3, 4};
        if (len(values) != 4) { throw TestAssertionFailed {.code = 3}; }
        std.hash::sha256_digest digest = std.hash::sha256("generic");
        std.hash::sha256_digest digest_copy = pod_copy(digest);
        if (capacity(digest_copy) != sizeof(std.hash::sha256_digest) + alignof(std.hash::sha256_digest)) { throw TestAssertionFailed {.code = 5}; }
        o<std.time::duration> duration = o::none;
        if (optional_capacity(duration) != sizeof(std.time::duration) + alignof(std.time::duration)) { throw TestAssertionFailed {.code = 6}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
