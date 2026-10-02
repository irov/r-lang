module test.codegen.async_never_results;

/* R-TYPE-0007, R-STMT-0012, R-FUNC-0012: an async function with a never result. Its body ends
   every path in a throw, a never call or an endless loop; the call form of await observes it as
   a terminating statement, an initializer, a return operand and an operand, and only its checked
   completion errors reach the awaiting frame. */

error Stop { i32 code; };

protected async never stop(i32 code) throws Stop {
    throw Stop {.code = code};
}

protected never refuse(i32 code) throws Stop {
    throw Stop {.code = code};
}

/* The endless loop leaves only through an awaited never call. */
protected async never stop_after(i32 limit) throws Stop, std.async::start_error {
    i32 count = 0;
    while (true) {
        count += 1;
        if (count == limit) {
            await stop(count * 10);
        }
    }
}

/* A synchronous never call ends the async body. */
protected async never stop_now(i32 code) throws Stop {
    refuse(code + 1);
}

protected async i32 statement(i32 code) {
    try {
        await stop(code);
    } catch (Stop failure) {
        return failure.code;
    } catch (std.async::start_error failure) {
        failure as void;
        return -1;
    }
}

protected async i32 initializer(i32 limit) {
    try {
        i32 value = await stop_after(limit);
        return value;
    } catch (Stop failure) {
        return failure.code;
    } catch (std.async::start_error failure) {
        failure as void;
        return -1;
    }
}

protected async i32 returned(i32 code) {
    try {
        return await stop_now(code);
    } catch (Stop failure) {
        return failure.code;
    } catch (std.async::start_error failure) {
        failure as void;
        return -1;
    }
}

protected async i32 operand(i32 code) {
    try {
        return 1 + await stop(code);
    } catch (Stop failure) {
        return failure.code;
    } catch (std.async::start_error failure) {
        failure as void;
        return -1;
    }
}

/* The completion error propagates through a declared throws set of the awaiting frame. */
protected async i32 forwarded(i32 code) throws Stop, std.async::start_error {
    if (code > 0) {
        await stop(code);
    }
    return code;
}

async i32 main() {
    try {
        if (await statement(7) != 7) { return 1; }
        if (await initializer(3) != 30) { return 2; }
        if (await returned(4) != 5) { return 3; }
        if (await operand(6) != 6) { return 4; }
        try {
            if (await forwarded(0) != 0) { return 5; }
            i32 value = await forwarded(8);
            return 6 + value;
        } catch (Stop failure) {
            if (failure.code != 8) { return 7; }
        }
    } catch (std.async::start_error failure) {
        failure as void;
        return 8;
    }
    return 0;
}
