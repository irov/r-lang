module generic_enum;
@generic<T> error Choice { Empty, Value(T), Fields { T value; }, Last, };
@generic<T: copy>
T read(Choice<T> choice, T fallback) {
    switch (choice) {
    case variant Choice<T>::Empty: return fallback;
    case variant Choice<T>::Value(value): return *value;
    case variant Choice<T>::Fields(fields): return fields->value;
    case variant Choice<T>::Last: return fallback;
    }
}
@generic<T>
enum Value { Some(T), None, };
@generic<T>
T selected(Value<T> source, T fallback) {
    switch (move source) {
        case variant Value<T>::Some(move value): return move value;
        case variant Value<T>::None: return move fallback;
    }
}
i32 main() {
    try {
        Choice<i32> choice = Choice<i32>::Value(7);
        i32 first = read(choice, 0);
        Choice<i32> fields = Choice<i32>::Fields { .value = 11 };
        i32 second = read(fields, 0);
        if (first != 7 || second != 11) { throw TestAssertionFailed {.code = 1}; }
        Value<i32> number = Value<i32>::Some(29);
        if (selected(move number, 0) != 29 || selected(move number, 0) != 29) { throw TestAssertionFailed {.code = 2}; }
        own i32* owner = new i32(31);
        own i32* fallback = new i32(37);
        Value<own i32*> source = Value<own i32*>::Some(move owner);
        own i32* result = selected(move source, move fallback);
        if (*result != 31) { throw TestAssertionFailed {.code = 3}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
