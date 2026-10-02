module test.codegen.payload_enum;
enum Choice {
 Empty, Number(i32), Owner(own i32*),
 Fields { i32 code; own i32* data; }, Last,
};
protected i32 take(Choice value) {
    switch (move value) {
    case variant Choice::Empty: return 0;
    case variant Choice::Number(number): return *number;
    case variant Choice::Owner(move owner): return *owner;
    case variant Choice::Fields(fields): return (*fields).code;
    case variant Choice::Last: return 99;
    }
}
i32 main() {
    try {
     Choice c = Choice::Owner(new i32(11));
     switch (c) {
     case variant Choice::Owner(owner): if (**owner != 11) { throw TestAssertionFailed {.code = 1}; } break;
     default: throw TestAssertionFailed {.code = 2};
     }
     i32 first = take(move c);
     Choice d = Choice::Fields {.code=13, .data=new i32(17)};
     i32 second = take(move d);
     if (first != 11 || second != 13) { throw TestAssertionFailed {.code = 3}; }
     return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
