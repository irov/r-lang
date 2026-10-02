module test.codegen.scoped_start_recovery;
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
async i32 value() { return 10; }
async i32 consume(Owner owner) { return *owner.value; }
async i32 main() {
    try {
        Owner saved = Owner {.value = new i32(42)};
        i32 failures = 0;
        try {
            task_scope(1) group {
                auto first = value();
                try {
                    auto rejected = consume(move saved);
                    i32 unused = await move rejected;
                    throw TestAssertionFailed {.code = 1};
                } catch (std.async::start_error failure) {
                    std.error::error code = std.error::from_async(failure);
                    if (*saved.value != 42 || code.code != 2u32) { throw TestAssertionFailed {.code = 2}; }
                    failures += 1;
                }
                try {
                    auto rejected = consume(Owner {.value = new i32(7)});
                    i32 unused = await move rejected;
                    throw TestAssertionFailed {.code = 8};
                } catch (std.async::start_error failure) { failures += 1; }
                i32 initial = await move first;
                if (initial != 10) { throw TestAssertionFailed {.code = 3}; }
                try {
                    auto rejected = consume(move saved);
                    i32 unused = await move rejected;
                    throw TestAssertionFailed {.code = 4};
                } catch (std.async::start_error failure) {
                    if (*saved.value != 42) { throw TestAssertionFailed {.code = 5}; }
                    failures += 1;
                }
                i32 result = await consume(move saved);
                i32 selected = result == 42 && failures == 3 ? 0 : 6;
                return selected;
            }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 7}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
