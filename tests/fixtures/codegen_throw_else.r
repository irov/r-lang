module test.codegen.throw_else;

error Error { own i32* payload; i32 code; };

protected i32 tick(i32* count) {
    *count += 1;
    return *count;
}

protected i32 run(bool sugar, bool flag) {
    i32 count = 0;
    i32 result = 0;
    i32 left_divisor = 0;
    i32 right_divisor = 0;
    if (flag == true) { left_divisor = 1; } else { right_divisor = 1; }
    try {
        try {
            own i32* left = new i32(40);
            own i32* right = new i32(80);
            if (sugar == true) {
                throw ((tick(&count) == 1) && (flag == true)) Error {
                    .payload = move left,
                    .code = 1 / left_divisor,
                } else Error {
                    .payload = move right,
                    .code = 2 / right_divisor,
                };
            } else {
                if ((tick(&count) == 1) && (flag == true)) {
                    throw Error {
                        .payload = move left,
                        .code = 1 / left_divisor,
                    };
                } else {
                    throw Error {
                        .payload = move right,
                        .code = 2 / right_divisor,
                    };
                }
            }
        } finally {
            count += 10;
        }
    } catch (Error error) {
        result = error.code;
    }
    return result + 100 * count;
}

error First { i32 code; };
error Second { i32 code; };

protected i32 different(bool flag) throws First, Second {
    throw (flag == true) First { .code = 7 } else Second { .code = 9 };
}

protected i32 in_switch(i32 selector, bool flag) throws First {
    switch (selector) {
        case 0:
            throw (flag == true) { .code = 3 } else { .code = 4 };
        default:
            throw (true) First { .code = 5 } else First { .code = 6 };
    }
}

protected void transfer(bool flag, Error error) throws Error {
    throw (flag == true) move error else move error;
}

protected i32 cases(bool flag) {
    i32 result = 0;
    try {
        i32 ignored = different(flag);
        ignored as void;
        return 1;
    } catch (First error) {
        result = error.code;
    } catch (Second error) {
        result = error.code;
    }
    if ((flag == true && result != 7) || (flag == false && result != 9)) { return 2; }
    try {
        i32 ignored = in_switch(0, flag);
        ignored as void;
        return 3;
    } catch (First error) {
        result = error.code;
    }
    if ((flag == true && result != 3) || (flag == false && result != 4)) { return 4; }
    try {
        try {
            i32 ignored = in_switch(1, flag);
            ignored as void;
            return 5;
        } catch (First error) {
            throw (flag == true) (error) else First { .code = error.code + 1 };
        }
    } catch (First error) {
        result = error.code;
    }
    if ((flag == true && result != 5) || (flag == false && result != 6)) { return 6; }
    try {
        own i32* payload = new i32(12);
        Error error = { .payload = move payload, .code = 8 };
        transfer(flag, move error);
        return 7;
    } catch (Error error) {
        if (error.code != 8) { return 8; }
    }
    return 0;
}

i32 main() {
    try {
        i32 a = run(true, true);
        i32 b = run(false, true);
        i32 c = run(true, false);
        i32 d = run(false, false);
        if (a != b || a != 1101 || c != d || c != 1102) { throw TestAssertionFailed {.code = 1}; }
        i32 first = cases(true);
        i32 second = cases(false);
        if (first != 0 || second != 0) { throw TestAssertionFailed {.code = 2}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
