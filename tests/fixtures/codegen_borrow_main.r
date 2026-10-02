module codegen.borrow_main;

import codegen.borrow_api::{Cell, identity, increment, read, read_item, sum};

i32 main() {
    try {
        Cell cell = {
            .value = 20,
        };
        increment(&cell.value);
        i32 doubled = sum(&cell, &cell);
        i32 observed = read(&cell);
        i32 default_item = read_item(&cell, 0);
        const i32* alias = identity(&cell.value);
        if ((((doubled == 42) && (observed == 21)) && (default_item == 0)) && (*alias == 21)) {
            return 0;
        } else {
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
