module audit.std_c_raw_fn_modules;
import audit.std_c_callback_module::{Box, identity, increment, choose, invoke};

i32 main() {
    try {
        raw fn(c_int) -> c_int callback = increment;
        raw fn(c_int) -> c_int copied = identity(move callback);
        Box<raw fn(c_int) -> c_int> box = Box<raw fn(c_int) -> c_int> { .value = copied };
        Box<raw fn(c_int) -> c_int> other = identity(move box);
        raw fn(c_int) -> c_int selected = choose();
        unsafe {
            if (callback(41i32 as c_int) != 42i32 as c_int ||
                box.value(41i32 as c_int) != 42i32 as c_int ||
                other.value(41i32 as c_int) != 42i32 as c_int) { throw TestAssertionFailed {.code = 1}; }
        }
        if (invoke(selected, 41i32 as c_int) != 42i32 as c_int) { throw TestAssertionFailed {.code = 2}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
