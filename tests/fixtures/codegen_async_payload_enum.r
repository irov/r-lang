module test.codegen.async_payload_enum;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };
struct TestStorage2 { i32 value; };
enum Choice {
 Empty, Number(i32), Owner(own i32*),
 Fields { i32 code; own i32* data; }, Last,
};
protected async i32 take(Choice value) {
    switch (move value) {
    case variant Choice::Empty: return 0;
    case variant Choice::Number(number): return *number;
    case variant Choice::Owner(move owner): return *owner;
    case variant Choice::Fields(fields): return (*fields).code;
    case variant Choice::Last: return 99;
    }
}
async i32 main() {
    try {
     Choice c = Choice::Owner(new i32(11));
     switch (c) {
     case variant Choice::Owner(owner): if (**owner != 11) { throw TestAssertionFailed {.code = 1}; } break;
     default: throw TestAssertionFailed {.code = 2};
     }
     TestStorage1 storage_first = {.value = 0};
     try { task<i32> pending = take(move c); i32 returned = await move pending; storage_first.value = returned; }
     catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 101}; }
     Choice d = Choice::Fields {.code=13, .data=new i32(17)};
     TestStorage2 storage_second = {.value = 0};
     try { task<i32> pending = take(move d); i32 returned = await move pending; storage_second.value = returned; }
     catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 102}; }
     if (storage_first.value != 11 || storage_second.value != 13) { throw TestAssertionFailed {.code = 3}; }
     return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
