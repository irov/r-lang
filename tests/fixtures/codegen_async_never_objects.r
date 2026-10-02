module test.codegen.async_never_objects;

/* R-TYPE-0007: never locals, fields, parameters and payloads in async frames; the never paths
   stay unreachable at run time. */

struct Halt {
    i32 code;
    never reason;
};

enum Step {
    Run(i32),
    Stop(never),
};

never fail(str message) {
    panic(message);
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work(i32 x) throws std.async::start_error {
    i32 a = await tick(x);
    if (a > 0) {
        return a;
    }
    never stop = fail("work");
    Halt h = Halt {.code = 1, .reason = stop};
    i32 y = h.reason;
    return y;
}

protected async i32 run(Step s) {
    switch (s) {
        case variant Step::Run(v):
            return *v;
        case variant Step::Stop(n):
            return 0;
    }
}

protected async i32 take(never value) {
    return value;
}

protected async i32 call(i32 x) throws std.async::start_error {
    if (x > 0) {
        return await run(Step::Run(x));
    }
    return await take(fail("take"));
}

async i32 main() {
    try {
        i32 first = await work(1);
        i32 second = await call(3);
        return first + second - 5;
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
