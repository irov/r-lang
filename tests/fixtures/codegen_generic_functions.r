module test.codegen.generic_functions;

@generic<T>
T identity(T value) { return move value; }

@generic<T: copy>
T copied(T value) { return value; }

@generic<T>
void consume(T value) { }

@generic<T>
struct Box { T value; };

@generic<T>
Box<T> boxed(T value) { return Box<T> { .value = move value }; }

@generic<T>
T repeated(T value, i32 remaining) {
    i32 left = remaining;
    while (left > 0) { left = left - 1; }
    return move value;
}

i32 main() {
    try {
        i32 value = 7;
        i32 first = identity(move value);
        i32 second = identity(11);
        bool flag = copied(true);
        if (value != 7 || first != 7 || second != 11 || flag != true) { throw TestAssertionFailed {.code = 1}; }
        Box<i32> box = boxed(13);
        Box<i32> other = identity(move box);
        if (box.value != 13 || other.value != 13) { throw TestAssertionFailed {.code = 2}; }
        own i32* owner = new i32(17);
        own i32* moved = identity(move owner);
        if (*moved != 17) { throw TestAssertionFailed {.code = 3}; }
        consume(move moved);
        consume(move value);
        if (value != 7) { throw TestAssertionFailed {.code = 4}; }
        own i32* repeated_owner = new i32(41);
        own i32* returned = repeated(move repeated_owner, 5);
        if (*returned != 41 || repeated(true, 4) != true) { throw TestAssertionFailed {.code = 5}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
