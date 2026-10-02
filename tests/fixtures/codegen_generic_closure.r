module test.codegen.generic_closure;
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }
error Invalid { i32 value; };
@generic<F: fn once(T) -> T, T>
T invoke(F operation, T argument) {
    T result = (move operation).call(move argument);
    return move result;
}
@generic<T>
T relay(T value) {
    fn once T local(T argument) { return move argument; }
    T result = invoke(local, move value);
    return move result;
}
@generic<T: copy & unborrowed>
T inspect(T value) {
    fn T local() { return value; }
    T result = local();
    return result;
}
@generic<T: copy & unborrowed>
i32 checked(T value) throws Invalid {
    fn once i32 local() move(value) throws Invalid {
        T copy = value;
        throw Invalid {.value = 42};
    }
    i32 result = local();
    return result;
}
i32 main() {
    try {
        i32 number = relay(42);
        i32 inspected = inspect(number);
        Tracked first = {.data = new i32(inspected)};
        Tracked returned = relay(move first);
        if (*(returned.data) != 42) { throw TestAssertionFailed {.code = 1}; }
        Tracked second = {.data = new i32(7)};
        Tracked again = relay(move second);
        if (*(again.data) != 7) { throw TestAssertionFailed {.code = 2}; }
        try { i32 unexpected = checked(number); throw TestAssertionFailed {.code = 3}; }
        catch (Invalid failure) { i32 selected = failure.value == 42 ? 0 : 4; return selected; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
