module test.codegen.closure_modes;

struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }

@generic<F: fn mut(i32) -> i32>
i32 update(F* operation, i32 value) {
    i32 result = operation(value);
    return result;
}

@generic<F: fn once() -> i32>
i32 consume(F operation) {
    i32 result = (move operation).call();
    return result;
}

i32 main() {
    try {
        i32 total = 10;
        fn mut i32 accumulate(i32 value) move(total) { total += value; return total; }
        i32 first = update(&accumulate, 4);
        i32 second = update(&accumulate, 5);
        if (first != 14 || second != 19 || total != 10) { throw TestAssertionFailed {.code = 1}; }
        i32 observed = 42;
        const i32* view = &observed;
        fn shared i32 read() move(view) { return *view; }
        i32 read_value = read();
        if (read_value != 42) { throw TestAssertionFailed {.code = 2}; }
        Tracked owner = {.data = new i32(20)};
        fn once i32 work() move(owner) { return *(owner.data); }
        i32 completed = consume(move work);
        if (completed != 20) { throw TestAssertionFailed {.code = 3}; }
        Tracked message = {.data = new i32(22)};
        o<Tracked> pending = o::some(move message);
        fn once i32 deliver() move(pending) {
            o<Tracked> current = core::take(&pending);
            switch (move current) {
            case variant o::some(move value): return *(value.data);
            case variant o::none: return 0;
            }
        }
        i32 delivered = (move deliver).call();
        if (delivered != 22) { throw TestAssertionFailed {.code = 4}; }
        fn once i32 snapshot() move(total) { return total; }
        i32 copy1 = (move snapshot).call();
        i32 copy2 = (move snapshot).call();
        copy2 as void;
        i32 selected = copy1 == 10 && copy2 == 10 ? 0 : 5;
        return selected;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
