module test.codegen.async_discardable;
error Rejected {};
struct Ticket { own i32* data; };
drop(Ticket* self) { *(self->data) = 9; }
@discardable async Ticket acquire(bool accepted) throws Rejected {
    throw (accepted == false) Rejected {};
    return Ticket {.data = new i32(7)};
}
@discardable async i32 checksum() { return 42; }
async i32 main() {
    try {
        i32 finalized = 0;
        try {
            await checksum();
            i32 kept = await checksum();
            if (kept != 42) { throw TestAssertionFailed {.code = 1}; }
            Ticket first = await acquire(true);
            await acquire(true);
            await acquire(false);
            throw TestAssertionFailed {.code = 4};
        } catch (Rejected failure) { finalized = 1; }
          catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 2}; }
          finally { finalized += 10; }
        i32 selected = finalized == 11 ? 0 : 3;
        return selected;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
