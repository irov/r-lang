module test.codegen.scoped_composition;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };
struct TestStorage2 { i32 value; };
async void pause() {}
@generic<T: copy & send & sync & unborrowed>
@scoped
async T read(const T* value) throws std.async::start_error {
    await pause();
    return *value;
}
@generic<T: copy & send & sync & unborrowed>
async T copy_scoped(T value) throws std.async::start_error {
    task_scope(1) group { return await read(&value); }
}
async i32 choose(bool reverse) throws std.async::start_error {
    TestStorage1 storage_left = {.value = 20};
    TestStorage2 storage_right = {.value = 22};
    task_scope(2) group {
        auto first = read(&storage_left.value);
        auto second = read(&storage_right.value);
        usize selected = reverse == true ? await group.first(&second, &first)
                                         : await group.first(&first, &second);
        if (selected > 1usize) { return 1; }
        await group.all();
        storage_left.value = 0;
        storage_right.value = 0;
        return await move first + await move second;
    }
}
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
error Failed { Owner payload; };
async void rejected(Owner owner) throws Failed { throw Failed {.payload = move owner}; }
async i32 main() {
    try {
        try {
            if (await copy_scoped(42) != 42) { throw TestAssertionFailed {.code = 7}; }
            if (await choose(false) != 42 || await choose(true) != 42) { throw TestAssertionFailed {.code = 2}; }
            i32 finalized = 0;
            task_scope(1) outer {
                try {
                    task_scope(1) inner {
                        await rejected(Owner {.value = new i32(42)});
                    }
                    throw TestAssertionFailed {.code = 3};
                } catch (Failed failure) {
                    if (*failure.payload.value != 42) { throw TestAssertionFailed {.code = 4}; }
                } finally { finalized += 1; }
            }
            i32 iterations = 0;
            while (iterations < 3) {
                task_scope(1) loop {
                    await pause();
                    iterations += 1;
                    if (iterations == 1) { continue; }
                    if (iterations == 2) { break; }
                }
            }
            i32 chosen = finalized == 1 && iterations == 2 ? 0 : 5;
            return chosen;
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 6}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
