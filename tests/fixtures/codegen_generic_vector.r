module test.codegen.generic_vector;
@generic<T>
struct Vector { array<T> storage; };
@generic<T>
Vector<T> vector(T value) throws std.alloc::alloc_error, std.array::push_error<T> {
    array<T> storage = std.array::with_capacity::<T>(1usize);
    std.array::push(&storage, move value);
    return Vector<T> { .storage = move storage };
}
@generic<T: copy>
T first(const T[] view) { return view[0]; }
@generic<T: copy>
T read(const T* view) { return *view; }
i32 main() {
    try {
        i32[2] values = {7, 9};
        if (first(values) != 7) { throw TestAssertionFailed {.code = 1}; }
        i32 x = 12;
        i32* pointer = &x;
        if (read(pointer) != 12) { throw TestAssertionFailed {.code = 2}; }
        try {
            own i32* owner = new i32(17);
            Vector<own i32*> a = vector(move owner);
            Vector<i32> b = vector(23);
            if (len(a.storage) != 1 || len(b.storage) != 1) { throw TestAssertionFailed {.code = 3}; }
        } catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 4}; }
          catch (std.array::push_error<own i32*> failure) { throw TestAssertionFailed {.code = 5}; }
          catch (std.array::push_error<i32> failure) { throw TestAssertionFailed {.code = 6}; }
        try {
            own i32* owner = new i32(29);
            Vector<own i32*> unexpected = vector(move owner);
            throw TestAssertionFailed {.code = 7};
        } catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 8}; }
          catch (std.array::push_error<own i32*> failure) {
            switch (move failure) {
                case variant std.array::push_error::allocation_failed(move payload):
                    if (*(payload.value) != 29) { throw TestAssertionFailed {.code = 9}; }
                    return 0;
            }
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}


// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
