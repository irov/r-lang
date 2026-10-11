module test.codegen.async_pending_phi_returns;

/* B11-2: a returned value that a short-circuit operator selects passes through the finally
   block of an async function after an await and is read again after it; an async step keeps the
   selected value in its frame. */

protected async u32 tick(u32 value) {
    return value + 1u32;
}

protected i32 trace(i32 marker) {
    static i32 total = 0;
    unsafe {
        total += marker;
        return total;
    }
}

protected async bool both(bool left, u32 value) throws std.async::start_error {
    u32 next = await tick(value);
    try {
        return (next == 8u32) && left;
    } finally {
        trace(1) as void;
    }
}

protected async bool either(bool left, u32 value) throws std.async::start_error {
    u32 next = await tick(value);
    try {
        return (next == 8u32) || left;
    } finally {
        trace(1) as void;
    }
}

async i32 main() {
    try {
        if (((await both(true, 7u32)) == false) || ((await both(false, 7u32)) == true)) {
            return 1;
        }
        if (((await either(false, 7u32)) == false) || ((await either(false, 8u32)) == true)) {
            return 2;
        }
    } catch (std.async::start_error failure) {
        return 3;
    }
    if (trace(0) != 4) {
        return 4;
    }
    return 0;
}
