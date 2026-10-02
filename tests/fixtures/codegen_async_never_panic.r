module test.codegen.async_never_panic;

/* R-TYPE-0007, R-FUNC-0012: an async never function without completion errors ends only in a
   panic, which the call form of await re-raises in the awaiting frame. An async body with a value
   result that only panics leaves its result storage unused. */

protected async never spin(i32 limit) {
    i32 count = 0;
    while (true) {
        count += 1;
        if (count == limit) {
            panic("limit reached");
        }
    }
}

protected async i32 refuse(i32 code) {
    panic("refused");
}

protected async i32 choose(i32 code) throws std.async::start_error {
    if (code > 100) {
        return await refuse(code);
    }
    await spin(code);
}

async i32 main() {
    try {
        return await choose(3);
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}
