module test.codegen.async_generic_vector;
@generic<T>
struct Vector { array<T> storage; };
@generic<T: send & unborrowed>
async Vector<T> vector(T value) throws std.alloc::alloc_error, std.array::push_error<T> {
    array<T> storage = std.array::with_capacity::<T>(1usize);
    std.array::push(&storage, move value);
    return Vector<T> { .storage = move storage };
}
@generic<T: copy>
T first(const T[] view) { return view[0]; }
@generic<T: copy>
T read(const T* view) { return *view; }
async i32 main() {
    try {
        try {
            own i32* owner = new i32(17);
            task<Vector<own i32*> throws std.alloc::alloc_error, std.array::push_error<own i32*>> pending = vector(move owner);
            Vector<own i32*> a = await move pending;
            task<Vector<i32> throws std.alloc::alloc_error, std.array::push_error<i32>> numbers = vector(23);
            Vector<i32> b = await move numbers;
            if (len(a.storage) != 1 || len(b.storage) != 1) { throw TestAssertionFailed {.code = 3}; }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 99}; }
          catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 4}; }
          catch (std.array::push_error<own i32*> failure) { throw TestAssertionFailed {.code = 5}; }
          catch (std.array::push_error<i32> failure) { throw TestAssertionFailed {.code = 6}; }
        try {
            own i32* owner = new i32(29);
            task<Vector<own i32*> throws std.alloc::alloc_error, std.array::push_error<own i32*>> pending = vector(move owner);
            Vector<own i32*> unexpected = await move pending;
            throw TestAssertionFailed {.code = 7};
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 99}; }
          catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 8}; }
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
