module test.codegen.async_throw_else;

error Error { own i32* payload; i32 code; };

protected i32 tick(i32* count) {
    *count += 1;
    return *count;
}

protected async i32 step() { return 1; }

protected async i32 run(bool sugar, bool flag) throws std.async::start_error {
    task<i32> pending = step();
    i32 started = await move pending;
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
    task<i32> after = step();
    i32 resumed = await move after;
    return result + 100 * count + 10000 * started + 100000 * resumed;
}

async i32 main() {
    try {
        try {
            task<i32 throws std.async::start_error> first = run(true, true);
            i32 a = await move first;
            task<i32 throws std.async::start_error> second = run(false, true);
            i32 b = await move second;
            task<i32 throws std.async::start_error> third = run(true, false);
            i32 c = await move third;
            task<i32 throws std.async::start_error> fourth = run(false, false);
            i32 d = await move fourth;
            if (a != b || a != 111101 || c != d || c != 111102) { throw TestAssertionFailed {.code = 1}; }
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 2};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
