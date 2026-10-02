module test.codegen.const_checked_locals;

/* R-INIT-0001, R-ERR-0001: a const local initialized by a checked call names the success value of
   the call's carrier, in synchronous and asynchronous bodies. */

error Bad { i32 code; };

i32 pick(i32 x) throws Bad {
    if (x < 0) { throw Bad {.code = x}; }
    return x + 1;
}

protected async i32 later(i32 x) throws Bad {
    if (x < 0) { throw Bad {.code = x - 1}; }
    return x + 2;
}

i32 synchronous() {
    try {
        const i32 first = pick(4);
        const i32 second = pick(first);
        if (second != 6) { return 1; }
        const i32 failed = pick(-3);
        return failed;
    } catch (Bad failure) {
        if (failure.code != -3) { return 2; }
    }
    return 0;
}

async i32 main() {
    const i32 status = synchronous();
    if (status != 0) { return status; }
    try {
        const i32 value = await later(1);
        if (value != 3) { return 3; }
        const i32 failed = await later(-1);
        return failed;
    } catch (Bad failure) {
        if (failure.code != -2) { return 4; }
    } catch (std.async::start_error failure) {
        failure as void;
        return 5;
    }
    return 0;
}
