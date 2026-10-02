module test.codegen.closure_async_body;
error Invalid { i32 value; };
struct Tracked { own i32* value; };
drop(Tracked* self) { *(self->value) = 9; }
async i32 bump(i32 value) { return value + 1; }
@generic<F: async fn() -> i32 throws(Invalid, std.async::start_error) & send & unborrowed>
task<i32 throws Invalid, std.async::start_error> invoke(F operation) throws std.async::start_error {
    task<i32 throws Invalid, std.async::start_error> pending = (move operation).call();
    return move pending;
}
async i32 main() {
    try {
        i32 completed = 0;
        try {
            Tracked first = {.value = new i32(7)};
            async fn i32 accepted() move(first) throws Invalid, std.async::start_error {
                throw (*(first.value) < 0) Invalid {.value = *(first.value)};
                i32 value = await bump(*(first.value));
                return value;
            }
            i32 value = await invoke(move accepted);
            if (value != 8) { throw TestAssertionFailed {.code = 1}; }
            Tracked second = {.value = new i32(-1)};
            async fn i32 rejected() move(second) throws Invalid {
                throw Invalid {.value = *(second.value)};
            }
            i32 ignored = await (move rejected).call();
            throw TestAssertionFailed {.code = 2};
        } catch (Invalid failure) {
            if (failure.value != -1) { throw TestAssertionFailed {.code = 3}; }
            completed = 1;
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 4}; }
          finally { completed += 10; }
        i32 selected = completed == 11 ? 0 : 5;
        return selected;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
