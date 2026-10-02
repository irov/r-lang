module test.codegen.closure_checked;
error Invalid { i32 value; };
struct Tracked { own i32* value; };
drop(Tracked* self) { *(self->value) = 9; }
@generic<F: fn @noalloc once() -> i32 throws(Invalid)>
@noalloc i32 invoke(F operation) throws Invalid {
    i32 value = (move operation).call();
    return value;
}
i32 main() {
    try {
        i32 completed = 0;
        try {
            Tracked first = {.value = new i32(7)};
            fn @noalloc once i32 accepted() move(first) throws Invalid {
                throw (*(first.value) < 0) Invalid {.value = *(first.value)};
                return *(first.value);
            }
            i32 value = invoke(move accepted);
            if (value != 7) { throw TestAssertionFailed {.code = 1}; }
            Tracked second = {.value = new i32(-1)};
            fn @noalloc once i32 rejected() move(second) throws Invalid {
                throw Invalid {.value = *(second.value)};
            }
            i32 ignored = invoke(move rejected);
            throw TestAssertionFailed {.code = 2};
        } catch (Invalid failure) {
            if (failure.value != -1) { throw TestAssertionFailed {.code = 3}; }
            completed = 1;
        } finally { completed += 10; }
        i32 selected = completed == 11 ? 0 : 4;
        return selected;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
