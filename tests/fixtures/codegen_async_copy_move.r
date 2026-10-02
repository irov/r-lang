module test.codegen.async_copy_move;

struct Pair { i32 value; };
error Failure { i32 value; };
enum Kind { First, Second, };

protected i32 exercise() {
    const i32 scalar = 7;
    i32 a = move scalar;
    i32 b = move scalar;
    if (a != 7 || b != 7 || scalar != 7) { return 1; }
    Pair pair = {.value=11};
    Pair other = move pair;
    other.value = 13;
    if (pair.value != 11 || other.value != 13) { return 2; }
    const i32[2] source = {17, 19};
    i32[2] copied = move source;
    copied[0] = 23;
    if (source[0] != 17 || source[1] != 19 || copied[0] != 23) { return 3; }
    Kind kind = Kind::Second;
    Kind selected = move kind;
    if (selected != Kind::Second || kind != Kind::Second) { return 4; }
    switch (move kind) {
    case Kind::First: return 10;
    case Kind::Second: break;
    }
    if (kind != Kind::Second) { return 11; }
    switch (move scalar) {
    case 7: break;
    default: return 12;
    }
    if (scalar != 7) { return 13; }
    Failure failure = {.value=29};
    try { throw move failure; }
    catch (Failure caught) { if (caught.value != 29) { return 5; } }
    if (failure.value != 29) { return 6; }
    own i32* owned = new i32(31);
    own i32* transferred = move owned;
    if (*transferred != 31) { return 7; }
    return 0;
}

protected async i32 relay(const i32 value) {
    i32 copied = move value;
    if (copied != value) { return 8; }
    i32 outcome = exercise(); return outcome;
}
async i32 main() {
    try {
        try {
            const i32 value = 37;
            task<i32> work = relay(move value);
            i32 result = await move work;
            if (value != 37) { throw TestAssertionFailed {.code = 9}; }
            return result;
        } catch (std.async::start_error error) { error as void; throw TestAssertionFailed {.code = 99}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
