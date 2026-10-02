module test.await_calls;
import test.await_library;

async i32 main() {
    try {
        try {
            await test.await_library::action();
            i32 value = await test.await_library::combine(1, 2);
            if (value != 12) { throw TestAssertionFailed {.code = 1}; }
            task<i32> pending = test.await_library::combine(3, 4);
            i32 explicit_value = await move pending;
            if (explicit_value != 34) { throw TestAssertionFailed {.code = 2}; }
            i32 copied = await test.await_library::identity(move value);
            if (copied != 12 || value != 12) { throw TestAssertionFailed {.code = 3}; }
            own i32* source = new i32(17);
            own i32* result = await test.await_library::relay(move source);
            if (*result != 17) { throw TestAssertionFailed {.code = 4}; }
            i32 sequence = await test.await_library::factory();
            task<i32> next_operation = test.await_library::factory();
            i32 next_sequence = await move next_operation;
            if (sequence != 1 || next_sequence != 2) { throw TestAssertionFailed {.code = 7}; }
            i32 iterations = 0;
            while (iterations < 3) {
                i32 step = await test.await_library::identity(1);
                iterations += step;
            }
            if (iterations != 3) { throw TestAssertionFailed {.code = 8}; }
            // Standard calls share the same operand and start-error checks.
            std.io::output output = std.io::stdout();
            await std.io::flush(&output, o::none);
        } catch (std.io::io_error error) {
            throw TestAssertionFailed {.code = 6};
        } catch (std.async::start_error error) {
            throw TestAssertionFailed {.code = 5};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
